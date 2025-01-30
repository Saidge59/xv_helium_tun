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

#define PAGE_ALIGN(x) (((x) + sysconf(_SC_PAGESIZE) - 1) & ~(sysconf(_SC_PAGESIZE) - 1))

int hpt_efd(struct hpt *dev)
{
    return dev->fd;
}

int hpt_init()
{
    return 0;
}

void hpt_close(struct hpt *dev)
{
	if(!dev) return;

    if(dev->ring_memory) munmap(dev->ring_memory, dev->ring_memory_size);

    //if(dev->loop) uv_loop_close(dev->loop);

	if(dev->fd) close(dev->fd);

	free(dev);
	
	printf("Closed %s\n", HPT_DEVICE_NAME);
}

struct hpt *hpt_alloc(const char name[HPT_NAMESIZE], size_t ring_buffer_items)
{
    if(ring_buffer_items == 0 || ring_buffer_items > HPT_MAX_ITEMS)
    {
        printf("Cannot allocate that count buffers\n");
        return NULL;
    }

    int ret;
    struct hpt *dev;
    struct hpt_net_device_param net_dev_info;
    void** ring_memory;
    size_t num_ring_memory;

    dev = malloc(sizeof(struct hpt));
    if(!dev)
    {
        printf("Cannot allocate 'struct hpt'\n");
        return NULL;
    }

	memset(dev, 0, sizeof(struct hpt));

    dev->fd = open(HPT_DEVICE_PATH, O_RDWR);
    if(dev->fd < 0)
    {
        printf("Error open %s\n", HPT_DEVICE_NAME);
        goto end;
        return NULL;
    }
    printf("Opened %s\n", HPT_DEVICE_NAME);

    memset(&net_dev_info, 0, sizeof(net_dev_info));

	strncpy(net_dev_info.name, name, HPT_NAMESIZE - 1);
	net_dev_info.name[HPT_NAMESIZE - 1] = 0;

    net_dev_info.ring_buffer_items = ring_buffer_items;

	ret = ioctl(dev->fd, HPT_IOCTL_CREATE, &net_dev_info);
	if (ret < 0) {
        printf("Error create ioctl\n");
        goto end;
	}

	num_ring_memory = (2 * sizeof(struct hpt_ring_buffer)) + (2 * ring_buffer_items * HPT_RB_ELEMENT_SIZE);
    size_t aligned_size = PAGE_ALIGN(num_ring_memory);

    ring_memory = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if(ring_memory == MAP_FAILED) 
    {
        printf("Error allocate memory %zu\n", aligned_size);
        goto end;
    }

    dev->ring_memory = ring_memory;
    dev->ring_memory_size = aligned_size;
	dev->ring_buffer_items = ring_buffer_items;
	strncpy(dev->name, name, HPT_NAMESIZE - 1);
	dev->name[HPT_NAMESIZE - 1] = 0;

    dev->ring_info_tx = (struct hpt_ring_buffer *)dev->ring_memory;
	dev->ring_info_rx = dev->ring_info_tx + 1;
    dev->ring_data_tx = (uint8_t *)(dev->ring_info_rx + 1);
    dev->ring_data_rx = dev->ring_data_tx + (dev->ring_buffer_items * HPT_RB_ELEMENT_SIZE);

    printf("Memory mapped to user space at %p\n", ring_memory);
    printf("Memory mapped size %ld\n", aligned_size);

    return dev;

end:
    hpt_close(dev);
    return NULL;
}

void hpt_drain(struct hpt *dev, hpt_do_pkt read_cb, void *handle)
{
	size_t num = hpt_count_items(dev->ring_info_tx);
    struct hpt_ring_buffer_element *item;

    for(size_t j = 0; j < num; j++)
    {
        item = hpt_get_item(dev->ring_info_tx, dev->ring_buffer_items, dev->ring_data_tx);
        if(!item) continue;
        read_cb(handle, item->data, item->len);
        hpt_set_read_item(dev->ring_info_tx);
    }
}

void hpt_write(struct hpt *dev, uint8_t *data, size_t len)
{
    hpt_set_item(dev->ring_info_rx, dev->ring_buffer_items, dev->ring_data_rx, data, len);
}
