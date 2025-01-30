#include <hpt/hpt_common.h>
#include "hpt_dev.h"
#include <linux/dma-mapping.h>
#include <linux/page-flags.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/platform_device.h> 
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/hrtimer.h>

MODULE_VERSION(HPT_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Blake Loring, Aplit-Soft ltd");
MODULE_DESCRIPTION("High Performance TUN device");

#define SLEEP_NS 30ULL

/**********************************************************************************************//**
* @brief hpt_kernel_thread: Main kernel thread function for the HPT device
* @param param: Pointer to parameters passed to the thread
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_kernel_thread(void *param);

/**********************************************************************************************//**
* @brief hpt_capable: Check if the system is capable of running the HPT device
* @return True if capable, false otherwise
**************************************************************************************************/
static inline bool hpt_capable(void);

/**********************************************************************************************//**
* @brief hpt_run_thread: Start the kernel thread for the HPT device
* @param hpt: Pointer to the hpt_net_device_info structure
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_run_thread(struct hpt_net_device_info *hpt);

/**********************************************************************************************//**
* @brief hpt_open: Open function for the HPT device file
* @param inode: Pointer to the inode structure representing the device file
* @param file: Pointer to the file structure for the opened device
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_open(struct inode *inode, struct file *file);

/**********************************************************************************************//**
* @brief hpt_release: Release function for the HPT device file
* @param inode: Pointer to the inode structure representing the device file
* @param file: Pointer to the file structure for the released device
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_release(struct inode *inode, struct file *file);

/**********************************************************************************************//**
* @brief hpt_mmap: Memory mapping function for the HPT device
* @param file: Pointer to the file structure for the device
* @param vma: Pointer to the vm_area_struct representing the memory area to map
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_mmap(struct file *file, struct vm_area_struct *vma);

/**********************************************************************************************//**
* @brief hpt_ioctl_create: Handle an ioctl create request for the HPT device
* @param file: Pointer to the file structure for the device
* @param net: Pointer to the net structure for the associated network namespace
* @param ioctl_num: IOCTL command number
* @param ioctl_param: IOCTL parameter
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_ioctl_create(struct file *file, struct net *net,
                            uint32_t ioctl_num, unsigned long ioctl_param);

/**********************************************************************************************//**
* @brief hpt_ioctl: Handle generic ioctl requests for the HPT device
* @param file: Pointer to the file structure for the device
* @param ioctl_num: IOCTL command number
* @param ioctl_param: IOCTL parameter
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static long hpt_ioctl(struct file *file, uint32_t ioctl_num,
                      unsigned long ioctl_param);

/**********************************************************************************************//**
* @brief hpt_init: Initialize the HPT driver
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int __init hpt_init(void);

/**********************************************************************************************//**
* @brief hpt_exit: Cleanup and exit the HPT driver
**************************************************************************************************/
static void __exit hpt_exit(void);

#define PAGES_PER_BLOCK 128
#define DMA_BLOCK_SIZE (PAGES_PER_BLOCK * PAGE_SIZE)

extern struct hpt_dev *hpt_device;

/*static int hpt_preallocation_skb(struct hpt_net_device_info *dev_info)
{
	struct sk_buff *skb;
	int size = HPT_BUFFER_HALF_SIZE;

    for(int i = 0; i < HPT_SKB_COUNT; i++) 
	{
		skb = dev_info->sk_buffers[i];
		if(!skb)
		{
			skb = netdev_alloc_skb(dev_info->net_dev, size);
			if (!skb) {
				pr_err("Failed to allocate SKB\n");
				return -ENOMEM;
			}

			dev_info->sk_buffers[i] = skb;
		}
    }

    return 0;
}

static int hpt_free_skb(struct hpt_net_device_info *dev_info)
{
	struct sk_buff *skb;

    for(int i = 0; i < HPT_SKB_COUNT; i++) 
	{
		skb = dev_info->sk_buffers[i];
		if(skb)
		{
			dev_kfree_skb(skb);
			dev_info->sk_buffers[i] = NULL;
		}
    }

    return 0;
}*/

static int hpt_kernel_thread(void *param)
{
	struct hpt_net_device_info *dev_info = param;
	ktime_t waittime = ktime_set(0, SLEEP_NS);

	pr_info("Kernel RX thread %s started!\n", dev_info->name);

	while(!kthread_should_stop()) 
	{ 
        set_current_state(TASK_INTERRUPTIBLE);

        if (schedule_hrtimeout(&waittime, HRTIMER_MODE_REL) != 0) {
            pr_info("Woke early due to signal.\n");
        } else {
			hpt_net_rx(dev_info);
			//hpt_preallocation_skb(dev_info);
        }
        
        if (kthread_should_stop())
            break;
	}

	pr_info("Kernel RX thread %s stopped\n", dev_info->name);

	return 0;
}

static inline bool hpt_capable(void)
{
	return capable(CAP_NET_ADMIN);
}

static int hpt_run_thread(struct hpt_net_device_info *dev_info)
{
	dev_info->pthread = kthread_create(hpt_kernel_thread, (void *)dev_info, "%s", dev_info->name);

	if (IS_ERR(dev_info->pthread)) {
		return -ECANCELED;
	}

	pr_info("Kernel RX thread %s created\n", dev_info->name);

	wake_up_process(dev_info->pthread);

	return 0;
}

static unsigned int hpt_poll(struct file *file, struct poll_table_struct *poll_table)
{
    struct hpt_net_device_info *dev_info = file->private_data;

	unsigned int mask = 0;

	if(dev_info) 
	{
		poll_wait(file, &dev_info->tx_busy, poll_table);
		if(hpt_count_items(dev_info->ring_info_tx)) 
		{
			mask |= POLLIN | POLLRDNORM; /* readable */
		}
	}

	return mask;
}

static int hpt_open(struct inode *inode, struct file *file)
{
	int ret;
	
	ret = security_tun_dev_create();
	if (ret < 0) {
		pr_info("Cannot create tun_dev\n");
		return ret;
	}

	if (!hpt_capable()) {
		return -EINVAL;
	}
	
	file->private_data = NULL;
	pr_info("HPT open!\n");
    return 0;
}

static int hpt_release(struct inode *inode, struct file *file)
{
    struct hpt_net_device_info *dev_info = NULL;

	rtnl_lock();

	dev_info = file->private_data;

	if(dev_info) 
	{
		if (dev_info->pthread) {
			kthread_stop(dev_info->pthread);
			dev_info->pthread = NULL;
		}

		pr_info("Stopped pthread\n");

		//hpt_free_skb(dev_info);

		if(dev_info->ring_memory)
		{
			//vfree(dev_info->ring_memory);
			size_t blocks_size = DMA_BLOCK_SIZE;
			size_t num_blocks = dev_info->ring_memory_size / DMA_BLOCK_SIZE;
			size_t last_block_size = dev_info->ring_memory_size % DMA_BLOCK_SIZE;
			if(last_block_size > 0) num_blocks++;

			for(size_t j = 0; j < num_blocks; j++) 
			{
				if(j == (num_blocks - 1) && last_block_size > 0) blocks_size = last_block_size;

				void *buffer = dev_info->ring_memory[j]; 
    			dma_addr_t dma_handle = dev_info->dma_handle[j];
				dma_free_coherent(&hpt_device->pdev->dev, blocks_size, buffer, dma_handle);
			}

			if(dev_info->ring_memory) 
			{
				vfree(dev_info->ring_memory);
				dev_info->ring_memory = NULL;
			}
			if(dev_info->dma_handle) 
			{
				vfree(dev_info->dma_handle);
				dev_info->dma_handle = NULL;
			}
		}

		if(dev_info->net_dev)
		{
			unregister_netdevice(dev_info->net_dev);
			free_netdev(dev_info->net_dev);
			file->private_data = NULL;
		}
	}

	pr_info("HPT close!\n");

	rtnl_unlock();
	return 0;
}

static int hpt_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0;
	struct hpt_net_device_info *dev_info;
    unsigned long pfn;
	unsigned long size;
	size_t min_size_memory;
	size_t aligned_size;
	size_t num_blocks;
	size_t offset = 0;
	size_t allocated_blocks = 0;
	size_t blocks_size;
	size_t last_block_size = 0;
	dma_addr_t dma_handle;
	void *buffer;

	mutex_lock(&hpt_device->device_mutex);

	dev_info = file->private_data;
	if(!dev_info)
	{
		pr_err("hpt_dev is null\n");
		ret = -EINVAL;
		goto end;
	}


	min_size_memory = (2 * sizeof(struct hpt_ring_buffer)) + (2 * HPT_RB_ELEMENT_SIZE);
	pr_err("The size requested is %zu, minimum size: %lu\n", size, min_size_memory);

	if(vma)
		pr_err("vma->vm_end %lu, vma->vm_start: %lu\n", vma->vm_end, vma->vm_start);

	size = vma->vm_end - vma->vm_start;
	if(size == 0 || size < min_size_memory) 
	{
		pr_err("The size requested is %zu, minimum size: %lu\n", size, min_size_memory);
		ret = -EINVAL;
		goto end;
	}


	aligned_size = PAGE_ALIGN(size);
	if(aligned_size != size)
	{
		pr_err("User didn't align the size memory %zu, aligned size: %zu\n", size, aligned_size);
		ret = -EINVAL;
		goto end;
	}

	blocks_size = DMA_BLOCK_SIZE;
	num_blocks = aligned_size / DMA_BLOCK_SIZE;
	last_block_size = aligned_size % DMA_BLOCK_SIZE;
	if(last_block_size > 0) num_blocks++;

	pr_info("min_size_memory %zu\n", min_size_memory);
	pr_info("aligned_size %zu\n", aligned_size);
	pr_info("size %zu\n", size);
	pr_info("num_blocks %zu\n", num_blocks);
	pr_info("last_block_size %zu\n", last_block_size);
	pr_info("sizeof(void *) %zu\n", sizeof(void *));
	

	dev_info->ring_memory = vzalloc(num_blocks * sizeof(void *));
	dev_info->dma_handle = vzalloc(num_blocks * sizeof(dma_addr_t));

	if (!dev_info->ring_memory || !dev_info->dma_handle) {
		pr_err("Failed to allocate memory for tracking DMA buffers\n");
		ret = -ENOMEM;
		goto free_memory;
	}
	
	for(size_t i = 0; i < num_blocks; i++) 
	{
		if(i == (num_blocks - 1) && last_block_size > 0) blocks_size = last_block_size;
		pr_info("--blocks_size %zu\n", blocks_size);

		buffer = dma_alloc_coherent(&hpt_device->pdev->dev, blocks_size, &dma_handle, GFP_KERNEL);
		if (!buffer) {
			pr_err("Failed to allocate DMA block %zu\n", i);
			ret = -ENOMEM;
			goto free_dma;
		}

		dev_info->ring_memory[i] = buffer;
		dev_info->dma_handle[i] = dma_handle;

		allocated_blocks++;

		pfn = dma_handle >> PAGE_SHIFT;
		if (remap_pfn_range(vma, vma->vm_start + offset, pfn, blocks_size, vma->vm_page_prot)) {
			pr_err("Failed to remap DMA block %zu\n", i);
			ret = -EIO;
			goto free_dma;
		}

		offset += blocks_size;
	}

	set_ring_offset(dev_info, 0);

    pr_info("DMA memory successfully allocated and mapped\n");
	dev_info->ring_memory_size = aligned_size;

	return 0;

free_dma:
	for(size_t j = 0; j < allocated_blocks; j++) 
	{
		buffer = dev_info->ring_memory[j]; 
    	dma_handle = dev_info->dma_handle[j];
		dma_free_coherent(&hpt_device->pdev->dev, blocks_size, dev_info->ring_memory[j], dma_handle);
	}

free_memory:
	if(dev_info->ring_memory) vfree(dev_info->ring_memory);
	if(dev_info->dma_handle) vfree(dev_info->dma_handle);

end:
	mutex_unlock(&hpt_device->device_mutex);
    return ret;
}

/*static int hpt_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0;
	struct hpt_net_device_info *dev_info;
    unsigned long pfn;
	unsigned long size;
	unsigned long num_ring_memory;
	unsigned long virt_addr;

	mutex_lock(&hpt_device->device_mutex);

	size = vma->vm_end - vma->vm_start;

	dev_info = file->private_data;
	if(!dev_info)
	{
		pr_err("hpt_dev is null\n");
		ret = -EINVAL;
		goto end;
	}

	num_ring_memory = (2 * sizeof(struct hpt_ring_buffer)) + (2 * dev_info->ring_buffer_items * HPT_RB_ELEMENT_SIZE);
	if(size < num_ring_memory) 
	{
		pr_info("User requested mmap size: %lu, kernel size: %lu\n", size, num_ring_memory);
		ret = -EINVAL;
		goto end;
	}

	size_t aligned_size = PAGE_ALIGN(num_ring_memory);

	dev_info->ring_memory = vmalloc(aligned_size);
	if(!dev_info->ring_memory) 
	{
		pr_err("Cannot allocate memory\n");
		ret = -EINVAL;
		goto end;
	}

	virt_addr = (unsigned long)dev_info->ring_memory;

	for(size_t i = 0; i < aligned_size; i += PAGE_SIZE) 
	{
		pfn = vmalloc_to_pfn((void *)(virt_addr + i));

		if (remap_pfn_range(vma, vma->vm_start + i, pfn, PAGE_SIZE, vma->vm_page_prot)) {
			pr_err("Failed to remap vmalloc memory at offset %zu\n", i);
			ret = -EIO;
			goto end;
		}
	}

	dev_info->ring_info_tx = (struct hpt_ring_buffer *)dev_info->ring_memory;
	dev_info->ring_info_rx = dev_info->ring_info_tx + 1;
    dev_info->ring_data_tx = (uint8_t *)(dev_info->ring_info_rx + 1);
    dev_info->ring_data_rx = dev_info->ring_data_tx + (dev_info->ring_buffer_items * HPT_RB_ELEMENT_SIZE);

    pr_info("Memory mapped to user space at %lx\n", vma->vm_start);

end:
	mutex_unlock(&hpt_device->device_mutex);

    return ret;
}*/

static int hpt_ioctl_create(struct file *file, struct net *net, uint32_t ioctl_num, unsigned long ioctl_param)
{
	struct net_device *net_dev = NULL;
	struct hpt_net_device_param net_dev_name;
	struct hpt_net_device_info *dev_info;
	int ret;

	if(_IOC_SIZE(ioctl_num) != sizeof(net_dev_name)) 
	{
		pr_err("Error check the buffer size\n");
		return -EINVAL;
	}

	if(copy_from_user(&net_dev_name, (void *)ioctl_param, sizeof(net_dev_name))) 
	{
		pr_err("Error copy hpt info from user space\n");
		return -EFAULT;
	}

	if(strnlen(net_dev_name.name, sizeof(net_dev_name.name)) == sizeof(net_dev_name.name)) 
	{
		pr_err("hpt.name not zero-terminated");
		return -EINVAL;
	}

	if(net_dev_name.ring_buffer_items == 0 || net_dev_name.ring_buffer_items > HPT_MAX_ITEMS)
    {
        pr_err("Cannot allocate %zu buffers\n", net_dev_name.ring_buffer_items);
        return -EINVAL;
    }

	net_dev = alloc_netdev(sizeof(struct hpt_net_device_info), net_dev_name.name,
#ifdef NET_NAME_USER
			       NET_NAME_USER,
#endif
			       hpt_net_init);

	if(net_dev == NULL)
	{
		pr_err("Error allocating device \"%s\"\n", net_dev_name.name);
        return -EBUSY;
	}

	dev_net_set(net_dev, net);

	dev_info = netdev_priv(net_dev);
	if (!dev_info) 
	{ 
		pr_err("Error allocating struct hpt_net_device_info\n");
		goto clean_up;
	}
	
	memset(dev_info, 0, sizeof(struct hpt_net_device_info));

	dev_info->ring_buffer_items = net_dev_name.ring_buffer_items;
	dev_info->net_dev = net_dev;
	
	init_waitqueue_head(&dev_info->tx_busy);

	strncpy(dev_info->name, net_dev_name.name, HPT_NAMESIZE);

	unsigned char virtual_mac_addr[6] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	eth_hw_addr_set(net_dev, virtual_mac_addr);

	ret = register_netdevice(net_dev);
	if (ret) {
		pr_err("Error %i registering device \"%s\"\n", ret, dev_info->name);
		goto clean_up;
	}

	net_dev->needs_free_netdev = true;

	/*ret = hpt_preallocation_skb(dev_info);
	if (ret != 0) {
		pr_err("Failed to preallocation sk buffers\n");
		unregister_netdevice(net_dev);
		goto clean_up;
	}*/

	ret = hpt_run_thread(dev_info);
	if (ret != 0) {
		pr_err("Couldn't start rx kernel thread: %i\n", ret);
		unregister_netdevice(net_dev);
		goto clean_up;
	}
	
	file->private_data = dev_info;

	return 0;

clean_up:
	if (net_dev)
		free_netdev(net_dev);

	return ret;
}

static long hpt_ioctl(struct file *file, uint32_t ioctl_num,
		      unsigned long ioctl_param)
{
	int ret = -EINVAL;
	struct net *net = NULL;

	switch (_IOC_NR(ioctl_num)) {
	case _IOC_NR(HPT_IOCTL_CREATE):
		rtnl_lock();
		net = current->nsproxy->net_ns;
		ret = hpt_ioctl_create(file, net, ioctl_num, ioctl_param);
		rtnl_unlock();
		break;
	default:
		pr_info("IOCTL default\n");
		break;
	}

	return ret;
}

static struct file_operations hpt_fops = {
    .owner = THIS_MODULE,
    .open = hpt_open,
    .release = hpt_release,
    .mmap = hpt_mmap,
	.poll = hpt_poll,
	.unlocked_ioctl = hpt_ioctl,
};

static int __init hpt_init(void)
{
	pr_info("Init HPT!\n");

    int ret;

    hpt_device = kzalloc(sizeof(struct hpt_dev), GFP_KERNEL);
    if (!hpt_device) { return -ENOMEM; }
	
	memset(hpt_device, 0, sizeof(struct hpt_dev));

    ret = alloc_chrdev_region(&hpt_device->devt, 0, 1, HPT_DEVICE_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region\n");
        goto free_hpt_device;
    }

    cdev_init(&hpt_device->cdev, &hpt_fops);
    hpt_device->cdev.owner = THIS_MODULE;
    ret = cdev_add(&hpt_device->cdev, hpt_device->devt, 1);
    if (ret) {
        pr_err("Failed to add cdev\n");
        goto unregister_chrdev_region;
    }

    hpt_device->class = class_create(HPT_DEVICE_NAME);
    if (IS_ERR(hpt_device->class)) {
        pr_err("Failed to create class\n");
        ret = PTR_ERR(hpt_device->class);
        goto del_cdev;
    }

    hpt_device->pdev = platform_device_register_simple(HPT_DEVICE_NAME, -1, NULL, 0);
    if (IS_ERR(hpt_device->pdev)) {
        pr_err("Failed to register platform device\n");
        ret = PTR_ERR(hpt_device->pdev);
        goto destroy_class;
    }

    ret = dma_set_mask_and_coherent(&hpt_device->pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        pr_err("Failed to set DMA mask\n");
        goto unregister_platform_device;
    }

    hpt_device->device = device_create(hpt_device->class, &hpt_device->pdev->dev,
                                      hpt_device->devt, NULL, HPT_DEVICE_NAME);
    if (IS_ERR(hpt_device->device)) {
        pr_err("Failed to create device\n");
        ret = PTR_ERR(hpt_device->device);
        goto unregister_platform_device;
    }

	mutex_init(&hpt_device->device_mutex);

    return 0;


unregister_platform_device:
    platform_device_unregister(hpt_device->pdev);
destroy_class:
    class_destroy(hpt_device->class);
del_cdev:
    cdev_del(&hpt_device->cdev);
unregister_chrdev_region:
    unregister_chrdev_region(hpt_device->devt, 1);
free_hpt_device:
    kfree(hpt_device);
    pr_err("Module initialization failed\n");
    return ret;
}

static void __exit hpt_exit(void)
{
	if(hpt_device)
	{
		device_destroy(hpt_device->class, hpt_device->devt);
		platform_device_unregister(hpt_device->pdev);
		class_destroy(hpt_device->class);
		cdev_del(&hpt_device->cdev);
		unregister_chrdev_region(hpt_device->devt, 1);
		kfree(hpt_device);
	}

	pr_info("Exit HPT!\n");
}

module_init(hpt_init);
module_exit(hpt_exit);


