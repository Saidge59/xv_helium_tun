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
#include <string.h>
#endif

#define HPT_NAMESIZE 32
#define HPT_RB_ELEMENT_SIZE 2048
#define HPT_RB_ELEMENT_USABLE_SPACE (HPT_RB_ELEMENT_SIZE - sizeof(uint16_t))
#define HPT_RB_ELEMENT_PADDING (HPT_RB_ELEMENT_SIZE - (2 * sizeof(uint64_t)))
#define HPT_MTU 1350
#define HPT_MAX_ITEMS 65536
#define PAGES_PER_BLOCK 1024

struct hpt_ring_buffer {
	uint32_t write;
	uint32_t read;
	uint32_t block_ind;
	uint32_t max_block_ind;
	uint32_t min_block_ind;
};

struct hpt_ring_buffer_element {
	uint16_t len;
	uint8_t data[HPT_RB_ELEMENT_USABLE_SPACE];
};

/**********************************************************************************************//**
* @brief Structure to store the name and count buffers of a network device
**************************************************************************************************/
struct hpt_net_device_param
{
	char name[HPT_NAMESIZE];
    size_t ring_buffer_items;
};

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

static inline uint64_t hpt_count_items(struct hpt_ring_buffer *ring)
{
	return ACQUIRE(&ring->write) - ACQUIRE(&ring->read);
}

static inline uint64_t hpt_free_items(struct hpt_ring_buffer *ring, size_t ring_buffer_items)
{
	return ring_buffer_items - hpt_count_items(ring);
}

static inline struct hpt_ring_buffer_element *hpt_get_item(struct hpt_ring_buffer *ring, size_t ring_buffer_items, uint8_t *start_read)
{
	struct hpt_ring_buffer_element *elem;

	if(unlikely(!hpt_count_items(ring))) 
    {
		return NULL;
	}

	elem = start_read + (HPT_RB_ELEMENT_SIZE * ACQUIRE(&ring->read));
	if(!elem) return NULL;

	if(unlikely(elem->len > HPT_RB_ELEMENT_USABLE_SPACE)) 
    {
		return NULL;
	}

	return elem;
}

static inline int hpt_set_item(struct hpt_ring_buffer *ring, size_t ring_buffer_items, uint8_t *start_write, uint8_t *data, size_t len)
{
	struct hpt_ring_buffer_element *elem;

	if(unlikely(!hpt_free_items(ring, ring_buffer_items)) || unlikely(len > HPT_RB_ELEMENT_USABLE_SPACE))
    {
		return 1;
	}

	elem = start_write + (HPT_RB_ELEMENT_SIZE * ACQUIRE(&ring->write));
	if(!elem) return 0;

	elem->len = len;
	memcpy(elem->data, data, len);

	return 0;
}

static inline void hpt_set_read_item(struct hpt_ring_buffer *ring)
{
	if(unlikely(!hpt_count_items(ring))) 
    {
		return;
	}

	uint32_t ind = ACQUIRE(&ring->read) + 1;
	if(ind >= PAGES_PER_BLOCK) 
	{
		STORE(&ring->read, 0);
		return;
	}

	STORE(&ring->read, ind);
}

#endif