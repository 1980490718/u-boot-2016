/*
 * webterm.c - Web Terminal for U-Boot
 * Provides console output capture and transfer to HTTP interface
 */

#include <common.h>
#include <malloc.h>
#include <command.h>
#include "../failsafe/failsafe_httpd.h"
#include "../failsafe/failsafe_httpd_types.h"
#include <net.h>

#define WEBTERM_BUFFER_SIZE 16384
#define WEBTERM_LINE_SIZE 256
#define WEBTERM_RESPONSE_SIZE 32768
#define WEBTERM_MAX_CMD_LEN 4096

struct webterm_buffer {
	char *buffer;
	int size, head, tail, count, overflow;
};

static struct webterm_buffer webterm_out = {0};
static char webterm_line_buffer[WEBTERM_LINE_SIZE];
static int webterm_line_pos = 0;
static int webterm_prev_char_was_cr = 0;

static volatile int webterm_output_seq = 0;

static char webterm_pending_cmd[WEBTERM_MAX_CMD_LEN] = {0};
static volatile int webterm_has_pending_cmd = 0;
volatile int webterm_abort_requested = 0;

static char webterm_response_buf[WEBTERM_RESPONSE_SIZE];
static char webterm_output_buf[WEBTERM_BUFFER_SIZE];

static void webterm_batch_copy(const char *src, int len);
static void webterm_flush_line(void);
static void webterm_add_char(char c);
static void webterm_respond(struct failsafe_httpd_state *hs, int code, const char *ctype, const char *fmt, ...);
static int webterm_parse_post_body(char *data, int data_len, char *out, int out_size);

int webterm_init(void) {
	if (webterm_out.buffer)
		return 0;

	webterm_out.buffer = malloc(WEBTERM_BUFFER_SIZE);
	if (!webterm_out.buffer)
		return -1;

	webterm_out.size = WEBTERM_BUFFER_SIZE;
	webterm_out.head = 0;
	webterm_out.tail = 0;
	webterm_out.count = 0;
	webterm_out.overflow = 0;
	webterm_line_pos = 0;

	return 0;
}

void webterm_reset(void) {
	if (webterm_out.buffer) {
		webterm_out.head = 0;
		webterm_out.tail = 0;
		webterm_out.count = 0;
		webterm_out.overflow = 0;
	}
	webterm_line_pos = 0;
	webterm_prev_char_was_cr = 0;
}

static void webterm_flush_line(void) {
	webterm_line_buffer[webterm_line_pos] = '\n';
	webterm_batch_copy(webterm_line_buffer, webterm_line_pos + 1);
	webterm_line_pos = 0;
}

static void webterm_add_char(char c) {
	if (!webterm_out.buffer)
		return;

	if (c == '\n' || c == '\r') {
		if (c == '\n' && webterm_prev_char_was_cr) {
			webterm_prev_char_was_cr = 0;
			return;
		}
		webterm_prev_char_was_cr = (c == '\r');
		if (webterm_line_pos > 0)
			webterm_flush_line();
		return;
	}

	webterm_prev_char_was_cr = 0;

	if (webterm_line_pos >= WEBTERM_LINE_SIZE - 1)
		webterm_flush_line();

	webterm_line_buffer[webterm_line_pos++] = c;
}

static void webterm_batch_copy(const char *src, int len) {
	int first = min(len, webterm_out.size - webterm_out.head);
	if (first > 0) {
		memcpy(&webterm_out.buffer[webterm_out.head], src, first);
		webterm_out.head = (webterm_out.head + first) % webterm_out.size;
	}
	if (len > first) {
		memcpy(webterm_out.buffer, src + first, len - first);
		webterm_out.head = len - first;
	}
	webterm_out.count += len;
	if (webterm_out.count > webterm_out.size) {
		webterm_out.tail = (webterm_out.tail + webterm_out.count - webterm_out.size) % webterm_out.size;
		webterm_out.count = webterm_out.size;
		webterm_out.overflow = 1;
	}
	webterm_output_seq++;
}

void webterm_capture_output(const char *str) {
	if (!str || !webterm_out.buffer)
		return;
	while (*str)
		webterm_add_char(*str++);
}

void webterm_putc(const char c) {
	webterm_add_char(c);
}

int webterm_get_output(char *buf, int size) {
	int len, first;
	if (!buf || !webterm_out.buffer || size <= 0 || webterm_out.count <= 0) {
		if (buf && size > 0) buf[0] = '\0';
		return 0;
	}

	len = min(size - 1, webterm_out.count);
	first = min(len, webterm_out.size - webterm_out.tail);

	memcpy(buf, &webterm_out.buffer[webterm_out.tail], first);
	if (len > first)
		memcpy(buf + first, webterm_out.buffer, len - first);

	buf[len] = '\0';
	webterm_out.tail = (webterm_out.tail + len) % webterm_out.size;
	webterm_out.count -= len;
	return len;
}

void webterm_execute_command(const char *cmd) {
	webterm_abort_requested = 0;
	snprintf(webterm_output_buf, WEBTERM_BUFFER_SIZE, "> %s\n", cmd);
	webterm_capture_output(webterm_output_buf);
	if (webfailsafe_is_running) {
		strncpy(webterm_pending_cmd, cmd, sizeof(webterm_pending_cmd) - 1);
		webterm_pending_cmd[sizeof(webterm_pending_cmd) - 1] = '\0';
		webterm_has_pending_cmd = 1;
	} else {
		run_command(cmd, 0);
		if (webterm_line_pos > 0) webterm_flush_line();
	}
}

int webterm_run_pending_command(void) {
	if (!webterm_has_pending_cmd)
		return 0;
	webterm_has_pending_cmd = 0;
	webterm_abort_requested = 0;
	run_command(webterm_pending_cmd, 0);
	if (webterm_line_pos > 0) webterm_flush_line();
	return 1;
}

static void webterm_respond(struct failsafe_httpd_state *hs, int code, const char *ctype, const char *fmt, ...) {
	va_list ap;
	int hlen = snprintf(webterm_response_buf, sizeof(webterm_response_buf),
		"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n", code, code == 200 ? "OK" : "405", ctype);
	va_start(ap, fmt);
	hlen += vsnprintf(webterm_response_buf + hlen, sizeof(webterm_response_buf) - hlen, fmt, ap);
	va_end(ap);
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)webterm_response_buf;
	hs->upload = hlen;
	httpd_send_data(hs);
}

static int webterm_parse_post_body(char *data, int data_len, char *out, int out_size) {
	char *body = strstr(data, "\r\n\r\n"), *nl;
	int body_len;

	if (!body)
		return -1;
	body += 4;

	body_len = data_len - (body - data);
	if (body_len <= 0 || body_len > WEBTERM_MAX_CMD_LEN)
		return -1;
	if (body_len >= out_size)
		body_len = out_size - 1;

	memcpy(out, body, body_len);
	out[body_len] = '\0';

	nl = strpbrk(out, "\r\n");
	if (nl) *nl = '\0';
	return body_len;
}

void webterm_http_handler(struct failsafe_httpd_state *hs, char *data, int data_len) {
	const char *path;
	int is_post, len;
	char cmd[WEBTERM_MAX_CMD_LEN];

	if (!hs || !data) return;

	if (strncmp(data, "GET ", 4) == 0) {
		path = data + 4;
		is_post = 0;
	} else if (strncmp(data, "POST ", 5) == 0) {
		path = data + 5;
		is_post = 1;
	} else {
		return;
	}

	if (strncmp(path, "/webterm/", 9) != 0)
		return;
	path += 9;

	if (strncmp(path, "cmd", 3) == 0) {
		if (is_post) {
			if (webterm_parse_post_body(data, data_len, cmd, sizeof(cmd)) > 0)
				webterm_execute_command(cmd);
		}
		webterm_respond(hs, is_post ? 200 : 405, "text/plain", is_post ? "OK\n" : "405\n");
	} else if (strncmp(path, "abort", 5) == 0) {
		if (is_post)
			webterm_abort_requested = 1;
		webterm_respond(hs, is_post ? 200 : 405, "text/plain", is_post ? "OK\n" : "405\n");
	} else if (is_post) {
		return;
	} else if (strncmp(path, "status", 6) == 0) {
		if (webterm_line_pos > 0)
			webterm_flush_line();
		webterm_respond(hs, 200, "text/plain", "%d", webterm_output_seq);
	} else if (strncmp(path, "data", 4) == 0) {
		if (webterm_line_pos > 0)
			webterm_flush_line();
		len = webterm_get_output(webterm_output_buf, sizeof(webterm_output_buf));
		webterm_respond(hs, 200, "text/plain; charset=utf-8", "%s", len > 0 ? webterm_output_buf : "");
	}
}

void webterm_puts(const char *str) {
	webterm_capture_output(str);
	puts(str);
}