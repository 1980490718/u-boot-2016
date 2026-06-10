#ifndef _NET_HTTPD_H__
#define _NET_HTTPD_H__

void HttpdStart(void);
void HttpdStop(void);
void HttpdDone(void);
void HttpdHandler(void);
void NetSendHttpd(void);
void NetReceiveHttpd(volatile uchar *inpkt, int len);

/* board specific implementation */
extern int do_http_upgrade(const ulong size, const int upgrade_type);
extern int do_http_progress(const int state);
extern void all_led_on(void);
extern void all_led_off(void);

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
void ppe_arp_kickstart(void);
#endif

#endif