#ifndef _HPT_H_
#define _HPT_H_

#include "hpt_common.h"
#include <pthread.h>
#include <sys/epoll.h>
#include <uv.h>
#include <unistd.h>

/**********************************************************************************************//**
* @brief Main structure representing the HPT device
**************************************************************************************************/
struct hpt 
{
	char name[HPT_NAMESIZE];
    size_t ring_buffer_items;
    int isTread;
    pthread_t thread_write;
    pthread_mutex_t mutex;
    //int efd;
    uv_loop_t* loop;
    uv_poll_t poll_handle;
    int fd;
    struct hpt_ring_buffer *ring_info_rx;
    struct hpt_ring_buffer *ring_info_tx;
    void *ring_memory;
    size_t size_memory;
    uint8_t *ring_data_rx;
    uint8_t *ring_data_tx;
};

/**********************************************************************************************//**
* @brief hpt_fd: Get file descriptor for the HPT device
* @param dev: Pointer to the HPT device structure
* @return File descriptor for interacting with the HPT device
**************************************************************************************************/
int hpt_fd(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_efd: Get file descriptor for the HPT device
* @param dev: Pointer to the HPT device structure
* @return Event file descriptor for interacting with the HPT device
**************************************************************************************************/
int hpt_efd(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_init: Initialize the HPT system
* @param dev: Pointer to the HPT device structure
* @return 0 on success
* @return Negative value on failure
**************************************************************************************************/
int hpt_init();

/**********************************************************************************************//**
* @brief hpt_close: Close and free the HPT device
* @param dev: Pointer to the HPT device structure
**************************************************************************************************/
void hpt_close(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_alloc: Allocate an HPT device
* @param name: Name of the device
* @alloc_buffers_count: Allocation of buffer count
* @return Pointer to the allocated HPT device on success
* @return NULL on failure
**************************************************************************************************/
struct hpt *hpt_alloc(const char name[HPT_NAMESIZE], size_t alloc_buffers_count);

/**********************************************************************************************//**
* @brief hpt_read: Read packet
* @param dev: Pointer to the hpt structure representing the HPT device
**************************************************************************************************/
void hpt_read(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_write: Write packet
* @param dev: Pointer to the hpt structure representing the HPT device
* @param data: Pointer to the data
* @param len: Length of the packet
**************************************************************************************************/
void hpt_write(struct hpt *dev, uint8_t *data, size_t len);


#define PAYLOAD_SIZE 1024

#endif
