#ifndef __FAILSAFE_HTTPD_H__
#define __FAILSAFE_HTTPD_H__

void failsafe_httpd_init(void);
void failsafe_httpd_poll(void);
void failsafe_netif_input(volatile uchar *inpkt, int len);

void httpd_poll(void);
void httpd_stop(void);
int httpd_is_running(void);

#endif