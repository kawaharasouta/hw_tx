#include<linux/module.h>
#include<linux/types.h>
#include<linux/kernel.h>
#include<linux/pci.h>
#include<linux/dma-mapping.h>
#include<linux/timer.h>
#include<linux/workqueue.h>
#include<linux/rtnetlink.h>
#include<linux/pm_runtime.h>

#include"include/reg.h"

#define INTEL_ETHERNET_DEVICE(device_id) {\
	        PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

char *kidnet_msg = "module [kidnet]:";

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
	void *data;
	int len;

	dma_addr_t dma;
};

struct tx_desc {
  uint64_t buffer_addr;
  uint16_t length;
  uint16_t pad[3];
};

struct ring {
	struct adapter *adapter;

	//void *tail;
	//void *head;
	
	uint16_t next_to_use;
	uint16_t next_to_clean;

	struct tx_buffer *tx_ring;

	//!! nic desc_ring memory addr
	struct tx_desc *desc_ring;

	//!! nic desc_ring phisical addr. driver do not use this. use tdba, free.
	dma_addr_t dma;

	//!! num of desc
	uint32_t count;
	//!! size of desc in bytes 
	uint32_t size;
};

struct adapter {
  struct pci_dev *pdev;

	//mmio
  uint32_t *mem_start;
  int mem_len;
  void *mmio_addr;

	dma_addr_t dma;

	struct ring *tx_ring_info;
};

struct adapter *adapter_free;


static inline void 
global_reset(struct adapter *adapter) {
	//printk(KERN_INFO "%s global_reset.\n", kidnet_msg);
	uint32_t ctrl;
	ctrl = kidnet_readl(adapter, 0x0000);

	//!set the RST bit.
	ctrl |= 0x04000000;

	kidnet_writel(adapter, 0x0000, ctrl);
	//kidnet_dump_reg(netdev);
}

static void
initialize_phy_setup_link(struct adapter *adapter) {
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


static int alloc_ringdesc_dma(struct adapter *adapter, struct ring *ring_info) {
	struct pci_dev *pdev = adapter->pdev;

	ring_info->desc_ring = dma_alloc_coherent(&pdev->dev, ring_info->size, &ring_info->dma, GFP_KERNEL);

	if (!ring_info->desc_ring)
		return -ENOMEM;
	
	return 0;
}

static void 
tx_configure(struct adapter *adapter) {
	//!e1000e_configure_tx 
	//!init desc
	struct ring *tx_ring_info = adapter->tx_ring_info;
	uint64_t tdba;
	uint32_t tdlen;

	//! config tx descripter.
	tdba = tx_ring_info->dma;
	tdlen = tx_ring_info->count * sizeof(struct tx_desc);

	kidnet_writel(adapter, 0x3800, tdba & DMA_BIT_MASK(32)); //TDBAL
	kidnet_writel(adapter, 0x3804, tdba >> 32);					//TDBAH
	kidnet_writel(adapter, 0x3808, tdlen);								//TDLEN
	kidnet_writel(adapter, 0x3810, 0);								//TDH
	kidnet_writel(adapter, 0x3818, 0);								//TDT
	
	tx_ring_info->next_to_use = 0;
	tx_ring_info->next_to_clean = 0;

	return;
}

static int 
tx_setup(struct adapter *adapter) {
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
	struct ring *tx_ring_info = adapter->tx_ring_info;
	int ret, size;

	ret = -ENOMEM;
	tx_ring_info->tx_ring = vzalloc((sizeof(struct tx_buffer) * tx_ring_info->count));
	if (tx_ring_info->tx_ring)
		goto err;
	
	tx_ring_info->size = (sizeof(struct tx_desc) * tx_ring_info->count);
	//tx_ring->size = ALIGN(tx_ring->size, 4096);

	ret = alloc_ringdesc_dma(adapter, tx_ring_info);
	if (ret)
		goto err;

	tx_configure(adapter);

	return ret;
	
err:
	vfree(tx_ring_info->tx_ring);
	return ret;
}

static void 
open(struct adapter *adapter) {
	struct pci_dev *pdev = adapter->pdev;

	global_reset(adapter);
	initialize_phy_setup_link(adapter);


	tx_setup(adapter);
}

static int
alloc_ring(struct adapter *adapter) {
	adapter->tx_ring_info = kzalloc(sizeof(struct ring), GFP_KERNEL);
	if (!adapter->tx_ring_info)
		goto tx_err;
	adapter->tx_ring_info->count = 8;
	adapter->tx_ring_info->adapter = adapter;

	return 0;

tx_err:
	kfree(adapter->tx_ring_info);
	return -ENOMEM;
}

static void 
adapter_init(struct adapter *adapter) {
	int ret;
	ret = 0;

	ret = alloc_ring(adapter);
//	if (ret)
//		return ret;
	adapter_free = adapter;
	return ret;
}


int 
hw_tx(void *data, int len, struct adapter *adapter) {
	struct pci_dev *pdev = adapter->pdev;
	struct ring *tx_ring_info = adapter->tx_ring_info;
	struct tx_desc *tx_desc;
	struct tx_buffer *buffer_info;
	int index = tx_ring_info->next_to_use;

//	//!map
	buffer_info = &tx_ring_info->tx_ring[index];
	buffer_info->dma = dma_map_single(&pdev->dev, data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, buffer_info->dma))
			goto dma_err;

	tx_desc = (struct tx_desc *)&tx_ring_info->desc_ring[index];
	//tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
	//tx_desc->length = cpu_to_le16(skb->len);
	tx_desc->buffer_addr = buffer_info->dma;
	tx_desc->length = len;

	index++;
	kidnet_writel(adapter, 0x3818, index);

	if (unlikely(index == tx_ring_info->count))
		index = 0;
	tx_ring_info->next_to_use = index;
	
	return 1;

dma_err:
	buffer_info->dma = 0;
	return -1;
}


static int 
tx_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
	int ret;
	resource_size_t mmio_start, mmio_len;
	int bars;
	uint8_t data[5] = {0xa, 0xb, 0xc, 0xd, 0xe};

	adapter_free = kzalloc(sizeof(struct adapter), GFP_KERNEL);
	struct adapter *adapter = adapter_free;

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
	
	open(adapter);
	tx_setup(adapter);
	//hw_tx(data, 5, adapter);

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
	struct adapter *adapter = adapter_free;

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
