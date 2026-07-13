#ifndef __FAILSAFE_HTTPD_TYPES_H__
#define __FAILSAFE_HTTPD_TYPES_H__

#include "lwip/opt.h"
#include "lwip/tcp.h"

#define STATE_NONE            0
#define STATE_FILE_REQUEST    1
#define STATE_UPLOAD_REQUEST  2

#define ISO_slash   0x2f

struct failsafe_httpd_state {
	u8_t state;
	u32_t last_activity;
	u8_t *dataptr;
	u32_t upload;
	u32_t upload_total;
	u8_t owns_global;
	struct tcp_pcb *pcb;
};

void failsafe_lwip_init(struct ip4_addr *ipaddr, struct ip4_addr *netmask, struct ip4_addr *gw);
void failsafe_httpd_stop(void);
void httpd_send_data(struct failsafe_httpd_state *hs);

#endif