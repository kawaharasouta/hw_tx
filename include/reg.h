#ifndef _REG_H_
#define _REG_H_






//except IMS_RXSEQ
#define IMS_ENABLE_MASK ( \
	IMS_RXT0	 |		\
	IMS_TXDW	 |		\
	IMS_RXDMT0 |		\
	IMS_LSC)

//! toriaezu senngenndake sitokuYO
#define IMS_RXT0		0x00000080
#define IMS_TXDW		0x00000001
#define IMS_RXDMT0	0x00000010
#define IMS_LSC			0x00000004

#if 0
//! IMS(Interrupt Mask Set/Read Register) Mask
#define IMS_TXDW	0x00000001
#define IMS_TXQE	0x00000002
#define IMS_LSC		0x00000004
#endif









#define kidnet_write(func, dev, reg, val) \
	func(val, (uint8_t *)(((struct adapter *)dev))->mmio_addr + reg)
#define kidnet_writeb(dev, reg, val) kidnet_write(writeb, dev, reg, val)
#define kidnet_writew(dev, reg, val) kidnet_write(writew, dev, reg, val)
#define kidnet_writel(dev, reg, val) kidnet_write(writel, dev, reg, val)

#define kidnet_read(func, dev, reg) \
	func((uint8_t *)(((struct adapter *)dev))->mmio_addr + reg)
#define kidnet_readb(dev, reg) kidnet_read(readb, dev, reg)
#define kidnet_readw(dev, reg) kidnet_read(readw, dev, reg)
#define kidnet_readl(dev, reg) kidnet_read(readl, dev, reg)




#endif /*_REG_H_*/
