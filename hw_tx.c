#include<linux/module.h>
#include<linux/types.h>
#include<linux/kernel.h>
#include<linux/pci.h>
#include<linux/dma-mapping.h>
#include<linux/timer.h>
#include<linux/workqueue.h>
#include<linux/rtnetlink.h>
#include<linux/pm_runtime.h>

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kawaharasouta <kawahara6514@gmail.com>");
MODULE_DESCRIPTION("tx gomi");

static char tx_driver_name[] = "hw_tx";

static struct pci_device_id tx_pci_tbl[] = {
  INTEL_ETHERNET_DEVICE(0x100E),
  INTEL_ETHERNET_DEVICE(0x2E6E), /* e1000 kvm */

	INTEL_ETHERNET_DEVICE(0x10d3), /* 82574L */
	INTEL_ETHERNET_DEVICE(0x1528), /* X540-t1, X540-t2 */
	/* required last entry */
	{0,}
};

struct tx_buffer {

}

struct tx_buffer *tx_ring;

struct adapter {
  struct pci_dev *pdev;

  uint32_t *mem_start;
  int mem_len;
  void *mmio_addr;

  uint32_t max_frame_size;
  uint32_t min_frame_size;

  struct tx_ring *tx_ring;
  uint16_t tx_ring_count;
};

struct kidnet_regacy_tx_desc {
  uint64_t buffer_addr;
  uint16_t length;
  uint16_t pad[3];
};



static inline void 
kidnet_global_reset(struct adapter *adapter) {
	//printk(KERN_INFO "%s global_reset.\n", kidnet_msg);
	uint32_t ctrl;
	ctrl = kidnet_readl(adapter, 0x0000);

	//!set the RST bit.
	ctrl |= 0x04000000;

	kidnet_writel(adapter, 0x0000, ctrl);
	//kidnet_dump_reg(netdev);
}

static void
kidnet_initialize_phy_setup_link(struct adapter *adapter) {
	//!MAC setting automatically based on duplex and speed resolved by phy.
	uint32_t ctrl;
	ctrl = kidnet_readl(adapter, 0x0000);
	
	//!CTRL.SLU
	ctrl |= 0x00000040;
	kidnet_writel(adapter, 0x0000, ctrl);
}

static inline void 
kidnet_enable_irq(struct adapter *adapter) {
	uint32_t ims = kidnet_readl(adapter, 0x00D0);

	ims |= IMS_ENABLE_MASK;
	kidnet_writel(adapter, 0x00D0, ims);
}

static inline void 
kidnet_disable_irq(struct adapter *adapter) {
	kidnet_writel(adapter, 0x00D8, ~0);
}


static int kidnet_alloc_ringdesc_dma(struct adapter *adapter, struct kidnet_ring *ring) {
	struct pci_dev *pdev = adapter->pdev;

	ring->desc = dma_alloc_coherent(&pdev->dev, ring->size, &ring->dma, GFP_KERNEL);

	if (!ring->desc)
		return -ENOMEM;
	
	return 0;
}

static void 
kidnet_tx_configure(struct adapter *adapter) {
	//!e1000e_configure_tx 
	//!init desc
	struct kidnet_ring *tx_ring = adapter->tx_ring;
	uint64_t tdba;
	uint32_t tdlen;

	//! config tx descripter.
	tdba = tx_ring->dma;
	tdlen = tx_ring->count * sizeof(struct kidnet_regacy_tx_desc);

	kidnet_writel(netdev, 0x3800, tdba & DMA_BIT_MASK(32)); //TDBAL
	kidnet_writel(netdev, 0x3804, tdba >> 32);					//TDBAH
	kidnet_writel(netdev, 0x3808, tdlen);								//TDLEN
	kidnet_writel(netdev, 0x3810, 0);								//TDH
	kidnet_writel(netdev, 0x3818, 0);								//TDT
	
	tx_ring->head = adapter->mmio_addr + 0x3810;
	tx_ring->tail = adapter->mmio_addr + 0x3818;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return;
}

static int 
kidnet_tx_setup(struct adapter *adapter) {
	//!tx_reg_setting
	
	uint32_t txdctl, tctl, tipg;
	//txdctl = kidnet_readl(netdev, 0x3828); 
	//tctl = kidnet_readl(netdev, 0x0400); 
	//tipg = kidnet_readl(netdev, 0x0410); 

	//!TXDCTL GRAN = 1b, WTHRESH = 1b, all other fiekds 0b.
	txdctl = 0x02010000;

	//!TCTL CT = 0x0f, COLD : HDX = 0x1ff, FDC = 0x3f
	//!			PSP = 1b, EN = 1b, all other fields = 0b.
	tctl = 0x0003f0fa;

	//!TIPG IPGT = 8, IPGR1 = 2, IPGR2 = 10
	tipg = 0x10100808;

	//!write val
	kidnet_writel(adapter, 0x3828, txdctl);
	kidnet_writel(adapter, 0x0400, tctl);
	kidnet_writel(adapter, 0x0410, tipg);


	//!e1000e_setup_tx_resources
	//!init buffer
	struct kidnet_ring *tx_ring = adapter->tx_ring;
	int ret, size;

	ret = -ENOMEM;
	size = sizeof(struct kidnet_buffer) * tx_ring->count;
	tx_ring->buffer_info = vzalloc(size);
	if (tx_ring->buffer_info)
		goto err;
	
	tx_ring->size = tx_ring->count * sizeof(struct kidnet_regacy_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	ret = kidnet_alloc_ringdesc_dma(adapter, tx_ring);
	if (ret)
		goto err;

	kidnet_tx_configure(adapter);

	return ret;
	
err:
	vfree(tx_ring->buffer_info);
	return ret;
}



static int
kidnet_alloc_ring(struct adapter *adapter) {
	int size;
	size = sizeof(struct kidnet_ring);

	adapter->tx_ring = kzalloc(size, GFP_KERNEL);
	if (!adapter->tx_ring)
		goto tx_err;
	adapter->tx_ring->count = adapter->tx_ring_count;
	adapter->tx_ring->adapter = adapter;

	return 0;

tx_err:
	kfree(adapter->tx_ring);
	return -ENOMEM;
}

static void 
adapter_init(struct adapter *adapter) {
	int ret;
	ret = 0;
	//! e1000_sw_init
	//adapter->rx_buffer_len = 
	//adapter->rx_ps_bsize0 = 128;
	adapter->max_frame_size = netdev->mtu + ETH_FCS_LEN;
	adapter->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;
	adapter->tx_ring_count = KIDNET_DEFAULT_TXD;

	spin_lock_init(&adapter->lock);


	ret = kidnet_alloc_ring(adapter);
//	if (ret)
//		return ret;
	return ret;

}


int 
kidnet_hw_legacy_tx(struct sk_buff *skb, struct adapter *adapter) {
	struct pci_dev *pdev = adapter->pdev;
	struct kidnet_ring *tx_ring = adapter->tx_ring;
	struct kidnet_regacy_tx_desc *tx_desc;
	struct kidnet_buffer *buffer_info;
	int index = tx_ring->next_to_use;

///////////////////////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!segv		
//	//!map
	buffer_info = &tx_ring->buffer_info[index];
	buffer_info->dma = dma_map_single(&pdev->dev, skb->data, skb->len, DMA_TO_DEVICE);
//	if (dma_mapping_error(&pdev->dev, buffer_info->dma))
//			goto dma_err;
//
//
//	tx_desc = (struct kidnet_regacy_tx_desc *)&tx_ring->desc[index];
//	tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
//	tx_desc->length = cpu_to_le16(skb->len);
//
//	kidnet_writel(netdev, index, tx_ring->tail);
//
//	index++;
//	if (unlikely(index == tx_ring->count))
//		index = 0;
//	tx_ring->next_to_use = index;
	
	return 1;

dma_err:
	buffer_info->dma = 0;
	return -1;

}



static int 
tx_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
	int ret;
	struct adapter *adapter;
	resource_size_t mmio_start, mmio_len;
	int bars;


	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		goto err_dma;
	}

	//!add this call to get the correct BAR mask
	bars = pci_select_bars(pdev, 0);
	ret = pci_request_region(pdev, bars, tx_driver_name);
	if (ret)
		goto err_pci_reg;

#endif
	adapter->pdev = pdev;

	//! setup adapter struct 
	adapter_init(adapter);
	//kidnet_disable_irq(netdev);

	//!mmio setting
	//printk(KERN_INFO "%s pci_resource_start.\n", kidnet_msg);
	mmio_start = pci_resource_start(pdev, bars);
	mmio_len	= pci_resource_len(pdev, bars);
	ret = EIO;
	//printk(KERN_INFO "%s ioremap.\n", kidnet_msg);
	adapter->mmio_addr = ioremap(mmio_start, mmio_len);
	if (!adapter->mmio_addr)
		goto err_ioremap;


	//kidnet_dump_reg(netdev);

	return 0;

err_ioremap:
	pci_release_mem_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);

	return ret;
}

static void 
tx_remove(struct pci_dev *pdev) {
/////////////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	struct adapter *adapter = ;

	iounmap(adapter->mmio_addr);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}


struct pci_driver tx_driver = {
	.name = tx_driver_name,
	.id_table = tx_pci_tbl, /* The device list this driver support. */
	.probe = tx_probe, /* It is called at device detection if it is a prescribed driver, or at (possibly) modprobe otherwise. */
	.remove = tx_remove, /* It is called at device unload. */
};



static int 
tx_init(void) {
	printk(KERN_ALERT "%s loading kidnet.\n", kidnet_msg);
	return pci_register_driver(&tx_driver);
}
static void 
tx_exit(void) {
	printk(KERN_ALERT "%s kidnet bye.\n", kidnet_msg);
	return pci_unregister_driver(&tx_driver);
}
module_init(tx_init);
module_exit(tx_exit);
