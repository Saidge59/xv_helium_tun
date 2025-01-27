#include "hpt.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/**********************************************************************************************//**
* @brief fill_buffer: Fill the buffer with payload
* @param pData: Pointer to the data to be written
**************************************************************************************************/
static void fill_buffer(uint8_t *pData, size_t *len);

/**********************************************************************************************//**
* @brief thread_write: Thread function for handling write operations
* @param arg: Pointer to the argument passed to the thread
* @return Pointer to the result of the thread execution, or NULL on failure
**************************************************************************************************/
static void* thread_write(void* arg);

/**********************************************************************************************//**
* @brief hpt_run: Run the main processing loop for the HPT device
* @param dev: Pointer to the hpt structure representing the device
* @return Integer status code indicating success or failure
**************************************************************************************************/
int hpt_run(struct hpt *dev);

/**********************************************************************************************//**
* @brief on_hpt_event: Callback function triggered on HPT device events
* @param handle: Pointer to the uv_poll_t handle associated with the event
* @param status: Status of the event (0 for success, negative for error)
* @param events: Bitmask indicating the type of event(s) that occurred
**************************************************************************************************/
static void on_hpt_event(uv_poll_t *handle, int status, int events);


static int hpt_set_interface(char *name)
{
    char data[128];

    if(snprintf(data, sizeof(data), "sudo ip link set %s up", name) >= sizeof(data)) {
        fprintf(stderr, "Error: interface name too long\n");
        return -1;
    }

    if(system(data) != 0) {
        fprintf(stderr, "Failed to set interface up\n");
        return -1;
    }

    return 0;
}

static int hpt_set_ip_address(char *name, char *ip_addr)
{
    char data[128];

    /*if(snprintf(data, sizeof(data), "sudo ip addr add %s/24 dev %s", ip_addr, name) >= sizeof(data)) {
        fprintf(stderr, "Error: interface name too long\n");
        return -1;
    }*/

    if(snprintf(data, sizeof(data), "sudo ip addr add 10.0.0.1 peer 10.0.0.2 dev %s", name) >= sizeof(data)) {
        fprintf(stderr, "Error: interface name too long\n");
        return -1;
    }

    if(system(data) != 0) {
        fprintf(stderr, "Failed to assign IP address\n");
        return -1;
    }

    return 0;
}

/*static int hpt_set_qdisc(char *name)
{
    char data[128];

    if(snprintf(data, sizeof(data), "sudo tc qdisc add dev %s root handle 1: mq", name) >= sizeof(data)) {
        fprintf(stderr, "Error: interface name too long\n");
        return -1;
    }

    if(system(data) != 0) {
        fprintf(stderr, "Failed to assign qdisc\n");
        return -1;
    }

    return 0;
}*/

/*static void show_message()
{
    printf("\nPlease use commands:\nq: quit\nw: write\n");
}*/

int main(int argc, char *argv[]) 
{
    if(argc != 4)
    {
        printf("Invalid number of arguments\n");
        return 1;
    }

    int ret;
    //uint8_t *data;
    int count = atoi(argv[2]);
    printf("HPT name %s, count_buffers %d, ip address %s\n", argv[1], count, argv[3]);

    //Init
    ret = hpt_init();
    if(ret != 0) { return 1; }

    //Allocation
    struct hpt *hpt = hpt_alloc(argv[1], count);
    if(!hpt) { hpt_close(hpt); return 1; }

    //Set address
    ret = hpt_set_ip_address(argv[1], argv[3]);
    if(ret != 0) { hpt_close(hpt); return 1; }

    //Set interface
    ret = hpt_set_interface(argv[1]);
    if(ret != 0) { hpt_close(hpt); return 1; }

    /*ret = hpt_set_qdisc(argv[1]);
    if(ret != 0) { hpt_close(hpt); return 1; }*/

    //Run threads
    ret = hpt_run(hpt);
    if(ret != 0) { hpt_close(hpt); return 1; }

    pthread_join(hpt->thread_write, NULL);

    if(hpt->loop) uv_loop_close(hpt->loop);
    
    /*pthread_mutex_destroy(&hpt->mutex);*/

    hpt_close(hpt); 
    
    return 0;
}

int hpt_run(struct hpt *dev)
{
    dev->loop = uv_default_loop();

    uv_poll_init(dev->loop, &dev->poll_handle, dev->fd);

    dev->poll_handle.data = dev;

    uv_poll_start(&dev->poll_handle, UV_READABLE, on_hpt_event);

    /*if (pthread_mutex_init(&dev->mutex, NULL) != 0) {
        printf("Mutex init failed\n");
        return -1;
    }*/

    dev->isTread = 1;

    if (pthread_create(&dev->thread_write, NULL, thread_write, dev) != 0) {
        printf("Error create thread_write\n");
        return -1;
    }
    
    printf("The threads created\n");

    uv_run(dev->loop, UV_RUN_DEFAULT);

    return 0;
}

static void* thread_write(void* arg) 
{
    struct hpt* dev = (struct hpt*)arg;
    uint8_t data[HPT_RB_ELEMENT_SIZE];
    size_t len;

    while(dev->isTread)
    {
        switch(getchar())
        {
            case 'q': 
                dev->isTread = 0;
                break;
            case 'w': 

                fill_buffer(data, &len);
                hpt_write(dev, data, len);

                printf("Write size: %zu\n\n", len);
                break;
        }
    }

    return NULL;
}

static void on_hpt_event(uv_poll_t *handle, int status, int events)
{
    struct hpt *dev = (struct hpt *)handle->data;

    if(status < 0) 
    {
        fprintf(stderr, "Poll error on eventfd: %s\n", uv_strerror(status));
        return;
    }

    if(events & UV_READABLE) 
    {
        hpt_read(dev);

        /*uint64_t val;
        ssize_t n = read(handle->io_watcher.fd, &val, sizeof(val));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("read(efd)");
            }
            return;
        }
        if(n == sizeof(val)) 
        {
            data = hpt_get_tx_buffer(dev);
            if(data)
            {
                print_data(data);
                hpt_read(data); 
                printf("\nRead size: %d\n", hpt_get_tx_buffer_size(data));
            }
        }*/
    }
}

static uint16_t calculate_checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i += 2) {
        uint16_t word = (data[i] << 8) + (i + 1 < length ? data[i + 1] : 0);
        sum += word;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

static uint16_t calculate_udp_checksum(const uint8_t *ip_header, const uint8_t *udp_header, const char *payload, size_t payload_len) {
    uint32_t sum = 0;

    for (int i = 12; i < 20; i += 2) {
        sum += (ip_header[i] << 8) + ip_header[i + 1];
    }
    sum += 17;
    uint16_t udp_len = (udp_header[4] << 8) + udp_header[5];
    sum += udp_len;

    for (size_t i = 0; i < 8; i += 2) {
        sum += (udp_header[i] << 8) + udp_header[i + 1];
    }

    for (size_t i = 0; i < payload_len; i += 2) {
        uint16_t word = (payload[i] << 8) + (i + 1 < payload_len ? payload[i + 1] : 0);
        sum += word;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~sum;
}

static void fill_buffer(uint8_t *pData, size_t *len)
{
    unsigned char ip_header[] = {
        0x45, 0x00, 0x00, 0x00,
        0x1C, 0x46, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0xC0, 0xA8, 0x1F, 0xC8, // 192.168.31.200
        0xC0, 0xA8, 0x1F, 0x01  // 192.168.31.201
    };

    unsigned char udp_header[] = {
        0x48, 0x1D, 0x6C, 0x5C,
        0x00, 0x00, 0x00, 0x00
    };

    char payload[PAYLOAD_SIZE];
	
	static uint8_t new_indx = 0;
	char ch = 'a' + new_indx++;
	
    memset(payload, ch, PAYLOAD_SIZE);

    size_t udp_len = sizeof(udp_header) + PAYLOAD_SIZE;
    size_t ip_len = sizeof(ip_header) + udp_len;

    ip_header[2] = (ip_len >> 8) & 0xFF;
    ip_header[3] = ip_len & 0xFF;

    uint16_t ip_checksum = calculate_checksum(ip_header, sizeof(ip_header));
    ip_header[10] = (ip_checksum >> 8) & 0xFF;
    ip_header[11] = ip_checksum & 0xFF;

    udp_header[4] = (udp_len >> 8) & 0xFF;
    udp_header[5] = udp_len & 0xFF;

    uint16_t udp_checksum = calculate_udp_checksum(ip_header, udp_header, payload, PAYLOAD_SIZE);
    udp_header[6] = (udp_checksum >> 8) & 0xFF;
    udp_header[7] = udp_checksum & 0xFF;

    *len = sizeof(ip_header) + sizeof(udp_header) + PAYLOAD_SIZE;

    memcpy(pData, ip_header, sizeof(ip_header));
    memcpy(pData + sizeof(ip_header), udp_header, sizeof(udp_header));
    memcpy(pData + sizeof(ip_header) + sizeof(udp_header), payload, PAYLOAD_SIZE);
}