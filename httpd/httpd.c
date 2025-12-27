#include "uip.h"
#include "httpd.h"
#include "fs.h"
#include "fsdata.h"
#include <asm/gpio.h>
#include <ipq_api.h>

#define STATE_NONE					0
#define STATE_FILE_REQUEST			1
#define STATE_UPLOAD_REQUEST		2

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

static int atoi(const char *s) {
	int i = 0;
	while (is_digit(*s)) {
		i = i * 10 + *(s++) - '0';
	}
	return i;
}

static void httpd_download_progress(void) {
	if (post_packet_counter == 39) {
		puts("\n         ");
		post_packet_counter = 0;
		post_line_counter++;
	}
	if (post_line_counter == 10) {
		post_line_counter = 0;
		led_toggle("blink_led");
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
	hs->count = 0;
	hs->dataptr = 0;
	hs->upload = 0;
	hs->upload_total = 0;
	data_start_found = 0;
	post_packet_counter = 0;
	led_on("blink_led");
	if (boundary_value) {
		free(boundary_value);
		boundary_value = NULL;
	}
}

static int httpd_findandstore_firstchunk(void) {
	char *start = NULL;
	char *end = NULL;
	int art_size = 0;
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
							printf("## Error: input name not found!\n");
							return 0;
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
				printf("Upload file size: %d bytes\n", hs->upload_total);
				if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_UBOOT) && (hs->upload_total > WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES)) {
					printf("## Error: wrong file size, should be less than or equal to: %lu bytes!\n", WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES);
					webfailsafe_upload_failed = 1;
					file_too_big = 1;
				} else if (webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_ART) {
					if (strcmp(getenv("machid"), "8030202") == 0) {
						art_size = WEBFAILSAFE_UPLOAD_ART_BIG_SIZE_IN_BYTES;
					} else {
						art_size = WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES;
					}
					if (hs->upload_total > art_size) {
						printf("## Error: wrong file size, should be less than or equal to: %lu bytes!\n", (unsigned long)art_size);
						webfailsafe_upload_failed = 1;
						file_too_big = 1;
					}
				} else if ((webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT) && (hs->upload_total > WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES)) {
					printf("## Error: wrong file size, should be less than or equal to: %lu bytes!\n", WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES);
					webfailsafe_upload_failed = 1;
					file_too_big = 1;
				}
				printf("Loading: ");
				hs->upload = (unsigned int)(uip_len - (end - (char *)uip_appdata));
				memcpy((void *)webfailsafe_data_pointer, (void *)end, hs->upload);
				webfailsafe_data_pointer += hs->upload;
				httpd_download_progress();
				return 1;
			}
		} else {
			printf("## Error: couldn't find start of data!\n");
		}
	}
	return 0;
}

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
				if (hs->count++ >= 10000) {
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
				if (uip_appdata[0] == ISO_G && uip_appdata[1] == ISO_E && uip_appdata[2] == ISO_T && (uip_appdata[3] == ISO_space || uip_appdata[3] == ISO_tab)) {
					hs->state = STATE_FILE_REQUEST;
				} else if (uip_appdata[0] == ISO_P && uip_appdata[1] == ISO_O && uip_appdata[2] == ISO_S && uip_appdata[3] == ISO_T && (uip_appdata[4] == ISO_space || uip_appdata[4] == ISO_tab)) {
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
						printf("## Error: request file name too long!\n");
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
							printf("## Error: file not found!\n");
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
							printf("## Error: couldn't find \"Content-Length\"!\n");
							httpd_state_reset();
							uip_abort();
							return;
						}
					} else {
						printf("## Error: couldn't find \"Content-Length\"!\n");
						httpd_state_reset();
						uip_abort();
						return;
					}
					if (hs->upload_total < 10240) {
						printf("## Error: request for upload < 10 KB data!\n");
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
								printf("## Error: couldn't allocate memory for boundary!\n");
								httpd_state_reset();
								uip_abort();
								return;
							}
						} else {
							printf("## Error: couldn't find boundary!\n");
							httpd_state_reset();
							uip_abort();
							return;
						}
					} else {
						printf("## Error: couldn't find boundary!\n");
						httpd_state_reset();
						uip_abort();
						return;
					}
					webfailsafe_data_pointer = (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
					if (!webfailsafe_data_pointer) {
						printf("## Error: couldn't allocate RAM for data!\n");
						httpd_state_reset();
						uip_abort();
						return;
					} else {
						printf("Data will be downloaded at 0x%lX in RAM\n", WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
					}
					memset((void *)webfailsafe_data_pointer, 0xFF, WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES);
					if (httpd_findandstore_firstchunk()) {
						data_start_found = 1;
					} else {
						data_start_found = 0;
					}
					return;
				}
			}
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
				if (hs->state == STATE_UPLOAD_REQUEST) {
					uip_appdata[uip_len] = '\0';
					if (!data_start_found) {
						if (!httpd_findandstore_firstchunk()) {
							printf("## Error: couldn't find start of data in next packet!\n");
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
						memcpy((void *)webfailsafe_data_pointer, (void *)uip_appdata, uip_len);
						webfailsafe_data_pointer += uip_len;
					}
					httpd_download_progress();
					if (hs->upload >= hs->upload_total + strlen(boundary_value) + 6) {
						printf("\n\ndone!\n");
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