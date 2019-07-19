#ifndef _KIDNET_H_
#define _KIDNET_H_


#define INTEL_ETHERNET_DEVICE(device_id) {\
				PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

#define KIDNET_DEFAULT_TXD 256
#define KIDNET_DEFAULT_RXD 256


extern char *kidnet_msg = "module [kidnet]:";

//struct kidnet_ps_page {
//	struct page *page;
//	uint64_t addr;
//};

struct kidnet_buffer {
	dma_addr_t dma;
	struct sk_buff *skb;
	union {
		//tx
		struct {
			uint64_t time_stamp;
			uint16_t length;
			uint16_t next_to_watch;
			uint32_t segs;
			uint32_t bytecount;
			uint16_t mapped_as_page;
		};
		//rx
		struct {
			struct e1000_ps_page *ps_pages;
			struct page *page;
		};
	};
};

struct kidnet_ring {
	struct kidnet_adapter *adapter;
	void *desc;
	dma_addr_t dma;
	uint32_t size;
	uint32_t count;

	uint16_t next_to_use;
	uint16_t next_to_clean;

	void *tail;
	void *head;

	struct kidnet_buffer *buffer_info;

	char name[IFNAMSIZ + 5];
	uint32_t ims_val;
	uint32_t itr_val;

	struct sk_buff *rx_skb_top;
};

struct kidnet_adapter {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats stats;

	spinlock_t lock;

	uint32_t *mem_start;
	int mem_len;
	void *mmio_addr;

	uint32_t max_frame_size;
	uint32_t min_frame_size;

	struct kidnet_ring *tx_ring;
	struct kidnet_ring *rx_ring;
	
	uint16_t tx_ring_count;
	uint16_t rx_ring_count;
};

struct kidnet_regacy_tx_desc {
	uint64_t buffer_addr;
	uint16_t length;
	uint16_t pad[3];
};

struct kidnet_regacy_rx_desc {
	uint64_t buffer_addr;
	uint16_t length;
	uint16_t csum;
	uint8_t status;
	uint8_t err;
	uint16_t vlan;
};


#endif /* _KIDNET_H_ */
