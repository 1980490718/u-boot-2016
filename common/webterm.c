/*
 * webterm.c - Web Terminal for U-Boot
 * Provides console output capture and transfer to HTTP interface
 */

#include <common.h>
#include <malloc.h>
#include <version.h>
#include <command.h>
#include "../httpd/uipopt.h"
#include "../httpd/httpd.h"
#include "../httpd/uip.h"
#include <cli.h>
#include <net.h>

/* Definitions copied from httpd.c */
#define STATE_NONE					0
#define STATE_FILE_REQUEST			1
#define STATE_UPLOAD_REQUEST		2
#ifdef CONFIG_CMD_SETENV_WEB
#define STATE_ENV_REQUEST			3
#endif

#define ISO_G		0x47
#define ISO_E		0x45
#define ISO_T		0x54
#define ISO_P		0x50
#define ISO_O		0x4f
#define ISO_S		0x53
#define ISO_slash	0x2f
#define ISO_space	0x20
#define ISO_nl		0x0a
#define ISO_cr		0x0d
#define ISO_tab		0x09

/* Web terminal buffer size */
#define WEBTERM_BUFFER_SIZE 16384
#define WEBTERM_LINE_SIZE 256

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

/* Track last read position to avoid duplicate output */
static int last_read_position = 0;

/* Flag to indicate if an interrupt signal was requested */
static volatile int webterm_interrupt_requested = 0;

/* Forward declaration */
void webterm_capture_output(const char *str);

/* External reference to httpd state */
extern struct httpd_state *hs;

/* Initialize web terminal */
int webterm_init(void)
{
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

	memset(webterm_line_buffer, 0, WEBTERM_LINE_SIZE);
	webterm_line_pos = 0;

	/* Initialization successful */
	return 0;
}

/* Check and handle buffer overflow */
static void webterm_check_overflow(void)
{
	// If the buffer has overflowed, reset the read position
	// because we've lost some old data
	if (webterm_out.overflow) {
		last_read_position = 0;  // Reset to beginning since we lost data
		webterm_out.overflow = 0;
	}
}

/* Add character to web terminal buffer */
static void webterm_batch_copy(const char *src, int len);

/* Modified webterm_add_char with batch copy */
static void webterm_add_char(char c)
{
	if (!webterm_out.buffer)
		return;

	if (c == '\n' || c == '\r') {
		if (webterm_line_pos > 0) {
			webterm_line_buffer[webterm_line_pos] = '\0';
			char formatted_line[WEBTERM_LINE_SIZE + 2];
			int len = snprintf(formatted_line, sizeof(formatted_line),
							   "%s\n", webterm_line_buffer);
			webterm_batch_copy(formatted_line, len);
		}
		webterm_line_pos = 0;
		memset(webterm_line_buffer, 0, WEBTERM_LINE_SIZE);
		return;
	}

	if (webterm_line_pos >= WEBTERM_LINE_SIZE - 1) {
		webterm_line_buffer[webterm_line_pos] = '\0';
		char formatted_line[WEBTERM_LINE_SIZE + 2];
		int len = snprintf(formatted_line, sizeof(formatted_line),
						   "%s\n", webterm_line_buffer);
		webterm_batch_copy(formatted_line, len);
		webterm_line_pos = 0;
		memset(webterm_line_buffer, 0, WEBTERM_LINE_SIZE);
		webterm_line_buffer[webterm_line_pos++] = c;
		return;
	}

	webterm_line_buffer[webterm_line_pos++] = c;
	webterm_check_overflow();
}

/* Simple batch copy - 2D optimization (split + memcpy) */
static void webterm_batch_copy(const char *src, int len)
{
	int first = (len <= webterm_out.size - webterm_out.head) ? len : webterm_out.size - webterm_out.head;

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
	// Capture the output for web terminal
	if (webterm_out.buffer) {
		webterm_add_char(c);
	}
}

/* Get console output for web interface - returns only new content since last read */
int webterm_get_output(char *buf, int size)
{
	if (!buf || !webterm_out.buffer || size <= 0)
		return 0;

	// Calculate how many new bytes are available since last read
	int total_available = webterm_out.count;
	int new_bytes_available = total_available - last_read_position;

	if (new_bytes_available <= 0) {
		// No new data since last read
		buf[0] = '\0';
		return 0;
	}

	// Limit to requested size
	int bytes_to_read = min(size - 1, new_bytes_available);
	int read = 0;

	// Calculate starting position for new data
	int start_pos = (webterm_out.tail + last_read_position) % webterm_out.size;

	for (int i = 0; i < bytes_to_read; i++) {
		buf[i] = webterm_out.buffer[start_pos];
		start_pos = (start_pos + 1) % webterm_out.size;
		read++;
	}

	// Update last read position
	last_read_position += read;

	// Keep last_read_position within reasonable bounds
	if (last_read_position > webterm_out.count) {
		last_read_position = webterm_out.count;
	}

	buf[read] = '\0';
	return read;
}

/* Reset web terminal buffer */
void webterm_reset(void)
{
	if (webterm_out.buffer) {
		webterm_out.head = 0;
		webterm_out.tail = 0;
		webterm_out.count = 0;
		webterm_out.overflow = 0;
		memset(webterm_out.buffer, 0, webterm_out.size);
	}

	webterm_line_pos = 0;
	memset(webterm_line_buffer, 0, WEBTERM_LINE_SIZE);

	// Reset tracking of last read position
	last_read_position = 0;
}

/* Execute command and capture output */
void webterm_execute_command(const char *cmd)
{
	// Echo the command to web terminal (plain text format)
	char cmd_echo[512];
	snprintf(cmd_echo, sizeof(cmd_echo), "> %s\n", cmd);
	webterm_capture_output(cmd_echo);

	// Execute the command (output will be captured automatically)
	run_command(cmd, 0);
}

/* Handle web terminal HTTP requests */
void webterm_http_handler(void)
{
	static char response_buffer[32768]; /* Static buffer to persist across calls */

	if (!hs)
		return;

	// Check if this is a web terminal GET request
	if (uip_appdata[0] == ISO_G && uip_appdata[1] == ISO_E && uip_appdata[2] == ISO_T) {
		// Check for /webterm/data endpoint to get just the data
		if (strncmp((char *)&uip_appdata[4], "/webterm/data", 13) == 0) {
			char output[16384];
			int out_len = webterm_get_output(output, sizeof(output));

			int len = snprintf(response_buffer, sizeof(response_buffer),
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain; charset=utf-8\r\n"
				"Cache-Control: no-cache\r\n"
				"Connection: close\r\n\r\n%s",
				out_len > 0 ? output : "");

			hs->state = STATE_FILE_REQUEST;
			hs->dataptr = (u8_t *)response_buffer;
			hs->upload = len;
			uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
			return;
		}
		// Check for /webterm/cmd endpoint - GET not supported
		else if (strncmp((char *)&uip_appdata[4], "/webterm/cmd", 12) == 0) {
			// GET request to /webterm/cmd - return method not allowed
			int len = snprintf(response_buffer, sizeof(response_buffer),
				"HTTP/1.1 405 Method Not Allowed\r\n"
				"Content-Type: text/plain\r\n"
				"Connection: close\r\n\r\n"
				"Method Not Allowed. Use POST for commands.\n");
			hs->state = STATE_FILE_REQUEST;
			hs->dataptr = (u8_t *)response_buffer;
			hs->upload = len;
			uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
			return;
		}
	}
	// Check if this is a web terminal POST request
	else if (uip_appdata[0] == ISO_P && uip_appdata[1] == ISO_O && uip_appdata[2] == ISO_S && uip_appdata[3] == ISO_T) {
		// Handle POST /webterm/cmd request
		if (strncmp((char *)&uip_appdata[5], "/webterm/cmd", 12) == 0) {
			// Find the body of the POST request (after headers)
			char *body = strstr((char *)uip_appdata, "\r\n\r\n");
			if (body) {
				body += 4;
				int header_len = body - (char *)uip_appdata;

				/* Check for valid header length (must not exceed packet length) */
				if (header_len < 0 || header_len > uip_len) {
					/* Invalid request, malformed HTTP data */
					goto send_cmd_response;
				}

				int body_len = uip_len - header_len;

				/* Check for valid body length (must be positive and not too large) */
				if (body_len <= 0 || body_len > 4096) {
					/* Empty command or command too long - ignore */
					goto send_cmd_response;
				}

				char *body_copy = malloc(body_len + 1);
				if (body_copy) {
					memcpy(body_copy, body, body_len);
					body_copy[body_len] = '\0';

					// Process the command - execute it in U-Boot
					// Truncate at first newline if present
					char *nl = strchr(body_copy, '\n');
					if (nl) *nl = '\0';
					nl = strchr(body_copy, '\r');
					if (nl) *nl = '\0';

					// Add the command to webterm output (plain text format)
					char cmd_echo[512];
					snprintf(cmd_echo, sizeof(cmd_echo), "> %s\n", body_copy);
					webterm_capture_output(cmd_echo);

					// Execute the command (output will be captured automatically)
					run_command(body_copy, 0);

					free(body_copy);
				}
			}

			send_cmd_response:
			{}
			// Send a simple response
			int len = snprintf(response_buffer, sizeof(response_buffer),
				"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK\n");
			hs->state = STATE_FILE_REQUEST;
			hs->dataptr = (u8_t *)response_buffer;
			hs->upload = len;
			uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
			return;
		}
	}
}

/* Integration function - override puts to capture output */
void webterm_puts(const char *str)
{
	// Capture the output for web terminal
	webterm_capture_output(str);

	// Still send to original console
	puts(str);
}