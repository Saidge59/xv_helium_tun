#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "hpt.h"

/**********************************************************************************************//**
* @brief hpt_get_buffer: Retrieve a buffer based on the given flag
* @param dev: Pointer to the HPT device structure
* @param flag: Flag to determine buffer selection criteria
* @return Pointer to the selected hpt_buffer_info_t structure, or NULL if not found
**************************************************************************************************/
static hpt_buffer_info_t* hpt_get_buffer(struct hpt *dev, const int flag);

int hpt_fd(struct hpt *dev)
{
    return dev->hpt_dev_fd;
}

int hpt_init()
{
    return 0;
}

void hpt_close(struct hpt *dev)
{
	if(!dev) return;

	size_t start = 0;
    size_t end = dev->alloc_buffers_count;
    void* buffer;

    for(int i = start; i < end; i++)
    {
		buffer = (hpt_buffer_info_t *)dev->buffers_combined[i];
		if(buffer) 
        {
            munmap(buffer, HPT_BUFFER_SIZE);	
            dev->buffers_combined[i] = NULL;	
        }
    }

	printf("Free %s\n", dev->name);

    //if(dev->loop) uv_loop_close(dev->loop);

	close(dev->hpt_dev_fd);

	free(dev);
	
	printf("Closed %s\n", HPT_DEVICE_NAME);
}

struct hpt *hpt_alloc(const char name[HPT_NAMESIZE], size_t alloc_buffers_count)
{
    if(alloc_buffers_count == 0 || alloc_buffers_count > HPT_BUFFER_COUNT)
    {
        printf("Cannot allocate that count buffers\n");
        return NULL;
    }

    int ret;
    struct hpt *dev;
    struct hpt_net_device_param net_dev_info;
    void* buffer;

    dev = malloc(sizeof(struct hpt));
    if(!dev)
    {
        printf("Cannot allocate 'struct hpt'\n");
        return NULL;
    }

	memset(dev, 0, sizeof(struct hpt));

    dev->hpt_dev_fd = open(HPT_DEVICE_PATH, O_RDWR);
    if(dev->hpt_dev_fd < 0)
    {
        printf("Error open %s\n", HPT_DEVICE_NAME);
        goto end;
        return NULL;
    }
    printf("Opened %s\n", HPT_DEVICE_NAME);

    memset(&net_dev_info, 0, sizeof(net_dev_info));

	strncpy(net_dev_info.name, name, HPT_NAMESIZE - 1);
	net_dev_info.name[HPT_NAMESIZE - 1] = 0;

    net_dev_info.alloc_buffers_count = alloc_buffers_count;

	ret = ioctl(dev->hpt_dev_fd, HPT_IOCTL_CREATE, &net_dev_info);
	if (ret < 0) {
        printf("Error create ioctl\n");
        goto end;
	}

    buffer = mmap(NULL, HPT_BUFFER_SIZE * alloc_buffers_count, PROT_READ | PROT_WRITE, MAP_SHARED, dev->hpt_dev_fd, 0);
    if(buffer == MAP_FAILED) 
    {
        printf("Error allocate buffers %zu\n", alloc_buffers_count);
        goto end;
    }

    for(int i = 0; i < alloc_buffers_count; i++)
    {
        dev->buffers_combined[i] = buffer + i*HPT_BUFFER_SIZE; 
    }

	dev->alloc_buffers_count = alloc_buffers_count;
	strncpy(dev->name, name, HPT_NAMESIZE - 1);
	dev->name[HPT_NAMESIZE - 1] = 0;

    printf("Allocate buffer success\n");

    return dev;

end:
    hpt_close(dev);
    return NULL;
}

void hpt_read(uint8_t *data)
{
    if(!data) return;

    hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)(data - HPT_START_OFFSET_READ);

    STORE(&buffer_info->state_tx, HPT_BUFFER_TX_EMPTY);
}

void hpt_write(uint8_t *data)
{
    if(!data) return;

    hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)(data - HPT_START_OFFSET_WRITE);

    STORE(&buffer_info->state_rx, HPT_BUFFER_RX_READY);
}


static hpt_buffer_info_t* hpt_get_buffer(struct hpt *dev, const int flag)
{
    hpt_buffer_info_t *buffer_info;
	size_t start = 0;
    size_t end = dev->alloc_buffers_count;

    pthread_mutex_lock(&dev->mutex);

    for(int i = start; i < end; i++)
    {
        buffer_info = (hpt_buffer_info_t *)dev->buffers_combined[i];

        if(!ACQUIRE(&buffer_info->in_use)) continue;
        
        if(flag)
        {
            if(ACQUIRE(&buffer_info->state_tx) != HPT_BUFFER_TX_READY) continue;
            STORE(&buffer_info->state_tx, HPT_BUFFER_TX_BUSY);
        }
        else
        {
            if(ACQUIRE(&buffer_info->state_rx) != HPT_BUFFER_RX_EMPTY) continue;
            STORE(&buffer_info->state_rx, HPT_BUFFER_RX_BUSY);
        }
        
        pthread_mutex_unlock(&dev->mutex);

        return buffer_info;
    }
    pthread_mutex_unlock(&dev->mutex);

    return NULL;
}

uint8_t* hpt_get_tx_buffer(struct hpt *dev) 
{ 
    hpt_buffer_info_t *buffer_info = hpt_get_buffer(dev, 1);

    if(buffer_info) 
        return (uint8_t *)buffer_info + HPT_START_OFFSET_READ; 

    return NULL;
}

uint8_t* hpt_get_rx_buffer(struct hpt *dev) 
{ 
    hpt_buffer_info_t *buffer_info = hpt_get_buffer(dev, 0);

    if(buffer_info) 
        return (uint8_t *)buffer_info + HPT_START_OFFSET_WRITE;

    return NULL;
}

void hpt_set_rx_buffer_size(uint8_t* data, int size)
{
    hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)(data - HPT_START_OFFSET_WRITE);
    buffer_info->size = size;
}

int hpt_get_tx_buffer_size(uint8_t* data)
{
    hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)(data - HPT_START_OFFSET_READ);
    return buffer_info->size;
}

int hpt_get_rx_buffer_size(uint8_t* data)
{
    hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)(data - HPT_START_OFFSET_WRITE);
    return buffer_info->size;
}
