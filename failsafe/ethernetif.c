#include <common.h>
#include <net.h>
#include <malloc.h>

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

static struct netif *g_netif;

static err_t ethernetif_linkoutput(struct netif *netif, struct pbuf *p)
{
	volatile uchar *tmpbuf = net_tx_packet;
	u16_t offset = 0;
	struct pbuf *q;

	for (q = p; q != NULL; q = q->next) {
		if (offset + q->len > PKTSIZE) {
			return ERR_BUF;
		}
		memcpy((void *)(tmpbuf + offset), q->payload, q->len);
		offset += q->len;
	}

	eth_send(net_tx_packet, offset);
	return ERR_OK;
}

static err_t ethernetif_output(struct netif *netif, struct pbuf *p,
				const ip4_addr_t *ipaddr)
{
	return etharp_output(netif, p, ipaddr);
}

static void ethernetif_input(struct netif *netif, volatile uchar *inpkt, int len)
{
	struct pbuf *p, *q;
	u16_t offset = 0;

	if (len <= 0 || len > PKTSIZE)
		return;

	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p == NULL)
		return;

	for (q = p; q != NULL; q = q->next) {
		u16_t copy_len = (offset + q->len > len) ? (len - offset) : q->len;
		memcpy(q->payload, (void *)(inpkt + offset), copy_len);
		offset += copy_len;
	}

	if (netif->input(p, netif) != ERR_OK) {
		pbuf_free(p);
	}
}

err_t ethernetif_init(struct netif *netif)
{
	g_netif = netif;

	netif->name[0] = 'e';
	netif->name[1] = 't';
	netif->output = ethernetif_output;
	netif->linkoutput = ethernetif_linkoutput;
	netif->hwaddr_len = 6;
	netif->hwaddr[0] = net_ethaddr[0];
	netif->hwaddr[1] = net_ethaddr[1];
	netif->hwaddr[2] = net_ethaddr[2];
	netif->hwaddr[3] = net_ethaddr[3];
	netif->hwaddr[4] = net_ethaddr[4];
	netif->hwaddr[5] = net_ethaddr[5];
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

	return ERR_OK;
}

void failsafe_netif_input(volatile uchar *inpkt, int len)
{
	if (g_netif)
		ethernetif_input(g_netif, inpkt, len);
}

u32_t sys_now(void)
{
	return (u32_t)get_timer(0);
}