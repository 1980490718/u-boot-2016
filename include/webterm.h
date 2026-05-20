/*
 * webterm.h - Web Terminal for U-Boot
 * Header file for console output capture and web interface
 */

#ifndef _WEBTERM_H_
#define _WEBTERM_H_

/* Initialize web terminal */
int webterm_init(void);

/* Capture console output */
void webterm_capture_output(const char *str);

/* Get console output for web interface */
int webterm_get_output(char *buf, int size);

/* Reset web terminal buffer */
void webterm_reset(void);

/* Handle web terminal HTTP requests */
void webterm_http_handler(void);

/* Integration functions */
void webterm_puts(const char *str);
void webterm_putc(const char c);

#endif /* _WEBTERM_H_ */