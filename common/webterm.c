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

/* Web terminal buffer size */
#define WEBTERM_BUFFER_SIZE 16384
#define WEBTERM_LINE_SIZE 256
#define WEBTERM_RESPONSE_SIZE 32768
#define WEBTERM_MAX_CMD_LEN 4096

/* Circular buffer for console output */
struct webterm_buffer {
	char *buffer;
	int size;
	int head;
	int tail;
	int count;
	int overflow;
};

static struct webterm_buffer webterm_out = {0};
static char webterm_line_buffer[WEBTERM_LINE_SIZE];
static int webterm_line_pos = 0;

static volatile int webterm_output_seq = 0;

static char webterm_pending_cmd[WEBTERM_MAX_CMD_LEN] = {0};
static volatile int webterm_has_pending_cmd = 0;
volatile int webterm_abort_requested = 0;

static char webterm_response_buf[WEBTERM_RESPONSE_SIZE];
static char webterm_output_buf[WEBTERM_BUFFER_SIZE];

/* Forward declaration */
void webterm_capture_output(const char *str);

/* External reference to httpd state */

/* Initialize web terminal */
int webterm_init(void) {
	if (webterm_out.buffer)
		return 0;  // Already initialized

	webterm_out.buffer = malloc(WEBTERM_BUFFER_SIZE);
	if (!webterm_out.buffer) {
		/* Cannot use printf here to avoid recursion */
		return -1;
	}

	webterm_out.size = WEBTERM_BUFFER_SIZE;
	webterm_out.head = 0;
	webterm_out.tail = 0;
	webterm_out.count = 0;
	webterm_out.overflow = 0;

	webterm_line_pos = 0;

	return 0;
}

static void webterm_batch_copy(const char *src, int len);

static void webterm_flush_line(void) {
	char formatted_line[WEBTERM_LINE_SIZE + 2];
	int len;

	webterm_line_buffer[webterm_line_pos] = '\0';
	len = snprintf(formatted_line, sizeof(formatted_line),
			   "%s\n", webterm_line_buffer);
	webterm_batch_copy(formatted_line, len);
	webterm_line_pos = 0;
}

static void webterm_flush_line_buffer(void) {
	if (webterm_line_pos > 0)
		webterm_flush_line();
}

static int webterm_prev_char_was_cr = 0;

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

/* Simple batch copy - 2D optimization (split + memcpy) */
static void webterm_batch_copy(const char *src, int len) {
	int first = min(len, webterm_out.size - webterm_out.head);

	if (first > 0) {
		memcpy(&webterm_out.buffer[webterm_out.head], src, first);
		webterm_out.head = (webterm_out.head + first) % webterm_out.size;
		webterm_out.count += first;
	}

	if (len > first) {
		int second = len - first;
		memcpy(webterm_out.buffer, src + first, second);
		webterm_out.head = second;
		webterm_out.count += second;
	}

	if (webterm_out.count > webterm_out.size) {
		int overrun = webterm_out.count - webterm_out.size;
		webterm_out.tail = (webterm_out.tail + overrun) % webterm_out.size;
		webterm_out.count = webterm_out.size;
		webterm_out.overflow = 1;
	}

	webterm_output_seq++;
}

/* Integration function - capture string output */
void webterm_capture_output(const char *str) {
	if (!str || !webterm_out.buffer)
		return;

	while (*str) {
		webterm_add_char(*str);
		str++;
	}
}

/* Integration function - capture single character output */
void webterm_putc(const char c) {
	webterm_add_char(c);
}

int webterm_get_output(char *buf, int size) {
	int bytes_to_read, first_chunk, start_pos;

	if (!buf || !webterm_out.buffer || size <= 0)
		return 0;

	bytes_to_read = webterm_out.count;
	if (bytes_to_read <= 0) {
		buf[0] = '\0';
		return 0;
	}

	bytes_to_read = min(size - 1, bytes_to_read);
	start_pos = webterm_out.tail;

	first_chunk = webterm_out.size - start_pos;
	if (first_chunk > bytes_to_read)
		first_chunk = bytes_to_read;

	memcpy(buf, &webterm_out.buffer[start_pos], first_chunk);

	if (bytes_to_read > first_chunk)
		memcpy(buf + first_chunk, webterm_out.buffer, bytes_to_read - first_chunk);

	buf[bytes_to_read] = '\0';

	webterm_out.tail = (start_pos + bytes_to_read) % webterm_out.size;
	webterm_out.count -= bytes_to_read;

	return bytes_to_read;
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

void webterm_execute_command(const char *cmd) {
	char cmd_echo[512];
	webterm_abort_requested = 0;
	snprintf(cmd_echo, sizeof(cmd_echo), "> %s\n", cmd);
	webterm_capture_output(cmd_echo);

	if (webfailsafe_is_running) {
		snprintf(webterm_pending_cmd, sizeof(webterm_pending_cmd), "%s", cmd);
		webterm_has_pending_cmd = 1;
	} else {
		run_command(cmd, 0);
		webterm_flush_line_buffer();
	}
}

int webterm_run_pending_command(void) {
	char cmd_copy[WEBTERM_MAX_CMD_LEN];
	if (!webterm_has_pending_cmd)
		return 0;
	webterm_has_pending_cmd = 0;
	webterm_abort_requested = 0;
	strcpy(cmd_copy, webterm_pending_cmd);
	run_command(cmd_copy, 0);
	webterm_flush_line_buffer();
	return 1;
}

static void webterm_respond(struct failsafe_httpd_state *hs, int code, const char *ctype, const char *fmt, ...) {
	va_list ap;
	const char *reason = code == 200 ? "OK" : "Method Not Allowed";
	int hlen = snprintf(webterm_response_buf, sizeof(webterm_response_buf),
		"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
		code, reason, ctype);
	va_start(ap, fmt);
	int blen = vsnprintf(webterm_response_buf + hlen, sizeof(webterm_response_buf) - hlen, fmt, ap);
	va_end(ap);
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)webterm_response_buf;
	hs->upload = hlen + blen;
	httpd_send_data(hs);
}

static int webterm_parse_post_body(char *data, int data_len, char *out, int out_size) {
	char *body = strstr(data, "\r\n\r\n");
	int header_len, body_len;
	char *nl;

	if (!body)
		return -1;

	body += 4;
	header_len = body - data;

	if (header_len > data_len)
		return -1;

	body_len = data_len - header_len;
	if (body_len <= 0 || body_len > WEBTERM_MAX_CMD_LEN)
		return -1;

	if (body_len >= out_size)
		body_len = out_size - 1;

	memcpy(out, body, body_len);
	out[body_len] = '\0';

	nl = strchr(out, '\n');
	if (nl) *nl = '\0';
	nl = strchr(out, '\r');
	if (nl) *nl = '\0';

	return body_len;
}

void webterm_http_handler(struct failsafe_httpd_state *hs, char *data, int data_len) {
	int is_get;
	const char *path;

	if (!hs || !data)
		return;

	is_get = (strncmp(data, "GET ", 4) == 0);
	path = is_get ? data + 4
		   : (strncmp(data, "POST ", 5) == 0) ? data + 5 : NULL;
	if (!path || strncmp(path, "/webterm/", 9) != 0)
		return;
	path += 9;

	if (strncmp(path, "cmd", 3) == 0) {
		if (!is_get) {
			char cmd_buf[WEBTERM_MAX_CMD_LEN];
			if (webterm_parse_post_body(data, data_len, cmd_buf, sizeof(cmd_buf)) > 0)
				webterm_execute_command(cmd_buf);
		}
		webterm_respond(hs, is_get ? 405 : 200, "text/plain",
			is_get ? "Method Not Allowed. Use POST for commands.\n" : "OK\n");
	} else if (strncmp(path, "abort", 5) == 0) {
		if (!is_get)
			webterm_abort_requested = 1;
		webterm_respond(hs, is_get ? 405 : 200, "text/plain",
			is_get ? "Method Not Allowed. Use POST for abort.\n" : "OK\n");
	} else if (!is_get) {
		return;
	} else if (strncmp(path, "status", 6) == 0) {
		webterm_flush_line_buffer();
		webterm_respond(hs, 200, "text/plain", "%d", webterm_output_seq);
	} else if (strncmp(path, "data", 4) == 0) {
		webterm_flush_line_buffer();
		int out_len = webterm_get_output(webterm_output_buf, sizeof(webterm_output_buf));
		webterm_respond(hs, 200, "text/plain; charset=utf-8", "%s",
			out_len > 0 ? webterm_output_buf : "");
	}
}

/* Integration function - override puts to capture output */
void webterm_puts(const char *str) {
	// Capture the output for web terminal
	webterm_capture_output(str);

	// Still send to original console
	puts(str);
}