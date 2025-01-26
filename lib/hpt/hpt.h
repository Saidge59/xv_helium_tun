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
    void *buffers_combined[HPT_BUFFER_COUNT];
    size_t alloc_buffers_count;
    int isTread;
    pthread_t thread_write;
    pthread_mutex_t mutex;
    //int efd;
    uv_loop_t* loop;
    uv_poll_t poll_handle;
    int hpt_dev_fd;
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
* @brief hpt_get_tx_buffer: Retrieve a receive buffer for the HPT device
* @param dev: Pointer to the hpt structure representing the HPT device
* @return Pointer to the beginning of the data representing the receive buffer, or NULL if not available
**************************************************************************************************/
uint8_t* hpt_get_tx_buffer(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_get_rx_buffer: Retrieve a transmit buffer for the HPT device
* @param dev: Pointer to the hpt structure representing the HPT device
* @return Pointer to the beginning of the data representing the transmit buffer, or NULL if not available
**************************************************************************************************/
uint8_t* hpt_get_rx_buffer(struct hpt *dev);

/**********************************************************************************************//**
* @brief hpt_get_tx_buffer_size: Retrieve a transmit buffer for the HPT device
* @param pData: Pointer to the buffer to store the read data
* @return Return the buffer size
**************************************************************************************************/
int hpt_get_tx_buffer_size(uint8_t* pData);

/**********************************************************************************************//**
* @brief hpt_get_rx_buffer_size: Retrieve a transmit buffer for the HPT device
* @param pData: Pointer to the buffer to store the read data
* @return Return the buffer size
**************************************************************************************************/
int hpt_get_rx_buffer_size(uint8_t* pData);

/**********************************************************************************************//**
* @brief hpt_read: Read data from the HPT device
* @param pData: Pointer to the buffer to store the read data
**************************************************************************************************/
void hpt_read(uint8_t *pData);

/**********************************************************************************************//**
* @brief hpt_write: Write data to the HPT device
* @param pData: Pointer to the buffer containing the data to write
**************************************************************************************************/
void hpt_write(uint8_t *pData);

void hpt_set_rx_buffer_size(uint8_t* data, int size);


#define PAYLOAD_SIZE 1024

#endif
