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
#include <asm/arch-qca-common/smem.h>
#include <command.h>
#ifdef CONFIG_CMD_NAND
#undef LED_OFF
#include <nand.h>
#endif
extern unsigned int get_spi_flash_size(void);
#ifdef CONFIG_QCA_MMC
#include <mmc.h>
#include <sdhci.h>
#include <part.h>
#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif
#endif

extern int flashread_partition(const char *part_name, ulong addr,
					 ulong user_size, int raw, ulong *out_offset,
					 ulong *out_size);

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
extern int webfailsafe_img_flash;
extern u32 net_boot_file_size;
extern unsigned char *webfailsafe_data_pointer;

struct httpd_state *hs;

int webfailsafe_post_done = 0;
int file_too_big = 0;
static int webfailsafe_upload_failed = 0;
static int data_start_found = 0;
int upgrade_status = 0;

static unsigned char post_packet_counter = 255;
static unsigned short post_led_counter = 0;
static unsigned long upload_start_time = 0;

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

static void httpd_upload_progress(void) {
	enum { bar_width = 25 };
	unsigned long data_written, elapsed, speed;
	unsigned int percent, filled, i;
	char bar[bar_width + 1];

	if (hs->upload_total == 0)
		return;

	data_written = webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;

	percent = (unsigned int)((unsigned long long)data_written * 100 / hs->upload_total);
	if (percent > 100)
		percent = 100;

	if (percent / 25 != post_packet_counter / 25) {
		filled = (percent * bar_width) / 100;
		for (i = 0; i < bar_width; i++)
			bar[i] = (i < filled) ? '#' : '.';
		bar[bar_width] = '\0';

		elapsed = get_timer(upload_start_time);
		speed = (elapsed > 0) ? (unsigned long)((unsigned long long)data_written * 1000 / elapsed) : 0;
		printf("\rUploading: [%s] %3u%% %lu.%02lu MiB/s", bar, percent, speed / (1024 * 1024), (speed % (1024 * 1024)) * 100 / (1024 * 1024));
		post_packet_counter = (unsigned char)percent;
	}

	post_led_counter++;
	if (post_led_counter >= 10000) {
		post_led_counter = 0;
		do_http_progress(WEBFAILSAFE_PROGRESS_UPLOADING);
	}
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
	post_packet_counter = 255;
	post_led_counter = 0;
	upload_start_time = 0;
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
	printf("\n## Error: %s\n", msg);
}

static void httpd_upload_complete(void) {
	if (webfailsafe_upload_failed) {
		printf("\nfailed!\n");
	} else {
		printf("  Done!\n");
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
			if (upload_types[i].type == WEBFAILSAFE_UPGRADE_TYPE_IMG) {
				webfailsafe_img_flash = strstr((char *)start, "img_nand_raw") ? IMG_FLASH_NAND_RAW :
				strstr((char *)start, "img_nand") ? IMG_FLASH_NAND :
				strstr((char *)start, "img_emmc") ? IMG_FLASH_EMMC :
				strstr((char *)start, "img_nor") ? IMG_FLASH_NOR : 0;
			}
			break;
		}
	}
	if (i == ARRAY_SIZE(upload_types)) {
		print_error("input name not found!");
		return 0;
	}
	end = (char *)strstr((char *)strstr((char *)start, upload_types[i].name), eol2);
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
	memcpy((void *)webfailsafe_data_pointer, (void *)end, hs->upload);
	webfailsafe_data_pointer += hs->upload;
	upload_start_time = get_timer(0);
	httpd_upload_progress();
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

#define PART_JSON_BUF_SIZE 2048
static char part_json_buf[PART_JSON_BUF_SIZE];
static ulong backup_data_addr;
static unsigned long backup_data_size;
static int backup_sending_header;

static void httpd_handle_partitions(void) {
	int i, pos = 0, hdr_len, count = 0;
	char name[SMEM_PTN_NAME_MAX], hdr[128];
	uint32_t start, size;
	uint32_t flash_type, flash_index, flash_cs, bsize, flash_density;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
#ifdef CONFIG_QCA_MMC
	int gpt_count;
	block_dev_desc_t *blk_dev = NULL;
	disk_partition_t disk_info;
#endif

	smem_get_boot_flash(&flash_type, &flash_index, &flash_cs, &bsize, &flash_density);

	pos += sprintf(part_json_buf + pos, "{\"parts\":[");

#ifdef CONFIG_QCA_MMC
	if (flash_type == SMEM_BOOT_MMC_FLASH || flash_type == SMEM_BOOT_NO_FLASH) {
		blk_dev = mmc_get_dev(mmc_host.dev_num);
		if (blk_dev != NULL) {
			gpt_count = get_partition_count_efi(blk_dev);
			for (i = 1; i <= gpt_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
				if (get_partition_info_efi(blk_dev, i, &disk_info) == 0) {
					pos += sprintf(part_json_buf + pos,
						"%s{\"name\":\"%s\",\"start\":%lu,\"size\":%lu,\"flash\":\"emmc\"}",
						(count > 0 ? "," : ""), disk_info.name,
						(unsigned long)disk_info.start,
						(unsigned long)disk_info.size);
					count++;
				}
			}
		}
	} else
#endif
	{
		int smem_count = smem_getpart_count();
		for (i = 0; i < smem_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
			if (smem_getpart_by_index(i, name, sizeof(name), &start, &size) == 0) {
				pos += sprintf(part_json_buf + pos,
					"%s{\"name\":\"%s\",\"start\":%lu,\"size\":%lu,\"flash\":\"nor\"}",
					(count > 0 ? "," : ""), name,
					(unsigned long)start * (unsigned long)bsize,
					(unsigned long)size);
				count++;
			}
		}
#ifdef CONFIG_QCA_MMC
		if ((sfi->flash_type == SMEM_BOOT_SPI_FLASH || flash_type == SMEM_BOOT_NORPLUSEMMC) &&
			(sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH ||
			 sfi->rootfs.offset == 0xBAD0FF5E || flash_type == SMEM_BOOT_NORPLUSEMMC)) {
			blk_dev = mmc_get_dev(mmc_host.dev_num);
			if (blk_dev != NULL) {
				gpt_count = get_partition_count_efi(blk_dev);
				for (i = 1; i <= gpt_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
					if (get_partition_info_efi(blk_dev, i, &disk_info) == 0) {
						pos += sprintf(part_json_buf + pos,
						"%s{\"name\":\"%s\",\"start\":%lu,\"size\":%lu,\"flash\":\"emmc\"}",
						(count > 0 ? "," : ""), disk_info.name,
						(unsigned long)disk_info.start,
						(unsigned long)disk_info.size);
					count++;
					}
				}
			}
		}
#endif
	}

	pos += sprintf(part_json_buf + pos, "],\"has_spi\":%s,\"spi_size\":%lu,\"has_nand\":%s,\"nand_size\":%lu,\"nand_raw_size\":%lu,\"ram_available\":%lu,\"has_emmc\":%s,\"emmc_size\":%lu,\"nand_type\":\"%s\"}",
		(sfi->flash_type == SMEM_BOOT_SPI_FLASH ? "true" : "false")
		,(unsigned long)(sfi->flash_type == SMEM_BOOT_SPI_FLASH ? get_spi_flash_size() : 0)
#ifdef CONFIG_CMD_NAND
		,(nand_info[0].size > 0 || (CONFIG_SYS_MAX_NAND_DEVICE > 1 && nand_info[1].size > 0) ? "true" : "false")
		,(unsigned long)(nand_info[0].size > 0 ? nand_info[0].size : (CONFIG_SYS_MAX_NAND_DEVICE > 1 ? nand_info[1].size : 0))
		,(unsigned long)(nand_info[0].size > 0 && nand_info[0].writesize > 0 ?
			(nand_info[0].size / nand_info[0].writesize * (nand_info[0].writesize + nand_info[0].oobsize)) :
			(CONFIG_SYS_MAX_NAND_DEVICE > 1 && nand_info[1].size > 0 && nand_info[1].writesize > 0 ?
				(nand_info[1].size / nand_info[1].writesize * (nand_info[1].writesize + nand_info[1].oobsize)) : 0UL))
#else
		,"false",0UL,0UL
#endif
		,(unsigned long)(CONFIG_SYS_SDRAM_END - WEBFAILSAFE_UPLOAD_RAM_ADDRESS)
#ifdef CONFIG_QCA_MMC
		,(blk_dev ? "true" : "false")
		,(unsigned long)(blk_dev ? (unsigned long)blk_dev->lba * (unsigned long)blk_dev->blksz : 0UL)
#else
		,"false",0UL
#endif
#ifdef CONFIG_CMD_NAND
		,(nand_info[0].size > 0 ? "parallel" :
			(CONFIG_SYS_MAX_NAND_DEVICE > 1 && nand_info[1].size > 0 ? "spi" : "parallel")
		)
#else
		,"none"
#endif
	);

	hdr_len = sprintf(hdr,
		"HTTP/1.0 200 OK\r\nServer: uIP/0.9\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n", pos);

	memmove(part_json_buf + hdr_len, part_json_buf, pos);
	memcpy(part_json_buf, hdr, hdr_len);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len + pos;
	uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
}

static void str_trim_crlf(char *s) {
	char *p;
	if ((p = strchr(s, ' ')))  *p = '\0';
	if ((p = strchr(s, '\r')))  *p = '\0';
	if ((p = strchr(s, '\n')))  *p = '\0';
}

static int hexval(char c) {
	return (c >= '0' && c <= '9') ? c - '0' :
			(c >= 'a' && c <= 'f') ? c - 'a' + 10 :
			(c >= 'A' && c <= 'F') ? c - 'A' + 10 : 0;
}

static void url_decode(char *s) {
	char *src = s, *dst = s;
	while (*src) {
		if (*src == '%' && src[1] && src[2]) {
			*dst++ = (char)(hexval(src[1]) * 16 + hexval(src[2]));
			src += 3;
		} else if (*src == '+') {
			*dst++ = ' ';
			src++;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

static void httpd_handle_backup(void) {
	char *query = strchr((char *)&uip_appdata[4], '?');
	char part_name[64], filename[96];
	ulong offset, size;
	int hdr_len;
	int raw = 0;

	if (!query || strncmp(query + 1, "part=", 5) != 0) {
		static const char *err = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\nMissing partition";
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)err;
		hs->upload = strlen(err);
		uip_send(hs->dataptr, hs->upload);
		return;
	}

	strncpy(part_name, query + 6, sizeof(part_name) - 1);
	part_name[sizeof(part_name) - 1] = '\0';
	str_trim_crlf(part_name);
	url_decode(part_name);

	{
		char *amp = strchr(part_name, '&');
		if (amp) *amp = '\0';
	}

	if (strstr(query, "raw=1"))
		raw = 1;

	printf("Backup request: %s%s\n", part_name, raw ? " (raw)" : "");

	if (flashread_partition(part_name, WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
					  0, raw, &offset, &size) != CMD_RET_SUCCESS) {
		static const char *err = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\nRead failed";
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)err;
		hs->upload = strlen(err);
		uip_send(hs->dataptr, hs->upload);
		return;
	}

	httpd_poll_wait(1);

	sprintf(filename, "%s%s.bin", part_name, raw ? "_oob" : "");
	hdr_len = sprintf(part_json_buf,
		"HTTP/1.0 200 OK\r\nServer: uIP/0.9\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Content-Length: %lu\r\n"
		"Connection: close\r\n\r\n",
		filename, size);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len;
	uip_send(hs->dataptr, hdr_len);

	backup_data_addr = WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
	backup_data_size = size;
	backup_sending_header = 1;
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
	httpd_upload_progress();
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
		if (strncmp((char *)&uip_appdata[4], "/partitions", 11) == 0 &&
			uip_appdata[15] == ISO_space) {
			httpd_handle_partitions();
			return;
		}
		if (strncmp((char *)&uip_appdata[4], "/backup?", 8) == 0) {
			httpd_handle_backup();
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
	if (backup_sending_header && hs->upload <= uip_mss()) {
		backup_sending_header = 0;
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)backup_data_addr;
		hs->upload = backup_data_size;
		uip_send(hs->dataptr, (hs->upload > uip_mss() ? uip_mss() : hs->upload));
		return;
	}
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