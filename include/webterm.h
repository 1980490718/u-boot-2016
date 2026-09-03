/*
 * webterm.h - Web Terminal for U-Boot
 * Header file for console output capture and web interface
 */

#ifndef _WEBTERM_H_
#define _WEBTERM_H_

#include "../failsafe/failsafe_httpd.h"

struct failsafe_httpd_state;

int webterm_init(void);

void webterm_capture_output(const char *str);

int webterm_get_output(char *buf, int size, int since);

void webterm_reset(void);

void webterm_http_handler(struct failsafe_httpd_state *hs, char *data, int data_len);

void webterm_puts(const char *str);
void webterm_putc(const char c);

int webterm_run_pending_command(void);

#endif /* _WEBTERM_H_ */