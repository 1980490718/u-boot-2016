#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/netif.h"

err_t ethernetif_init(struct netif *netif);
void failsafe_netif_input(volatile uchar *inpkt, int len);

#endif

