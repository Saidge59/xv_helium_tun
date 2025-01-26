#ifndef _HPT_DEV_H_
#define _HPT_DEV_H_

/*
 * This is the central header for the kernel component of the high performance tun device.
 *
 * This code is inspired by work from DPDK, published by Intel (the HPT driver)
 * Copyright(c) 2010-2014 Intel Corporation. 
 *
 * That code was inspired from the book "Linux Device Drivers" by
 * Alessandro Rubini and Jonathan Corbet, published by O'Reilly & Associates
 */

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define HPT_VERSION "1.8"

#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/debugfs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#include <hpt/hpt_common.h>

#if KERNEL_VERSION(5, 6, 0) <= LINUX_VERSION_CODE
#define HAVE_TX_TIMEOUT_TXQUEUE
#endif

#define HPT_KTHREAD_RESCHEDULE_INTERVAL 0 /* us */
#define HPT_BUFFER_COUNT 64000
#define HPT_BUFFER_SIZE 4096
#define HPT_BUFFER_HALF_SIZE (HPT_BUFFER_SIZE >> 1)
#define HPT_SKB_COUNT 1024

/**********************************************************************************************//**
* @brief Structure containing information about a network device
**************************************************************************************************/
struct hpt_net_device_info
{
	char name[HPT_NAMESIZE];
    uint32_t start_indx;
    uint32_t count_buffers;
	struct task_struct *pthread;
	struct net_device *net_dev;
    struct sk_buff *sk_buffers[HPT_SKB_COUNT];
    wait_queue_head_t tx_busy;
    uint32_t tx_count;
};

/**********************************************************************************************//**
* @brief Main structure representing the HPT device
**************************************************************************************************/
struct hpt_dev 
{
    struct class *class;
    struct device *device;
    struct cdev cdev;
    dev_t devt;
    uint32_t count_devices;
    uint32_t allocate_buffers;
    void* buffers_combined[HPT_BUFFER_COUNT];
    dma_addr_t dma_handle[HPT_BUFFER_COUNT];
    struct platform_device *pdev;
    struct mutex device_mutex;
};

/**********************************************************************************************//**
* @brief hpt_net_rx: Handle transmitted network data for the network stack
* @param hpt: Pointer to the hpt_net_device_info structure containing the device information
* @return Number of bytes received and processed
**************************************************************************************************/
size_t hpt_net_rx(struct hpt_net_device_info *hpt);

/**********************************************************************************************//**
* @brief hpt_net_init: Initialize the network settings for the HPT device
* @param dev: Pointer to the net_device structure representing the network device
**************************************************************************************************/
void hpt_net_init(struct net_device *dev);

#define HPT_START_OFFSET_WRITE sizeof(hpt_buffer_info_t)
#define HPT_START_OFFSET_READ (sizeof(hpt_buffer_info_t) + HPT_BUFFER_HALF_SIZE)
#define HPT_MAX_LENGTH_WRITE (HPT_BUFFER_HALF_SIZE - HPT_START_OFFSET_WRITE)
#define HPT_MAX_LENGTH_READ HPT_BUFFER_HALF_SIZE

#endif