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

#define STATE_NONE					0
#define STATE_FILE_REQUEST			1
#define STATE_UPLOAD_REQUEST		2
#define WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES	184

#define ISO_slash	0x2f
#define ISO_space	0x20
#define ISO_nl		0x0a
#define ISO_cr		0x0d
#define ISO_tab		0x09

#define is_digit(c) ((c) >= '0' && (c) <= '9')
#define is_http_whitespace(c) ((c) == ISO_space || (c) == ISO_cr || (c) == ISO_nl || (c) == ISO_tab)
#define is_http_method_separator(c) ((c) == ISO_space || (c) == ISO_tab)

extern const struct fsdata_file file_index_html;
extern const struct fsdata_file file_404_html;

extern int webfailsafe_ready_for_upgrade;
extern int webfailsafe_upgrade_type;
extern u32 net_boot_file_size;
extern unsigned char *webfailsafe_data_pointer;

struct httpd_state *hs;

int webfailsafe_post_done = 0;
int file_too_big = 0;
static int webfailsafe_upload_failed = 0;
static int data_start_found = 0;
int upgrade_status = 0;

static unsigned char post_packet_counter = 0;
static unsigned char post_line_counter = 0;

static char eol[3] = { 0x0d, 0x0a, 0x00 };
static char eol2[5] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };

static char *boundary_value;
static unsigned long upload_ram_end;

static void httpd_poll_wait(int count) {
	int i;
	for (i = 0; i < count; i++) {
		mdelay(100);
		if (eth_rx() > 0)
			HttpdHandler();
	}
}

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

static void httpd_upload_complete(void) {
	if (webfailsafe_upload_failed) {
		printf("\nfailed!\n");
	}
	led_on("blink_led");
	webfailsafe_post_done = 1;
	upgrade_status = 0;
	net_boot_file_size = (ulong)hs->upload_total;
	static const char resp_ok[] = "HTTP/1.0 200 OK\r\nServer: uIP/0.9\r\nConnection: close\r\n\r\n";
	static const char resp_err[] = "HTTP/1.0 500 Internal Server Error\r\nServer: uIP/0.9\r\nConnection: close\r\n\r\n";
	httpd_state_reset();
	hs->state = STATE_FILE_REQUEST;
	if (!webfailsafe_upload_failed) {
		hs->dataptr = (u8_t *)resp_ok;
		hs->upload = sizeof(resp_ok) - 1;
	} else {
		hs->dataptr = (u8_t *)resp_err;
		hs->upload = sizeof(resp_err) - 1;
	}
	uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
}

typedef unsigned long (*get_max_size_fn)(void);

static const struct {
	const char *name;
	int type;
	const char *label;
	get_max_size_fn get_max_size;
} upload_types[] = {
	{"name=\"firmware\"",	WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE,	"firmware",	get_firmware_upgrade_max_size},
	{"name=\"uboot\"",		WEBFAILSAFE_UPGRADE_TYPE_UBOOT,		"U-Boot",	get_uboot_size},
	{"name=\"art\"",		WEBFAILSAFE_UPGRADE_TYPE_ART,		"ART",		get_art_size},
	{"name=\"img\"",		WEBFAILSAFE_UPGRADE_TYPE_IMG,		"IMG",		NULL},
	{"name=\"cdt\"",		WEBFAILSAFE_UPGRADE_TYPE_CDT,		"CDT",		get_cdt_size},
	{"name=\"mibib\"",		WEBFAILSAFE_UPGRADE_TYPE_MIBIB,		"MIBIB",	get_mibib_size},
	{"name=\"ptable\"",		WEBFAILSAFE_UPGRADE_TYPE_PTABLE,	"PTABLE",	NULL},
	{"name=\"initramfs\"",	WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS,	"INITRAMFS",NULL},
};

static int httpd_findandstore_firstchunk(void) {
	char *start = NULL;
	char *end = NULL;
	unsigned int i;
	if (!boundary_value) {
		return 0;
	}
	start = (char *)strstr((char *)uip_appdata, (char *)boundary_value);
	if (!start) {
		return 0;
	}
	for (i = 0; i < ARRAY_SIZE(upload_types); i++) {
		if (strstr((char *)start, upload_types[i].name)) {
			printf("Upgrade type: %s\n", upload_types[i].label);
			webfailsafe_upgrade_type = upload_types[i].type;
			break;
		}
	}
	if (i == ARRAY_SIZE(upload_types)) {
		print_error("input name not found!");
		return 0;
	}
	end = (char *)strstr((char *)start, eol2);
	if (!end) {
		print_error("couldn't find start of data!");
		return 0;
	}
	if ((end - (char *)uip_appdata) >= uip_len) {
		return 0;
	}
	end += 4;
	hs->upload_total = hs->upload_total - (int)(end - start) - strlen(boundary_value) - 6;
	printf("Upload size: %lu.%02lu MiB [%lu bytes | 0x%lx]\n", (unsigned long)hs->upload_total / (1024 * 1024), ((unsigned long)hs->upload_total % (1024 * 1024)) * 100 / (1024 * 1024), (unsigned long)hs->upload_total, (unsigned long)hs->upload_total);
	if (upload_types[i].get_max_size) {
		unsigned long max_size = upload_types[i].get_max_size();
		if (hs->upload_total > max_size) {
			print_file_size_error(max_size);
			webfailsafe_upload_failed = 1;
			file_too_big = 1;
		}
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

static int httpd_parse_content_length(void) {
	char *start = (char *)strstr((char *)uip_appdata, "Content-Length:");
	char *end;
	if (start) {
		start += sizeof("Content-Length:");
		end = (char *)strstr(start, eol);
		if (end) {
			hs->upload_total = atoi(start);
			return 0;
		}
	}
	print_error("couldn't find \"Content-Length\"!");
	return -1;
}

static int httpd_parse_boundary(void) {
	char *start = (char *)strstr((char *)uip_appdata, "boundary=");
	char *end;
	if (start) {
		start += 9;
		end = (char *)strstr((char *)start, eol);
		if (end) {
			boundary_value = (char *)malloc(end - start + 3);
			if (boundary_value) {
				memcpy(&boundary_value[2], start, end - start);
				boundary_value[0] = '-';
				boundary_value[1] = '-';
				boundary_value[end - start + 2] = 0;
				return 0;
			}
			print_error("couldn't allocate memory for boundary!");
			return -1;
		}
	}
	print_error("couldn't find boundary!");
	return -1;
}

static int httpd_init_upload_ram(void) {
	unsigned long memset_len;
	webfailsafe_data_pointer = (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
	upload_ram_end = CONFIG_SYS_SDRAM_END;
	if (!webfailsafe_data_pointer) {
		print_error("couldn't allocate RAM for data!");
		return -1;
	}
	printf("Upload RAM address: 0x%lx\n", WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
	printf("Available RAM space: %lu.%02lu MiB\n", (upload_ram_end - (unsigned long)webfailsafe_data_pointer) / (1024 * 1024), ((upload_ram_end - (unsigned long)webfailsafe_data_pointer) % (1024 * 1024)) * 100 / (1024 * 1024));
	memset_len = WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES;
	if (webfailsafe_data_pointer + memset_len > (u8_t *)upload_ram_end)
		memset_len = upload_ram_end - (unsigned long)webfailsafe_data_pointer;
	if (memset_len > 0)
		memset((void *)webfailsafe_data_pointer, 0xFF, memset_len);
	return 0;
}

static int httpd_check_upload_size(void) {
	if (hs->upload_total < 10240 && webfailsafe_upgrade_type != WEBFAILSAFE_UPGRADE_TYPE_CDT) {
		print_error("request for upload < 10 KB data!");
		return -1;
	}
	if (webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT && hs->upload_total < WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES) {
		printf("## Error: CDT data too small, minimum %d bytes!\n", WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES);
		return -1;
	}
	return 0;
}

static int httpd_check_upload_complete(void) {
	if (hs->upload >= hs->upload_total + strlen(boundary_value) + 6) {
		httpd_upload_complete();
		return 1;
	}
	return 0;
}

static void httpd_handle_upgrade_status(void) {
	static const char *status_text[] = {"idle", "verifying", "flashing", "type_mismatch", "rebooting"};
	char resp[128];
	int len = sprintf(resp, "HTTP/1.0 200 OK\r\nServer: uIP/0.9\r\nCache-Control: no-cache\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s", status_text[upgrade_status]);
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)resp;
	hs->upload = len;
	uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
}

static void httpd_handle_file_request(void) {
	struct fs_file fsfile;
	unsigned int i;
	for (i = 4; i < 30; i++) {
		if (is_http_whitespace(uip_appdata[i])) {
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
}

static void httpd_handle_upload_data(void) {
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

static void httpd_handle_initial_request(void) {
	hs->last_activity = get_timer(0);
	if (strncmp((char *)uip_appdata, "GET", 3) == 0 && is_http_method_separator(uip_appdata[3])) {
		if (strncmp((char *)&uip_appdata[4], "/webterm", 8) == 0) {
			webterm_http_handler();
			return;
		}
		if (strncmp((char *)&uip_appdata[4], "/upgrade_status", 15) == 0) {
			httpd_handle_upgrade_status();
			return;
		}
		hs->state = STATE_FILE_REQUEST;
		httpd_handle_file_request();
		return;
	}
	if (strncmp((char *)uip_appdata, "POST", 4) == 0 && is_http_method_separator(uip_appdata[4])) {
		if (strncmp((char *)&uip_appdata[5], "/webterm", 8) == 0) {
			webterm_http_handler();
			return;
		}
		uip_appdata[uip_len] = '\0';
		if (httpd_parse_content_length() < 0) {
			httpd_state_reset();
			uip_abort();
			return;
		}
		hs->state = STATE_UPLOAD_REQUEST;
		led_off("blink_led");
		if (httpd_parse_boundary() < 0 || httpd_init_upload_ram() < 0) {
			httpd_state_reset();
			uip_abort();
			return;
		}
		if (httpd_findandstore_firstchunk()) {
			data_start_found = 1;
			if (httpd_check_upload_size() < 0) {
				httpd_state_reset();
				uip_abort();
				return;
			}
			if (httpd_check_upload_complete())
				return;
		} else {
			data_start_found = 0;
		}
		return;
	}
	httpd_state_reset();
	uip_abort();
}

static void httpd_handle_file_acked(void) {
	if (hs->upload <= uip_mss()) {
		if (webfailsafe_post_done) {
			if (!webfailsafe_upload_failed)
				webfailsafe_ready_for_upgrade = 1;
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

static void httpd_handle_upload_packet(void) {
	hs->last_activity = get_timer(0);
	uip_appdata[uip_len] = '\0';
	if (!data_start_found) {
		if (!httpd_findandstore_firstchunk()) {
			print_error("couldn't find start of data in next packet!");
			httpd_state_reset();
			uip_abort();
			return;
		}
		data_start_found = 1;
		if (httpd_check_upload_size() < 0) {
			httpd_state_reset();
			uip_abort();
			return;
		}
		if (httpd_check_upload_complete())
			return;
		return;
	}
	hs->upload += (unsigned int)uip_len;
	if (!webfailsafe_upload_failed)
		httpd_handle_upload_data();
	if (httpd_check_upload_complete())
		return;
}

void httpd_appcall(void) {
	if (uip_conn->lport != HTONS(80)) {
		uip_abort();
		return;
	}
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
	switch (hs->state) {
	case STATE_NONE:
		if (uip_newdata())
			httpd_handle_initial_request();
		break;
	case STATE_FILE_REQUEST:
		if (uip_acked())
			httpd_handle_file_acked();
		else if (uip_rexmit())
			uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
		break;
	case STATE_UPLOAD_REQUEST:
		if (uip_newdata())
			httpd_handle_upload_packet();
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
		webfailsafe_ready_for_upgrade = 0;
		upgrade_status = 1;
		setenv_hex("filesize", net_boot_file_size);
		setenv_hex("filesize_128k", (net_boot_file_size/131072+(net_boot_file_size%131072!=0))*131072);
		setenv_hex("fileaddr", load_addr);
		do_http_progress(WEBFAILSAFE_PROGRESS_UPLOAD_READY);

		httpd_poll_wait(20);

		upgrade_status = 2;
		httpd_poll_wait(20);

		if (do_http_upgrade(net_boot_file_size, webfailsafe_upgrade_type) < 0) {
			do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_FAILED);
			upgrade_status = 3;
			httpd_poll_wait(20);
			return;
		}
		upgrade_status = 4;

		httpd_poll_wait(35);
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

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	link_changed = eth_check_link_change();
#else
	eth_check_link_change();
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
struct ppe_arp_hdr {
	struct uip_eth_hdr ethhdr;
	u16_t hwtype;
	u16_t protocol;
	u8_t hwlen;
	u8_t protolen;
	u16_t opcode;
	struct uip_eth_addr shwaddr;
	u16_t sipaddr[2];
	struct uip_eth_addr dhwaddr;
	u16_t dipaddr[2];
};

void ppe_arp_kickstart(void) {
	uchar pkt[60];
	struct ppe_arp_hdr *arp = (struct ppe_arp_hdr *)pkt;

	memset(pkt, 0, sizeof(pkt));
	memset(arp->ethhdr.dest.addr, 0xff, 6);
	arp->ethhdr.src = uip_ethaddr;
	arp->ethhdr.type = htons(UIP_ETHTYPE_ARP);

	arp->hwtype = htons(1);
	arp->protocol = htons(UIP_ETHTYPE_IP);
	arp->hwlen = 6;
	arp->protolen = 4;
	arp->opcode = htons(1);

	arp->shwaddr = uip_ethaddr;
	arp->sipaddr[0] = uip_hostaddr[0];
	arp->sipaddr[1] = uip_hostaddr[1];

	arp->dipaddr[0] = uip_hostaddr[0];
	arp->dipaddr[1] = (uip_hostaddr[1] & htons(0xFF00)) | htons(0x00FE);

	net_send_packet(pkt, sizeof(pkt));
}
#endif

void httpd_stop(void) {
	webfailsafe_is_running = 0;
}

int httpd_is_running(void) {
	return webfailsafe_is_running;
}