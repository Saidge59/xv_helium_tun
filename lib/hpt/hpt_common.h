#ifndef _HPT_COMMON_H_
#define _HPT_COMMON_H_

#ifdef __KERNEL__
#include <linux/if.h>
#include <asm/barrier.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#else
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#endif

#define HPT_NAMESIZE 32
#define HPT_BUFFER_COUNT 64000
#define HPT_BUFFER_SIZE 4096
#define HPT_BUFFER_HALF_SIZE (HPT_BUFFER_SIZE >> 1)
#define HPT_MTU 1350
#define HPT_MAX_ITEMS 65536

/**********************************************************************************************//**
* @brief Structure to store the name and count buffers of a network device
**************************************************************************************************/
struct hpt_net_device_param
{
	char name[HPT_NAMESIZE];
    size_t alloc_buffers_count;
};

/**********************************************************************************************//**
* @brief Enumeration status buffer
**************************************************************************************************/
typedef enum 
{
    HPT_BUFFER_RX_EMPTY,
    HPT_BUFFER_TX_EMPTY,
    HPT_BUFFER_RX_READY,
    HPT_BUFFER_TX_READY,
    HPT_BUFFER_RX_BUSY,
    HPT_BUFFER_TX_BUSY,
} hpt_buffer_state_t;

/**********************************************************************************************//**
* @brief Metadata structure for buffer state and size
**************************************************************************************************/
typedef struct hpt_buffer_info 
{
    uint32_t in_use;   
    uint32_t size;            
    hpt_buffer_state_t state_rx;
    hpt_buffer_state_t state_tx;
} hpt_buffer_info_t;

/**********************************************************************************************//**
* @brief Metadata structure for buffer state and size
**************************************************************************************************/
/*typedef struct hpt_buffer_info 
{
    int in_use;          
    int ready_flag_rx;   
    int ready_flag_tx;   
    int size;            
} hpt_buffer_info_t;*/


#ifdef __KERNEL__
#define ACQUIRE(src) smp_load_acquire((src))
#else
#define ACQUIRE(src) __atomic_load_n((src), __ATOMIC_ACQUIRE)
#endif

#ifdef __KERNEL__
#define STORE(dst, val) smp_store_release((dst), (val))
#else
#define STORE(dst, val) __atomic_store_n((dst), (val), __ATOMIC_RELEASE)
#endif


#define HPT_DEVICE_NAME "hpt"
#define HPT_DEVICE_PATH "/dev/hpt"

#define HPT_IOCTL_CREATE _IOWR(0x92, 1, struct hpt_net_device_param)

#define HPT_START_OFFSET_WRITE sizeof(hpt_buffer_info_t)
#define HPT_START_OFFSET_READ (sizeof(hpt_buffer_info_t) + HPT_BUFFER_HALF_SIZE)
#define HPT_MAX_LENGTH_WRITE (HPT_BUFFER_HALF_SIZE - HPT_START_OFFSET_WRITE)
#define HPT_MAX_LENGTH_READ HPT_BUFFER_HALF_SIZE

#endif