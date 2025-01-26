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
* @brief hpt_allocate_buffers: Allocate buffers for the HPT device
* @param hpt: Pointer to the hpt_dev structure
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_allocate_buffers(struct hpt_dev *hpt);

/**********************************************************************************************//**
* @brief hpt_free_buffers: Free allocated buffers for the HPT device
* @param hpt: Pointer to the hpt_dev structure
**************************************************************************************************/
static void hpt_free_buffers(struct hpt_dev *hpt);

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


extern struct hpt_dev *hpt_device;

static int hpt_preallocation_skb(struct hpt_net_device_info *dev_info)
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
}

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
			hpt_preallocation_skb(dev_info);
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
		if(dev_info->tx_count) 
		{
			dev_info->tx_count--;
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

	mutex_lock(&hpt_device->device_mutex);
    hpt_device->count_devices++;
    mutex_unlock(&hpt_device->device_mutex);
	
	file->private_data = NULL;
	pr_info("HPT open!\n");
    return 0;
}

static int hpt_release(struct inode *inode, struct file *file)
{
    struct hpt_net_device_info *dev_info = NULL;

	rtnl_lock();

	mutex_lock(&hpt_device->device_mutex);

	hpt_device->count_devices--;
	if(hpt_device->count_devices == 0)
	{
		hpt_device->allocate_buffers = 0;
	}

	mutex_unlock(&hpt_device->device_mutex);

	dev_info = file->private_data;

	if(dev_info) 
	{
		if (dev_info->pthread != NULL) {
			kthread_stop(dev_info->pthread);
			dev_info->pthread = NULL;
		}

		pr_info("Stopped pthread\n");

		hpt_free_skb(dev_info);

		size_t start = dev_info->start_indx;
		size_t end = dev_info->start_indx + dev_info->count_buffers;
		hpt_buffer_info_t *buffer_info;

		if(dev_info->net_dev != NULL)
		{
			unregister_netdevice(dev_info->net_dev);
			free_netdev(dev_info->net_dev);
			file->private_data = NULL;
		}

		mutex_lock(&hpt_device->device_mutex);

		for (int i = start; i < end; i++) 
		{
			buffer_info = hpt_device->buffers_combined[i];
			if (buffer_info) 
			{
				STORE(&buffer_info->in_use, 0);
			}
		}

		mutex_unlock(&hpt_device->device_mutex);
	}

	pr_info("HPT close!\n");

	rtnl_unlock();
	return 0;
}

static int hpt_allocate_buffers(struct hpt_dev *hpt)
{
	void *buffer;
    for (int i = 0; i < HPT_BUFFER_COUNT; i++) 
	{
		//buffer = dma_alloc_coherent(&hpt->pdev->dev, HPT_BUFFER_SIZE, &hpt->dma_handle[i], GFP_KERNEL);

		buffer = vmalloc_user(HPT_BUFFER_SIZE);

		if (!buffer) {
			pr_err("Failed to allocate combined buffer %d\n", i);
			return -ENOMEM;
		}
		hpt_buffer_info_t *buffer_info = (hpt_buffer_info_t *)buffer;
		STORE(&buffer_info->in_use, 0);
		
		hpt->buffers_combined[i] = buffer;
	}
	
	pr_info("DMA buffer allocated successfully\n");
	return 0;
}

static void hpt_free_buffers(struct hpt_dev *hpt)
{
	void *buffer;
	for (int i = 0; i < HPT_BUFFER_COUNT; i++) 
	{
		buffer = hpt->buffers_combined[i];
		if (buffer) 
		{
			//dma_free_coherent(hpt->device, HPT_BUFFER_SIZE, buffer, hpt->dma_handle[i]);
			vfree(hpt->buffers_combined[i]);

			hpt->buffers_combined[i] = NULL;
            //hpt->dma_handle[i] = 0;
   		}
	}
	
    pr_info("DMA buffer free successfully\n");
}

static int hpt_mmap(struct file *file, struct vm_area_struct *vma)
{
    int buffer_idx;
    void *buffer;
    unsigned long pfn;
    struct hpt_net_device_info *dev_info;
    hpt_buffer_info_t *buffer_info;
	unsigned long offset;

    mutex_lock(&hpt_device->device_mutex);

	dev_info = file->private_data;
	offset = vma->vm_start;

	if(dev_info->count_buffers + hpt_device->allocate_buffers > HPT_BUFFER_COUNT) {
        pr_err("Cannot allocate that count buffers\n");
        mutex_unlock(&hpt_device->device_mutex);
        return -EINVAL;
    }

	dev_info->start_indx = hpt_device->allocate_buffers;

	for(int i = 0; i < dev_info->count_buffers; i++)
	{
		buffer_idx = hpt_device->allocate_buffers;
		buffer = hpt_device->buffers_combined[buffer_idx];
		//pfn = hpt_device->dma_handle[buffer_idx] >> PAGE_SHIFT;
		pfn = page_to_pfn(vmalloc_to_page(buffer));

		if(remap_pfn_range(vma, offset, pfn, HPT_BUFFER_SIZE, vma->vm_page_prot))
		{
        	return -EAGAIN;
		}

		buffer_info = (hpt_buffer_info_t *)buffer;
		STORE(&buffer_info->in_use, 1);
		STORE(&buffer_info->state_rx, HPT_BUFFER_RX_EMPTY);
		STORE(&buffer_info->state_tx, HPT_BUFFER_TX_EMPTY);

		hpt_device->allocate_buffers++;
		offset += HPT_BUFFER_SIZE;
	}

    mutex_unlock(&hpt_device->device_mutex);
	return 0;
}

static int hpt_ioctl_create(struct file *file, struct net *net,
			    uint32_t ioctl_num, unsigned long ioctl_param)
{
	struct net_device *net_dev = NULL;
	struct hpt_net_device_param net_dev_name;
	struct hpt_net_device_info *dev_info;
	int ret;

	if (_IOC_SIZE(ioctl_num) != sizeof(net_dev_name)) {
		pr_err("Error check the buffer size\n");
		return -EINVAL;
	}

	if (copy_from_user(&net_dev_name, (void *)ioctl_param, sizeof(net_dev_name))) {
		pr_err("Error copy hpt info from user space\n");
		return -EFAULT;
	}

	if (strnlen(net_dev_name.name, sizeof(net_dev_name.name)) ==
	    sizeof(net_dev_name.name)) {
		pr_err("hpt.name not zero-terminated");
		return -EINVAL;
	}

	net_dev = alloc_netdev(sizeof(struct hpt_net_device_info), net_dev_name.name,
#ifdef NET_NAME_USER
			       NET_NAME_USER,
#endif
			       hpt_net_init);
	if (net_dev == NULL) {
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

	dev_info->count_buffers = net_dev_name.alloc_buffers_count;
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

	ret = hpt_preallocation_skb(dev_info);
	if (ret != 0) {
		pr_err("Failed to preallocation sk buffers\n");
		unregister_netdevice(net_dev);
		goto clean_up;
	}

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

	ret = hpt_allocate_buffers(hpt_device);
	if (ret != 0) {
		pr_err("Failed to allocate DMA buffer\n");
        ret = -ENOMEM;
        goto destroy_device;
	}

	mutex_init(&hpt_device->device_mutex);

    return 0;

destroy_device:
	device_destroy(hpt_device->class, hpt_device->devt);
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
		hpt_free_buffers(hpt_device);
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


