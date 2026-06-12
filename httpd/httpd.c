#include "uip.h"
#include "httpd.h"
#include "fs.h"
#include "fsdata.h"
#include "uip_arp.h"
#include <common.h>
#include <net.h>
#include "../net/httpd.h"
#include <webterm.h>
#include <asm/gpio.h>
#include <ipq_api.h>
#include <asm-generic/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_DHCPD
#include "../net/dhcpd.h"
#endif
#ifdef CONFIG_CMD_SETENV_WEB
#include <setenv_web.h>
#endif

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

#define is_digit(c) ((c) >= '0' && (c) <= '9')

extern const struct fsdata_file file_index_html;
extern const struct fsdata_file file_404_html;
extern const struct fsdata_file file_flashing_html;
extern const struct fsdata_file file_fail_html;

extern int webfailsafe_ready_for_upgrade;
extern int webfailsafe_upgrade_type;
extern u32 net_boot_file_size;
extern unsigned char *webfailsafe_data_pointer;

#if defined(CONFIG_IPQ807x)
extern int ipq807x_eth_check_link_change(void);
#elif defined(CONFIG_IPQ40XX)
extern int ipq40xx_eth_check_link_change(void);
#elif defined(CONFIG_IPQ6018)
extern int ipq6018_eth_check_link_change(void);
#elif defined(CONFIG_IPQ9574)
extern int ipq9574_eth_check_link_change(void);
#elif defined(CONFIG_IPQ5332)
extern int ipq5332_eth_check_link_change(void);
#elif defined(CONFIG_IPQ5018)
extern int ipq5018_eth_check_link_change(void);
#elif defined(CONFIG_IPQ806x)
extern int ipq806x_eth_check_link_change(void);
#endif

struct httpd_state *hs;

int webfailsafe_post_done = 0;
int file_too_big = 0;
static int webfailsafe_upload_failed = 0;
static int data_start_found = 0;

static unsigned char post_packet_counter = 0;
static unsigned char post_line_counter = 0;

static char eol[3] = { 0x0d, 0x0a, 0x00 };
static char eol2[5] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };

static char *boundary_value;
static unsigned long upload_ram_end;

static int atoi(const char *s) {
	int i = 0;
	while (is_digit(*s)) {
		i = i * 10 + *(s++) - '0';
	}
	return i;
}

static void httpd_download_progress(void) {
	if (post_packet_counter == 80) {
		puts("\n");
		post_packet_counter = 0;
		post_line_counter++;
	}
	if (post_line_counter == 10) {
		post_line_counter = 0;
		do_http_progress(WEBFAILSAFE_PROGRESS_UPLOADING);
	}
	puts("#");
	post_packet_counter++;
}

void httpd_init(void) {
	fs_init();
	uip_listen(HTONS(80));
}

static void httpd_state_reset(void) {
	hs->state = STATE_NONE;
	hs->last_activity = get_timer(0);
	hs->dataptr = 0;
	hs->upload = 0;
	hs->upload_total = 0;
#ifdef CONFIG_CMD_SETENV_WEB
	hs->senddataptr = NULL;
	hs->sendlen = 0;
	hs->request_method = 0;
	hs->length_upload = 0;
	memset(hs->filename, 0, sizeof(hs->filename));
#endif
	data_start_found = 0;
	post_packet_counter = 0;
	file_too_big = 0;
	led_on("blink_led");
	if (boundary_value) {
		free(boundary_value);
		boundary_value = NULL;
	}
}

/* Common error printing functions */
static void print_file_size_error(unsigned long max_size) {
	printf("## Error: size too large, max size <= %lu bytes\n", max_size);
}

static void print_error(const char *msg) {
	printf("## Error: %s\n", msg);
}

static int httpd_findandstore_firstchunk(void) {
	char *start = NULL;
	char *end = NULL;
	if (!boundary_value) {
		return 0;
	}
	start = (char *)strstr((char *)uip_appdata, (char *)boundary_value);
	if (start) {
		end = (char *)strstr((char *)start, "name=\"firmware\"");
		if (end) {
			printf("Upgrade type: firmware\n");
			webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE;
		} else {
			end = (char *)strstr((char *)start, "name=\"uboot\"");
			if (end) {
				webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_UBOOT;
				printf("Upgrade type: U-Boot\n");
			} else {
				end = (char *)strstr((char *)start, "name=\"art\"");
				if (end) {
					printf("Upgrade type: ART\n");
					webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_ART;
				} else {
					end = (char *)strstr((char *)start, "name=\"img\"");
					if (end) {
						printf("Upgrade type: IMG\n");
						webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_IMG;
					} else {
						end = (char *)strstr((char *)start, "name=\"cdt\"");
						if (end) {
							printf("Upgrade type: CDT\n");
							webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_CDT;
						} else {
							end = (char *)strstr((char *)start, "name=\"mibib\"");
							if (end) {
								printf("Upgrade type: MIBIB\n");
								webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_MIBIB;
							} else {
								end = (char *)strstr((char *)start, "name=\"ptable\"");
								if (end) {
									printf("Upgrade type: PTABLE\n");
									webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_PTABLE;
								} else {
									end = (char *)strstr((char *)start, "name=\"initramfs\"");
									if (end) {
										printf("Upgrade type: INITRAMFS\n");
										webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS;
									} else {
										print_error("input name not found!");
										return 0;
									}
								}
							}
						}
					}
				}
			}
		}
		end = NULL;
		end = (char *)strstr((char *)start, eol2);
		if (end) {
			if ((end - (char *)uip_appdata) < uip_len) {
				end += 4;
				hs->upload_total = hs->upload_total - (int)(end - start) - strlen(boundary_value) - 6;
				printf("Upload size: %lu.%02lu MiB [%lu bytes | 0x%lx]\n", (unsigned long)hs->upload_total / (1024 * 1024), ((unsigned long)hs->upload_total % (1024 * 1024)) * 100 / (1024 * 1024), (unsigned long)hs->upload_total, (unsigned long)hs->upload_total);
				if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE) && (hs->upload_total > WEBFAILSAFE_UPLOAD_FIRMWARE_SIZE_IN_BYTES)) {
					print_file_size_error(WEBFAILSAFE_UPLOAD_FIRMWARE_SIZE_IN_BYTES);
					webfailsafe_upload_failed = 1;
					file_too_big = 1;
				} else if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_UBOOT) && (hs->upload_total > WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES)) {
					print_file_size_error(WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES);
						webfailsafe_upload_failed = 1;
					file_too_big = 1;
				} else if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_ART) && (hs->upload_total > WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES)) {
					print_file_size_error(WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES);
						webfailsafe_upload_failed = 1;
					file_too_big = 1;
				} else if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT) && (hs->upload_total > WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES)) {
					print_file_size_error(WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES);
						webfailsafe_upload_failed = 1;
					file_too_big = 1;
				} else if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_MIBIB) && (hs->upload_total > WEBFAILSAFE_UPLOAD_MIBIB_SIZE_IN_BYTES)) {
					print_file_size_error(WEBFAILSAFE_UPLOAD_MIBIB_SIZE_IN_BYTES);
					webfailsafe_upload_failed = 1;
					file_too_big = 1;
				}
				hs->upload = (unsigned int)(uip_len - (end - (char *)uip_appdata));
				if (file_too_big) {
					return 1;
				}
				if (webfailsafe_data_pointer + hs->upload > (u8_t *)upload_ram_end) {
					print_error("data larger than available RAM space!");
					webfailsafe_upload_failed = 1;
					file_too_big = 1;
					return 1;
				}
				printf("Uploading:\n");
				memcpy((void *)webfailsafe_data_pointer, (void *)end, hs->upload);
				webfailsafe_data_pointer += hs->upload;
				httpd_download_progress();
				return 1;
			}
		} else {
			print_error("couldn't find start of data!");
		}
	}
	return 0;
}

#ifdef CONFIG_CMD_SETENV_WEB
/* Url decode function */
static int parse_url_args(char *s, int *argc, char **argv, int max_args) {
	int n = 0;
	char *tok = strtok(s, "&");
	while (tok && n < max_args) {
		argv[n++] = tok;
		tok = strtok(NULL, "&");
	}
	*argc = n;
	return n;
}

/* Send HTTP response */
static void send_http_response(u8_t *buf, int len) {
	hs->state = STATE_ENV_REQUEST;
	hs->senddataptr = buf;
	hs->sendlen = len;
	uip_send(hs->senddataptr, (hs->sendlen > uip_mss() ? uip_mss() : hs->sendlen));
}

/* Handle POST request */
static int handle_post_request(int (*handler)(int, char **, char *, int), char *resp_buf, int bufsize) {
	char *body = strstr((char *)uip_appdata, "\r\n\r\n");
	int resp_len = 0;
	if (body) {
		body += 4;
		int header_len = body - (char *)uip_appdata;
		int body_len = uip_len - header_len;
		char *body_copy = malloc(body_len + 1);
		if (body_copy) {
			memcpy(body_copy, body, body_len);
			body_copy[body_len] = '\0';
			int argc = 0;
			char *argv[10];
			parse_url_args(body_copy, &argc, argv, 10);
			resp_len = handler(argc, argv, resp_buf, bufsize);
			free(body_copy);
		}
	}
	return resp_len;
}
#endif

void httpd_appcall(void) {
	struct fs_file fsfile;
	unsigned int i;
	switch (uip_conn->lport) {
		case HTONS(80):
			hs = (struct httpd_state *)(uip_conn->appstate);
			if (uip_closed()) {
				httpd_state_reset();
				uip_close();
				return;
			}
			if (uip_aborted() || uip_timedout()) {
				httpd_state_reset();
				uip_abort();
				return;
			}
			if (uip_poll()) {
				if (get_timer(hs->last_activity) >= 30000) {
					httpd_state_reset();
					uip_abort();
				}
				return;
			}
			if (uip_connected()) {
				httpd_state_reset();
				return;
			}
			if (uip_newdata() && hs->state == STATE_NONE) {
				hs->last_activity = get_timer(0);
				if (uip_appdata[0] == ISO_G && uip_appdata[1] == ISO_E && uip_appdata[2] == ISO_T && (uip_appdata[3] == ISO_space || uip_appdata[3] == ISO_tab)) {
					if (strncmp((char *)&uip_appdata[4], "/webterm", 8) == 0) {
						webterm_http_handler();
						return;
					}
#ifdef CONFIG_CMD_SETENV_WEB
					/* Handle environment variable API request */
					if (strncmp((char *)&uip_appdata[4], "/setenv", 7) == 0) {
						char *query = strchr((char *)&uip_appdata[4], '?');
						int argc = 0;
						char *argv[10];
						if (query) {
							*query = 0;
							query++;
							parse_url_args(query, &argc, argv, 10);
						}
						char resp_buf[8192];
						int resp_len = web_setenv_handle(argc, argv, resp_buf, sizeof(resp_buf));
						send_http_response((u8_t *)resp_buf, resp_len);
						return;
					}
#endif
					hs->state = STATE_FILE_REQUEST;
				} else if (uip_appdata[0] == ISO_P && uip_appdata[1] == ISO_O && uip_appdata[2] == ISO_S && uip_appdata[3] == ISO_T && (uip_appdata[4] == ISO_space || uip_appdata[4] == ISO_tab)) {
					/* Check for web terminal requests first */
					if (strncmp((char *)&uip_appdata[5], "/webterm", 8) == 0) {
						webterm_http_handler();
						return;
					}
#ifdef CONFIG_CMD_SETENV_WEB
					/* Handle environment variable API request */
					if (strncmp((char *)uip_appdata, "POST /setenv", 12) == 0) {
						char resp_buf[8192];
						int resp_len = handle_post_request(web_setenv_handle, resp_buf, sizeof(resp_buf));
						/* Print response to console */
						printf("[WEB setenv] %.*s\n", resp_len, resp_buf);
						send_http_response((u8_t *)resp_buf, resp_len);
						return;
					}
					/* Handle reset device request */
					if (strncmp((char *)uip_appdata, "POST /reset", 11) == 0) {
					static char resp_buf[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nServer: uIP\r\nConnection: close\r\n\r\nRebooting...\n";
					send_http_response((u8_t *)resp_buf, strlen(resp_buf));
					do_reset(NULL, 0, 0, NULL);
					return;
					}
#endif
					if (hs->state == STATE_NONE) {
						char *start = NULL;
						char *end = NULL;
						uip_appdata[uip_len] = '\0';
						start = (char *)strstr((char*)uip_appdata, "Content-Length:");
						if (start) {
							start += sizeof("Content-Length:");
							end = (char *)strstr(start, eol);
							if (end) {
								hs->upload_total = atoi(start);
							} else {
								print_error("couldn't find \"Content-Length\"!");
								httpd_state_reset();
								uip_abort();
								return;
							}
						} else {
							print_error("couldn't find \"Content-Length\"!");
							httpd_state_reset();
							uip_abort();
							return;
						}
						if (hs->upload_total < 10240) {
							/* Allow webterm command requests which are typically small */
							if (strncmp((char *)&uip_appdata[5], "/webterm/cmd", 12) != 0) {
								print_error("request for upload < 10 KB data!");
								httpd_state_reset();
								uip_abort();
								return;
							}
						}
					}
					hs->state = STATE_UPLOAD_REQUEST;
					led_off("blink_led");
				}
				if (hs->state == STATE_NONE) {
					httpd_state_reset();
					uip_abort();
					return;
				}
				if (hs->state == STATE_FILE_REQUEST) {
					for (i = 4; i < 30; i++) {
						if (uip_appdata[i] == ISO_space || uip_appdata[i] == ISO_cr || uip_appdata[i] == ISO_nl || uip_appdata[i] == ISO_tab) {
							uip_appdata[i] = 0;
							i = 0;
							break;
						}
					}
					if (i != 0) {
						print_error("request file name too long!");
						httpd_state_reset();
						uip_abort();
						return;
					}
					printf("Request for: ");
					printf("%s\n", &uip_appdata[4]);
					if (uip_appdata[4] == ISO_slash && uip_appdata[5] == 0) {
						fs_open(file_index_html.name, &fsfile);
					} else {
						if (!fs_open((const char *)&uip_appdata[4], &fsfile)) {
							print_error("file not found!");
							fs_open(file_404_html.name, &fsfile);
						}
					}
					hs->state = STATE_FILE_REQUEST;
					hs->dataptr = (u8_t *)fsfile.data;
					hs->upload = fsfile.len;
					uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
					return;
				} else if (hs->state == STATE_UPLOAD_REQUEST) {
					char *start = NULL;
					char *end = NULL;
					uip_appdata[uip_len] = '\0';
					start = (char *)strstr((char*)uip_appdata, "Content-Length:");
					if (start) {
						start += sizeof("Content-Length:");
						end = (char *)strstr(start, eol);
						if (end) {
							hs->upload_total = atoi(start);
						} else {
							print_error("couldn't find \"Content-Length\"!");
							httpd_state_reset();
							uip_abort();
							return;
						}
					} else {
						print_error("couldn't find \"Content-Length\"!");
						httpd_state_reset();
						uip_abort();
						return;
					}
					if (hs->upload_total < 10240) {
						print_error("request for upload < 10 KB data!");
						httpd_state_reset();
						uip_abort();
						return;
					}
					start = NULL;
					end = NULL;
					start = (char *)strstr((char *)uip_appdata, "boundary=");
					if (start) {
						start += 9;
						end = (char *)strstr((char *)start, eol);
						if (end) {
							boundary_value = (char*)malloc(end - start + 3);
							if (boundary_value) {
								memcpy(&boundary_value[2], start, end - start);
								boundary_value[0] = '-';
								boundary_value[1] = '-';
								boundary_value[end - start + 2] = 0;
							} else {
								print_error("couldn't allocate memory for boundary!");
								httpd_state_reset();
								uip_abort();
								return;
							}
						} else {
							print_error("couldn't find boundary!");
							httpd_state_reset();
							uip_abort();
							return;
						}
					} else {
						print_error("couldn't find boundary!");
						httpd_state_reset();
						uip_abort();
						return;
					}
					webfailsafe_data_pointer = (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
					upload_ram_end = CONFIG_SYS_SDRAM_END;
					if (!webfailsafe_data_pointer) {
						print_error("couldn't allocate RAM for data!");
						httpd_state_reset();
						uip_abort();
						return;
					} else {
						printf("Upload RAM address: 0x%lx\n", WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
						printf("Available RAM space: %lu.%02lu MiB\n", (upload_ram_end - (unsigned long)webfailsafe_data_pointer) / (1024 * 1024), ((upload_ram_end - (unsigned long)webfailsafe_data_pointer) % (1024 * 1024)) * 100 / (1024 * 1024));
					}
					{
						unsigned long memset_len = WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES;
						if (webfailsafe_data_pointer + memset_len > (u8_t *)upload_ram_end)
							memset_len = upload_ram_end - (unsigned long)webfailsafe_data_pointer;
						if (memset_len > 0)
							memset((void *)webfailsafe_data_pointer, 0xFF, memset_len);
					}
					if (httpd_findandstore_firstchunk()) {
						data_start_found = 1;
					} else {
						data_start_found = 0;
					}
					return;
				}
			}
#ifdef CONFIG_CMD_SETENV_WEB
			/* Handle environment variable request state */
			if (hs->state == STATE_ENV_REQUEST) {
				if (uip_acked()) {
					if (hs->sendlen > uip_mss()) {
						hs->senddataptr += uip_conn->len;
						hs->sendlen -= uip_conn->len;
						uip_send(hs->senddataptr, (hs->sendlen > uip_mss() ? uip_mss() : hs->sendlen));
					} else {
						httpd_state_reset();
						uip_close();
					}
				}
				if (uip_rexmit()) {
					uip_send(hs->senddataptr, (hs->sendlen > uip_mss() ? uip_mss() : hs->sendlen));
				}
				return;
			}
#endif
			if (uip_acked()) {
				if (hs->state == STATE_FILE_REQUEST) {
					if (hs->upload <= uip_mss()) {
						if (webfailsafe_post_done) {
							if (!webfailsafe_upload_failed) {
								webfailsafe_ready_for_upgrade = 1;
							}
							webfailsafe_post_done = 0;
							webfailsafe_upload_failed = 0;
						}
						httpd_state_reset();
						uip_close();
						return;
					}
					hs->dataptr += uip_conn->len;
					hs->upload -= uip_conn->len;
					uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
				}
				return;
			}
			if (uip_rexmit()) {
				if (hs->state == STATE_FILE_REQUEST) {
					uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
				}
				return;
			}
			if (uip_newdata()) {
				hs->last_activity = get_timer(0);
				if (hs->state == STATE_UPLOAD_REQUEST) {
					uip_appdata[uip_len] = '\0';
					if (!data_start_found) {
						if (!httpd_findandstore_firstchunk()) {
							print_error("couldn't find start of data in next packet!");
							httpd_state_reset();
							uip_abort();
							return;
						} else {
							data_start_found = 1;
						}
						return;
					}
					hs->upload += (unsigned int)uip_len;
					if (!webfailsafe_upload_failed) {
						unsigned long bytes_to_write = uip_len;
						unsigned long data_written = webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
						if (data_written + bytes_to_write > hs->upload_total)
							bytes_to_write = hs->upload_total - data_written;
						if (bytes_to_write > 0 && webfailsafe_data_pointer + bytes_to_write > (u8_t *)upload_ram_end) {
							print_error("data larger than available RAM space!");
							webfailsafe_upload_failed = 1;
							file_too_big = 1;
						} else if (bytes_to_write > 0) {
							memcpy((void *)webfailsafe_data_pointer, (void *)uip_appdata, bytes_to_write);
							webfailsafe_data_pointer += bytes_to_write;
						}
						httpd_download_progress();
					}
					if (hs->upload >= hs->upload_total + strlen(boundary_value) + 6) {
						if (webfailsafe_upload_failed) {
							printf("\nfailed!\n");
						}
						led_on("blink_led");
						webfailsafe_post_done = 1;
						net_boot_file_size = (ulong)hs->upload_total;
						if (!webfailsafe_upload_failed) {
							fs_open(file_flashing_html.name, &fsfile);
						} else {
							fs_open(file_fail_html.name, &fsfile);
						}
						httpd_state_reset();
						hs->state = STATE_FILE_REQUEST;
						hs->dataptr = (u8_t *)fsfile.data;
						hs->upload = fsfile.len;
						uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
					}
				}
				return;
			}
			break;
		default:
			uip_abort();
			break;
	}
}

void httpd_poll(void) {
	static int httpd_progress_start_done = 0;
	static int eth_init_attempted = 0;
	static ulong arptimer = 0;
	ulong now = get_timer(0);
	int i;
#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	int link_changed = 0;
#endif

	if (!webfailsafe_is_running)
		return;

	/* Check if upgrade is ready - this was handled in net_loop originally */
	if (webfailsafe_ready_for_upgrade) {
		/* Clear flag immediately to prevent re-entry */
		webfailsafe_ready_for_upgrade = 0;
		setenv_hex("filesize", net_boot_file_size);
		setenv_hex("filesize_128k", (net_boot_file_size/131072+(net_boot_file_size%131072!=0))*131072);
		setenv_hex("fileaddr", load_addr);
		do_http_progress(WEBFAILSAFE_PROGRESS_UPLOAD_READY);
		if (do_http_upgrade(net_boot_file_size, webfailsafe_upgrade_type) < 0) {
			do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_FAILED);
			return;
		}
		/* Upgrade successful */
		HttpdDone();
		do_reset(NULL, 0, 0, NULL);
		/* Shouldn't reach here */
		printf("reboot fail\n");
		return;
	}

	if (webterm_run_pending_command()) {
		if (!eth_is_active(eth_get_dev()))
			eth_init_attempted = 0;
	}

#if defined(CONFIG_IPQ807x)
	ipq807x_eth_check_link_change();
#elif defined(CONFIG_IPQ40XX)
	ipq40xx_eth_check_link_change();
#elif defined(CONFIG_IPQ6018)
	ipq6018_eth_check_link_change();
#elif defined(CONFIG_IPQ9574)
	link_changed = ipq9574_eth_check_link_change();
#elif defined(CONFIG_IPQ5332)
	link_changed = ipq5332_eth_check_link_change();
#elif defined(CONFIG_IPQ5018)
	ipq5018_eth_check_link_change();
#elif defined(CONFIG_IPQ806x)
	ipq806x_eth_check_link_change();
#endif

	if (!eth_is_active(eth_get_dev())) {
		if (!eth_init_attempted) {
			eth_init_attempted = 1;
			eth_halt();
			eth_set_current();
			eth_init();
#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
			ppe_arp_kickstart();
#endif
			if (!httpd_progress_start_done) {
				do_http_progress(WEBFAILSAFE_PROGRESS_START);
				httpd_progress_start_done = 1;
			}
		}
	}
#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	else if (link_changed > 0) {
		ppe_arp_kickstart();
	}
#endif

	if (eth_rx() > 0) {
		HttpdHandler();
#ifdef CONFIG_DHCPD
		dhcpd_poll_server();
#endif
	}

	for (i = 0; i < UIP_CONNS; i++) {
		uip_periodic(i);
		if (uip_len > 0) {
			uip_arp_out();
			NetSendHttpd();
		}
	}
	if (get_timer(arptimer) >= 1000) {
		uip_arp_timer();
		arptimer = now;
	}
}

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
void ppe_arp_kickstart(void) {
	uchar arp_packet[60];
	memset(arp_packet, 0, sizeof(arp_packet));

	memset(arp_packet, 0xff, 6);
	memcpy(arp_packet + 6, uip_ethaddr.addr, 6);
	*(u16_t *)(arp_packet + 12) = htons(0x0806);

	*(u16_t *)(arp_packet + 14) = htons(1);
	*(u16_t *)(arp_packet + 16) = htons(0x0800);
	*(arp_packet + 18) = 6;
	*(arp_packet + 19) = 4;
	*(u16_t *)(arp_packet + 20) = htons(1);

	memcpy(arp_packet + 22, uip_ethaddr.addr, 6);
	*(u16_t *)(arp_packet + 28) = uip_hostaddr[0];
	*(u16_t *)(arp_packet + 30) = uip_hostaddr[1];

	memset(arp_packet + 32, 0, 6);
	*(u16_t *)(arp_packet + 38) = htons(0xC0A8);
	*(u16_t *)(arp_packet + 40) = htons(0x01FE);

	net_send_packet(arp_packet, sizeof(arp_packet));
	mdelay(500);
}
#endif

void httpd_stop(void) {
	webfailsafe_is_running = 0;
}

int httpd_is_running(void) {
	return webfailsafe_is_running;
}