#ifndef _DEBUG_UTIL_H_
#define _DEBUG_UTIL_H_

#include"reg.h"


inline void 
kidnet_dump_ctrl(struct net_device *netdev) {
	uint32_t ctrl;
	ctrl = kidnet_readl(netdev, 0x0000);
	printk(KERN_INFO "%s ctrl: %08x.\n", kidnet_msg, ctrl);
}
inline void 
kidnet_dump_status(struct net_device *netdev) {
	uint32_t status;
	status = kidnet_readl(netdev, 0x0008);
	printk(KERN_INFO "%s status: %08x.\n", kidnet_msg, status);
}
inline void 
kidnet_dump_ims(struct net_device *netdev) {
	uint32_t ims;
	ims = kidnet_readl(netdev, 0x00D0);
	printk(KERN_INFO "%s ims: %08x.\n", kidnet_msg, ims);
}

void 
kidnet_dump_reg(struct net_device *netdev) {
	kidnet_dump_ctrl(netdev);
	kidnet_dump_status(netdev);
	kidnet_dump_ims(netdev);
}
 

#endif /*_DEBUG_UTIL_H_*/
