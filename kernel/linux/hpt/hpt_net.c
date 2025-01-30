#include "hpt_dev.h"

/**********************************************************************************************//**
* @brief hpt_net_open: Open the network interface for the HPT device
* @param dev: Pointer to the net_device structure representing the network device
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_open(struct net_device *dev);

/**********************************************************************************************//**
* @brief hpt_net_release: Close the network interface for the HPT device
* @param dev: Pointer to the net_device structure representing the network device
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_release(struct net_device *dev);

/**********************************************************************************************//**
* @brief hpt_net_config: Configure the network interface for the HPT device
* @param dev: Pointer to the net_device structure representing the network device
* @param map: Pointer to the ifmap structure containing the configuration parameters
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_config(struct net_device *dev, struct ifmap *map);

/**********************************************************************************************//**
* @brief hpt_net_tx: Receive a network packet through the HPT device
* @param skb: Pointer to the sk_buff structure containing the packet
* @param dev: Pointer to the net_device structure representing the network device
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_tx(struct sk_buff *skb, struct net_device *dev);

/**********************************************************************************************//**
* @brief hpt_net_change_mtu: Change the MTU (Maximum Transmission Unit) of the HPT network device
* @param dev: Pointer to the net_device structure representing the network device
* @param new_mtu: The new MTU value to set
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_change_mtu(struct net_device *dev, int new_mtu);

/**********************************************************************************************//**
* @brief hpt_net_change_rx_flags: Update the RX (receive) flags of the network device
* @param netdev: Pointer to the net_device structure representing the network device
* @param flags: New RX flags to set
**************************************************************************************************/
static void hpt_net_change_rx_flags(struct net_device *netdev, int flags);

/**********************************************************************************************//**
* @brief hpt_net_header: Create a network header for a packet
* @param skb: Pointer to the sk_buff structure containing the packet
* @param dev: Pointer to the net_device structure representing the network device
* @param type: Protocol type for the packet
* @param daddr: Pointer to the destination address
* @param saddr: Pointer to the source address
* @param len: Length of the payload
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_header(struct sk_buff *skb, struct net_device *dev,
                          unsigned short type, const void *daddr,
                          const void *saddr, uint32_t len);

/**********************************************************************************************//**
* @brief hpt_net_change_carrier: Change the carrier state of the network device
* @param dev: Pointer to the net_device structure representing the network device
* @param new_carrier: New carrier state (true for up, false for down)
* @return 0 on success, or a negative error code on failure
**************************************************************************************************/
static int hpt_net_change_carrier(struct net_device *dev, bool new_carrier);

/**********************************************************************************************//**
* @brief hpt_get_drvinfo: Retrieve driver information for the HPT network device
* @param dev: Pointer to the net_device structure representing the network device
* @param info: Pointer to the ethtool_drvinfo structure to populate with driver information
**************************************************************************************************/
static void hpt_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info);

#define WD_TIMEOUT 5 /*jiffies */
#define HPT_WAIT_RESPONSE_TIMEOUT 300 /* 3 seconds */

#define HPT_IP_VERSION 0
#define HPT_IP_HEADER_LENGTH_MSB 2
#define HPT_IP_HEADER_LENGTH_LSB 3
#define HPT_UDP_HEADER_LENGTH_MSB 4
#define HPT_UDP_HEADER_LENGTH_LSB 5

struct hpt_dev *hpt_device;

/*static struct sk_buff *hpt_get_sk_buffer(struct hpt_net_device_info *dev_info)
{
    struct sk_buff *skb;

	for(int i = 0; i < HPT_SKB_COUNT; i++)
	{
		skb = dev_info->sk_buffers[i];
		if(!skb) continue;

		dev_info->sk_buffers[i] = NULL;
		return skb;
	}

    return skb;
}*/

static int hpt_net_open(struct net_device *dev)
{
	netif_start_queue(dev);
	netif_carrier_on(dev);

	return 0;
}

static int hpt_net_release(struct net_device *dev)
{
	netif_stop_queue(dev); /* can't transmit any more */
	netif_carrier_off(dev);
	return 0;
}

static int hpt_net_config(struct net_device *dev, struct ifmap *map)
{
	/* can't act on a running interface */
	if (dev->flags & IFF_UP) {
		return -EBUSY;
	}

	/* ignore other fields */
	return 0;
}

static int hpt_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct hpt_net_device_info *dev_info = netdev_priv(dev);
	if(!dev_info)
	{
		pr_err("hpt_dev is null\n");
		return NETDEV_TX_OK;
	}

	unsigned int len = skb->len;

	if(!len) 
	{
		goto drop;
	}

	if(likely(hpt_set_item(dev_info->ring_info_tx, dev_info->ring_buffer_items, dev_info->ring_data_tx, skb->data, len) != 0))
	{
		goto drop;
	}

	dev_kfree_skb(skb);

	dev_info->net_dev->stats.tx_bytes += len;
	dev_info->net_dev->stats.tx_packets++;

	wake_up_interruptible(&dev_info->tx_busy);

	return NETDEV_TX_OK;

drop:
	dev_kfree_skb(skb);
	dev_info->net_dev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

size_t hpt_net_rx(struct hpt_net_device_info *dev_info)
{
    struct net_device *net_dev = dev_info->net_dev;
    struct sk_buff *skb;
    size_t num_processed = 0;
    int i, num, len;
    u8 ip_version;
	struct hpt_ring_buffer_element *item;

	if(!dev_info->ring_info_rx) return -1;

	num = hpt_count_items(dev_info->ring_info_rx);

	for (i = 0; i < num; i++)
	{
		item = hpt_get_item(dev_info->ring_info_rx, dev_info->ring_buffer_items, dev_info->ring_data_rx);
		if(unlikely(!item)) 
		{
			break;
		}

		len = item->len;

		if(unlikely(len == 0 || len > HPT_RB_ELEMENT_USABLE_SPACE)) 
		{
		    net_dev->stats.rx_dropped++;
			hpt_set_read_item(dev_info->ring_info_rx);
			pr_err("Drop packets that are len out of range\n");
        	continue;
        }

		//skb = hpt_get_sk_buffer(dev_info);
		skb = netdev_alloc_skb(net_dev, len);

        if(unlikely(!skb)) {
            net_dev->stats.rx_dropped++;
			hpt_set_read_item(dev_info->ring_info_rx);
			pr_err("Could not allocate memory to transmit a packet\n");
        	continue;
        }

        memcpy(skb_put(skb, len), item->data, len);
		hpt_set_read_item(dev_info->ring_info_rx);

        ip_version = skb->len ? (skb->data[HPT_IP_VERSION] >> 4) : 0;

        if(unlikely(!(ip_version == 4 || ip_version == 6))) {
            dev_kfree_skb(skb);
            net_dev->stats.rx_dropped++;
			pr_err("Drop packets that are not IPv4 or IPv6\n");
        	continue;
        }

        // Set SKB headers
        skb_reset_mac_header(skb);
        skb->protocol = ip_version == 4 ? htons(ETH_P_IP) : htons(ETH_P_IPV6);
        skb->ip_summed = CHECKSUM_UNNECESSARY;
        skb_reset_network_header(skb);
        skb_probe_transport_header(skb);

        // Send the SKB to the network stack
        netif_rx(skb);

        // Update statistics
        net_dev->stats.rx_bytes += len;
        net_dev->stats.rx_packets++;
        num_processed++;
    }

	return num_processed;
}

#ifdef HAVE_TX_TIMEOUT_TXQUEUE
static void hpt_net_tx_timeout(struct net_device *dev, unsigned int txqueue)
#else
static void hpt_net_tx_timeout(struct net_device *dev)
#endif
{
	pr_debug("Transmit timeout at %ld, latency %ld\n", jiffies,
		 jiffies - dev_trans_start(dev));
	dev->stats.tx_errors++;
	netif_wake_queue(dev);
}

static int hpt_net_change_mtu(struct net_device *dev, int new_mtu)
{
	return -EINVAL;
}

static void hpt_net_change_rx_flags(struct net_device *netdev, int flags)
{
	return;
}

static int hpt_net_header(struct sk_buff *skb, struct net_device *dev,
			  unsigned short type, const void *daddr,
			  const void *saddr, uint32_t len)
{
	return 0;
}

static int hpt_net_change_carrier(struct net_device *dev, bool new_carrier)
{
	if (new_carrier) {
		netif_carrier_on(dev);
	} else {
		netif_carrier_off(dev);
	}
	return 0;
}

static const struct header_ops hpt_net_header_ops = {
	.create = hpt_net_header,
	.parse = eth_header_parse,
	.cache = NULL, /* disable caching */
};

static const struct net_device_ops hpt_net_netdev_ops = {
	.ndo_open = hpt_net_open,
	.ndo_stop = hpt_net_release,
	.ndo_set_config = hpt_net_config,
	.ndo_change_rx_flags = hpt_net_change_rx_flags,
	.ndo_start_xmit = hpt_net_tx,
	.ndo_change_mtu = hpt_net_change_mtu,
	.ndo_tx_timeout = hpt_net_tx_timeout,
	.ndo_change_carrier = hpt_net_change_carrier,
};

static void hpt_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strscpy(info->version, HPT_VERSION, sizeof(info->version));
	strscpy(info->driver, "hpt", sizeof(info->driver));
}

static const struct ethtool_ops hpt_net_ethtool_ops = {
	.get_drvinfo = hpt_get_drvinfo,
	.get_link = ethtool_op_get_link,
};

void hpt_net_init(struct net_device *dev)
{
	/* Point-to-Point TUN Device */
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	/* Zero header length */
	dev->type = ARPHRD_NONE;
	dev->hard_header_len = 0;
	dev->addr_len = 0;

	dev->mtu = HPT_MTU;;
	dev->max_mtu = HPT_MTU;
	dev->min_mtu = HPT_MTU;

	dev->netdev_ops = &hpt_net_netdev_ops;
	dev->header_ops = &hpt_net_header_ops;
	dev->ethtool_ops = &hpt_net_ethtool_ops;
	dev->watchdog_timeo = WD_TIMEOUT;
}
