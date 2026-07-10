#include <common.h>
#include <net.h>
#include <malloc.h>
#include <asm/byteorder.h>
#ifdef CONFIG_CMD_NAND
#undef LED_OFF
#include <nand.h>
#endif
#include <ipq_api.h>
#include <asm-generic/global_data.h>
#include <asm/arch-qca-common/smem.h>
#include <command.h>
#include <webterm.h>
#include <asm/gpio.h>
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
extern unsigned int get_spi_flash_size(void);
extern int flashread_partition(const char *part_name, ulong addr,
					 ulong user_size, int raw, ulong *out_offset,
					 ulong *out_size);
#ifdef CONFIG_DHCPD
#include "../net/dhcpd.h"
#endif

#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "lwip/ip4_addr.h"

#include "failsafe_httpd.h"
#include "failsafe_httpd_types.h"
#include "ethernetif.h"
#include "fs_wrapper.h"

DECLARE_GLOBAL_DATA_PTR;

#define WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES 184

#define ISO_space   0x20
#define ISO_nl      0x0a
#define ISO_cr      0x0d
#define ISO_tab     0x09

#define is_digit(c) ((c) >= '0' && (c) <= '9')
#define is_http_whitespace(c) ((c) == ISO_space || (c) == ISO_cr || (c) == ISO_nl || (c) == ISO_tab)
#define is_http_method_separator(c) ((c) == ISO_space || (c) == ISO_tab)

#define WEBFAILSAFE_PROGRESS_START           0
#define WEBFAILSAFE_PROGRESS_UPLOADING       1
#define WEBFAILSAFE_PROGRESS_UPLOAD_READY    2
#define WEBFAILSAFE_PROGRESS_UPGRADING       3
#define WEBFAILSAFE_PROGRESS_UPGRADE_READY   4
#define WEBFAILSAFE_PROGRESS_UPGRADE_FAILED  5

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define PART_JSON_BUF_SIZE 2048

extern int webfailsafe_is_running;
extern int webfailsafe_ready_for_upgrade;
extern int webfailsafe_upgrade_type;
extern int webfailsafe_img_flash;
extern u32 net_boot_file_size;
extern int do_http_upgrade(ulong size, int upgrade_type);
extern void do_http_progress(int state);
extern void led_on(const char *name);
extern void led_off(const char *name);
extern void led_toggle(const char *name);
extern ulong get_timer(ulong base);
extern void HttpdDone(void);
extern void HttpdStop(void);
extern int eth_rx(void);
extern int eth_init(void);
extern void eth_halt(void);
extern void eth_set_current(void);
extern uchar *net_tx_packet;
extern uchar *net_rx_packets[];
extern struct in_addr net_ip;
extern struct in_addr net_netmask;
extern uchar net_ethaddr[6];
extern const struct fsdata_file file_index_html[];
extern const struct fsdata_file file_404_html[];
extern u64 get_firmware_upgrade_max_size(void);
extern u64 get_uboot_size(void);
extern u64 get_art_size(void);
extern u64 get_cdt_size(void);
extern u64 get_mibib_size(void);

static char eol[3] = { 0x0d, 0x0a, 0x00 };
static char eol2[5] = { 0x0d, 0x0a, 0x0d, 0x0a, 0x00 };
static char *boundary_value;
static u32_t upload_ram_end;
static int data_start_found;
static u8_t post_packet_counter = 255;
static u32_t post_led_counter = 0;
static u32_t upload_start_time = 0;
static int file_too_big = 0;
static int webfailsafe_upload_failed_local = 0;
static int webfailsafe_post_done_local = 0;
static u32_t backup_data_size;
static u32_t backup_data_addr;
static int backup_sending_header;
extern u8_t *webfailsafe_data_pointer;
int upgrade_status = 0;
static char part_json_buf[PART_JSON_BUF_SIZE];
static struct failsafe_httpd_state *hs_global;

static void httpd_poll_wait(int count);

static int atoi_local(const char *s) {
	int i = 0;
	while (is_digit(*s))
		i = i * 10 + *(s++) - '0';
	return i;
}

static void print_error(const char *msg) {
	printf("\n## Error: %s\n", msg);
}

static void print_file_size_error(u64 max_size) {
	printf("## Error: size too large, max size <= %llu bytes\n", max_size);
}

static void httpd_upload_progress(struct failsafe_httpd_state *hs) {
	enum { bar_width = 25 };
	u32_t data_written, elapsed, speed;
	u32_t percent, filled, i;
	char bar[bar_width + 1];

	if (hs->upload_total == 0)
		return;

	data_written = (u32_t)(webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
	percent = (u32_t)((u64_t)data_written * 100 / hs->upload_total);
	if (percent > 100)
		percent = 100;

	if (percent / 25 != post_packet_counter / 25) {
		filled = (percent * bar_width) / 100;
		for (i = 0; i < bar_width; i++)
			bar[i] = (i < filled) ? '#' : '.';
		bar[bar_width] = '\0';
		elapsed = (u32_t)get_timer(upload_start_time);
		speed = (elapsed > 0) ? (u32_t)((u64_t)data_written * 1000 / elapsed) : 0;
		printf("\rUploading: [%s] %3u%% %u.%02u MiB/s", bar, percent, speed / (1024 * 1024), (speed % (1024 * 1024)) * 100 / (1024 * 1024));
		post_packet_counter = (u8_t)percent;
	}

	post_led_counter++;
	if (post_led_counter >= 10000) {
		post_led_counter = 0;
		do_http_progress(WEBFAILSAFE_PROGRESS_UPLOADING);
	}
}

static void httpd_state_reset(struct failsafe_httpd_state *hs) {
	hs->state = STATE_NONE;
	hs->last_activity = (u32_t)get_timer(0);
	hs->dataptr = 0;
	hs->upload = 0;
	hs->upload_total = 0;
	if (hs->owns_global) {
		hs->owns_global = 0;
		hs_global = NULL;
		tcp_setprio(hs->pcb, TCP_PRIO_MIN);
		data_start_found = 0;
		post_packet_counter = 255;
		post_led_counter = 0;
		upload_start_time = 0;
		file_too_big = 0;
		backup_data_size = 0;
		backup_sending_header = 0;
		led_on("blink_led");
		if (boundary_value) {
			free(boundary_value);
			boundary_value = NULL;
		}
	}
}

typedef u64 (*get_max_size_fn)(void);

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

static int httpd_findandstore_firstchunk(struct failsafe_httpd_state *hs, char *data, int data_len) {
	char *start = NULL;
	char *end = NULL;
	u32_t i;

	if (!boundary_value)
		return 0;

	start = strstr(data, boundary_value);
	if (!start)
		return 0;

	for (i = 0; i < ARRAY_SIZE(upload_types); i++) {
		if (strstr(start, upload_types[i].name)) {
			printf("Upgrade type: %s\n", upload_types[i].label);
			webfailsafe_upgrade_type = upload_types[i].type;
			if (upload_types[i].type == WEBFAILSAFE_UPGRADE_TYPE_IMG) {
				webfailsafe_img_flash = strstr(start, "img_nand_raw") ? IMG_FLASH_NAND_RAW :
					strstr(start, "img_nand") ? IMG_FLASH_NAND :
					strstr(start, "img_emmc") ? IMG_FLASH_EMMC :
					strstr(start, "img_nor") ? IMG_FLASH_NOR : 0;
			}
			break;
		}
	}

	if (i == ARRAY_SIZE(upload_types)) {
		print_error("input name not found!");
		return 0;
	}

	end = strstr(strstr(start, upload_types[i].name), eol2);
	if (!end) {
		print_error("couldn't find start of data!");
		return 0;
	}

	if ((end - data) >= data_len)
		return 0;

	end += 4;
	hs->upload_total = hs->upload_total - (int)(end - start) - strlen(boundary_value) - 6;
	printf("Upload size: %u.%02u MiB [%u bytes | 0x%x]\n",
		hs->upload_total / (1024 * 1024),
		(hs->upload_total % (1024 * 1024)) * 100 / (1024 * 1024),
		hs->upload_total, hs->upload_total);

	if (upload_types[i].get_max_size) {
		u64 max_size = upload_types[i].get_max_size();
		if ((u64)hs->upload_total > max_size) {
			print_file_size_error(max_size);
			webfailsafe_upload_failed_local = 1;
			file_too_big = 1;
		}
	}

	hs->upload = (u32_t)(data_len - (end - data));
	if (file_too_big)
		return 1;

	if (webfailsafe_data_pointer + hs->upload > (u8_t *)upload_ram_end) {
		print_error("data larger than available RAM space!");
		webfailsafe_upload_failed_local = 1;
		file_too_big = 1;
		return 1;
	}

	memcpy((void *)webfailsafe_data_pointer, (void *)end, hs->upload);
	webfailsafe_data_pointer += hs->upload;
	upload_start_time = (u32_t)get_timer(0);
	httpd_upload_progress(hs);
	return 1;
}

static int httpd_parse_content_length(struct failsafe_httpd_state *hs, char *data) {
	char *start = strstr(data, "Content-Length:");
	char *end;
	if (start) {
		start += sizeof("Content-Length:");
		end = strstr(start, eol);
		if (end) {
			hs->upload_total = atoi_local(start);
			return 0;
		}
	}
	print_error("couldn't find \"Content-Length\"!");
	return -1;
}

static int httpd_parse_boundary(char *data) {
	char *start = strstr(data, "boundary=");
	char *end;
	if (start) {
		start += 9;
		end = strstr(start, eol);
		if (end) {
			boundary_value = malloc(end - start + 3);
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
	u32_t memset_len;
	webfailsafe_data_pointer = (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
	upload_ram_end = (u32_t)CONFIG_SYS_SDRAM_END;
	if (!webfailsafe_data_pointer) {
		print_error("couldn't allocate RAM for data!");
		return -1;
	}
	printf("Upload RAM address: 0x%x\n", (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
	printf("Available RAM space: %u.%02u MiB\n",
		(upload_ram_end - (u32_t)webfailsafe_data_pointer) / (1024 * 1024),
		((upload_ram_end - (u32_t)webfailsafe_data_pointer) % (1024 * 1024)) * 100 / (1024 * 1024));
	memset_len = WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES;
	if (webfailsafe_data_pointer + memset_len > (u8_t *)upload_ram_end)
		memset_len = upload_ram_end - (u32_t)webfailsafe_data_pointer;
	if (memset_len > 0)
		memset((void *)webfailsafe_data_pointer, 0xFF, memset_len);
	return 0;
}

static int httpd_check_upload_size(struct failsafe_httpd_state *hs) {
	if (hs->upload_total < 10240 && webfailsafe_upgrade_type != WEBFAILSAFE_UPGRADE_TYPE_CDT) {
		print_error("request for upload < 10 KB data!");
		return -1;
	}
	if (webfailsafe_upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT &&
		hs->upload_total < WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES) {
		printf("## Error: CDT data too small, minimum %d bytes!\n",
			WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES);
		return -1;
	}
	return 0;
}

static void httpd_upload_complete(struct failsafe_httpd_state *hs) {
	if (webfailsafe_upload_failed_local) {
		printf("\nfailed!\n");
	} else {
		printf("  Done!\n");
	}
	led_on("blink_led");
	webfailsafe_post_done_local = 1;
	upgrade_status = 0;
	net_boot_file_size = (ulong)hs->upload_total;
}

static int httpd_check_upload_complete(struct failsafe_httpd_state *hs) {
	if (hs->upload >= hs->upload_total + strlen(boundary_value) + 6) {
		httpd_upload_complete(hs);
		static const char resp_ok[] = "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n";
		static const char resp_err[] = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\n";
		httpd_state_reset(hs);
		hs->state = STATE_FILE_REQUEST;
		if (!webfailsafe_upload_failed_local) {
			hs->dataptr = (u8_t *)resp_ok;
			hs->upload = sizeof(resp_ok) - 1;
		} else {
			hs->dataptr = (u8_t *)resp_err;
			hs->upload = sizeof(resp_err) - 1;
		}
		httpd_send_data(hs);
		return 1;
	}
	return 0;
}

static void httpd_handle_upload_data(struct failsafe_httpd_state *hs, char *data, int data_len) {
	u32_t bytes_to_write = (u32_t)data_len;
	u32_t data_written = (u32_t)(webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);

	if ((u64_t)data_written + bytes_to_write > hs->upload_total)
		bytes_to_write = hs->upload_total - data_written;

	if (bytes_to_write > 0 && webfailsafe_data_pointer + bytes_to_write > (u8_t *)upload_ram_end) {
		print_error("data larger than available RAM space!");
		webfailsafe_upload_failed_local = 1;
		file_too_big = 1;
	} else if (bytes_to_write > 0) {
		memcpy((void *)webfailsafe_data_pointer, (void *)data, bytes_to_write);
		webfailsafe_data_pointer += bytes_to_write;
	}
	httpd_upload_progress(hs);
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

static void httpd_handle_upgrade_status(struct failsafe_httpd_state *hs) {
	static const char *status_text[] = {"idle", "verifying", "flashing", "type_mismatch", "rebooting"};
	static char resp[128];
	int len = sprintf(resp, "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s", status_text[upgrade_status]);
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)resp;
	hs->upload = len;
	httpd_send_data(hs);
}

static void httpd_handle_partitions(struct failsafe_httpd_state *hs) {
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
			(CONFIG_SYS_MAX_NAND_DEVICE > 1 && nand_info[1].size > 0 ? "spi" : "parallel"))
#else
		,"none"
#endif
	);

	hdr_len = sprintf(hdr,
		"HTTP/1.0 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n", pos);

	memmove(part_json_buf + hdr_len, part_json_buf, pos);
	memcpy(part_json_buf, hdr, hdr_len);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len + pos;
	httpd_send_data(hs);
}

static void httpd_handle_backup(struct failsafe_httpd_state *hs, char *data, int data_len) {
	char *query = strchr(&data[4], '?');
	char part_name[64], filename[96];
	ulong offset, size;
	int hdr_len;
	int raw = 0;

	if (!query || strncmp(query + 1, "part=", 5) != 0) {
		static const char *err = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\nMissing partition";
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)err;
		hs->upload = strlen(err);
		httpd_send_data(hs);
		return;
	}

	printf("Backup request: parsing...\n");
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
		httpd_send_data(hs);
		return;
	}

	httpd_poll_wait(1);

	sprintf(filename, "%s%s.bin", part_name, raw ? "_oob" : "");
	hdr_len = sprintf(part_json_buf,
		"HTTP/1.0 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Content-Length: %u\r\n"
		"Connection: close\r\n\r\n",
		filename, (u32_t)size);

	hs->state = STATE_FILE_REQUEST;
	hs->owns_global = 1;
	tcp_setprio(hs->pcb, TCP_PRIO_NORMAL);
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len;
	httpd_send_data(hs);

	backup_data_addr = (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
	backup_data_size = (u32_t)size;
	backup_sending_header = 1;
}

static void httpd_handle_file_request(struct failsafe_httpd_state *hs, char *data, int data_len) {
	struct fs_file fsfile;
	u32_t i;

	if (memcmp((const void *)&data[4], "/cgi-bin/", 9) == 0) {
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)"HTTP/1.0 302 Found\r\nLocation: /index.html\r\n\r\n";
		hs->upload = 44;
		httpd_send_data(hs);
		return;
	}

	for (i = 4; i < 30; i++) {
		if (is_http_whitespace(data[i])) {
			data[i] = 0;
			i = 0;
			break;
		}
	}
	if (i != 0) {
		print_error("request file name too long!");
		{
			struct tcp_pcb *save_pcb = hs->pcb;
			httpd_state_reset(hs);
			free(hs);
			tcp_abort(save_pcb);
		}
		return;
	}

	printf("Request for: %s\n", &data[4]);

	if (data[4] == ISO_slash && data[5] == 0) {
		fs_open(file_index_html[0].name, &fsfile);
	} else {
		if (!fs_open((const char *)&data[4], &fsfile)) {
			print_error("file not found!");
			fs_open(file_404_html[0].name, &fsfile);
		}
	}

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)fsfile.data;
	hs->upload = fsfile.len;

	httpd_send_data(hs);
}

void httpd_send_data(struct failsafe_httpd_state *hs) {
	u16_t snd_buf = tcp_sndbuf(hs->pcb);
	if (snd_buf == 0 || hs->upload == 0)
		return;

	u16_t send_len = (hs->upload > snd_buf) ? snd_buf : hs->upload;
	err_t wr_err = tcp_write(hs->pcb, hs->dataptr, send_len, TCP_WRITE_FLAG_COPY);
	if (wr_err == ERR_OK) {
		tcp_output(hs->pcb);
		hs->dataptr += send_len;
		hs->upload -= send_len;
		return;
	}

	if (send_len > TCP_MSS) {
		send_len = TCP_MSS;
		if (hs->upload < send_len)
			send_len = (u16_t)hs->upload;
		wr_err = tcp_write(hs->pcb, hs->dataptr, send_len, TCP_WRITE_FLAG_COPY);
		if (wr_err == ERR_OK) {
			tcp_output(hs->pcb);
			hs->dataptr += send_len;
			hs->upload -= send_len;
		}
	}
}

static void httpd_poll_wait(int count) {
	int i;
	for (i = 0; i < count; i++) {
		mdelay(100);
		eth_rx();
		sys_check_timeouts();
	}
}

static err_t httpd_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
	struct failsafe_httpd_state *hs = (struct failsafe_httpd_state *)arg;

	if (hs == NULL)
		return ERR_OK;

	hs->last_activity = (u32_t)get_timer(0);

	if (backup_sending_header && hs->upload <= 0) {
		backup_sending_header = 0;
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)(uintptr_t)backup_data_addr;
		hs->upload = backup_data_size;
	}

	if (hs->upload <= 0) {
		if (webfailsafe_post_done_local) {
			if (!webfailsafe_upload_failed_local)
				webfailsafe_ready_for_upgrade = 1;
			webfailsafe_post_done_local = 0;
			webfailsafe_upload_failed_local = 0;
		}
		httpd_state_reset(hs);
		tcp_close(pcb);
		return ERR_OK;
	}

	httpd_send_data(hs);
	return ERR_OK;
}

static err_t httpd_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
	struct failsafe_httpd_state *hs = (struct failsafe_httpd_state *)arg;
	char *data;
	int data_len;
	int need_free = 0;

	if (hs == NULL) {
		if (p) pbuf_free(p);
		return ERR_OK;
	}

	if (p == NULL) {
		httpd_state_reset(hs);
		tcp_close(pcb);
		return ERR_OK;
	}

	hs->last_activity = (u32_t)get_timer(0);

	if (p == NULL) {
		data = (char *)p->payload;
		data_len = p->len;
	} else {
		data = malloc(p->tot_len + 1);
		if (!data) {
			pbuf_free(p);
			httpd_state_reset(hs);
			free(hs);
			tcp_abort(pcb);
			return ERR_ABRT;
		}
		int offset = 0;
		struct pbuf *q;
		for (q = p; q; q = q->next) {
			memcpy(data + offset, q->payload, q->len);
			offset += q->len;
		}
		data_len = p->tot_len;
		data[data_len] = '\0';
		need_free = 1;
	}

	switch (hs->state) {
	case STATE_NONE:
		if (strncmp(data, "GET", 3) == 0 && is_http_method_separator(data[3])) {
			if (strncmp(&data[4], "/webterm", 8) == 0) {
				webterm_http_handler(hs, data, data_len);
				break;
			}
			if (strncmp(&data[4], "/upgrade_status", 15) == 0) {
				httpd_handle_upgrade_status(hs);
				break;
			}
			if (strncmp(&data[4], "/partitions", 11) == 0 &&
				data[15] == ISO_space) {
				httpd_handle_partitions(hs);
				break;
			}
			if (strncmp(&data[4], "/backup?", 8) == 0) {
				httpd_handle_backup(hs, data, data_len);
				break;
			}
			httpd_handle_file_request(hs, data, data_len);
		} else if (strncmp(data, "POST", 4) == 0 && is_http_method_separator(data[4])) {
			if (strncmp(&data[5], "/webterm", 8) == 0) {
				webterm_http_handler(hs, data, data_len);
				break;
			}
			data[data_len] = '\0';
			if (httpd_parse_content_length(hs, data) < 0) {
				httpd_state_reset(hs);
				free(hs);
				tcp_abort(pcb);
				if (need_free) free(data);
				pbuf_free(p);
				return ERR_ABRT;
			}
			hs->state = STATE_UPLOAD_REQUEST;
			hs->owns_global = 1;
			hs_global = hs;
			tcp_setprio(pcb, TCP_PRIO_NORMAL);
			led_off("blink_led");
			if (httpd_parse_boundary(data) < 0 || httpd_init_upload_ram() < 0) {
				httpd_state_reset(hs);
				free(hs);
				tcp_abort(pcb);
				if (need_free) free(data);
				pbuf_free(p);
				return ERR_ABRT;
			}
			if (httpd_findandstore_firstchunk(hs, data, data_len)) {
				data_start_found = 1;
				if (httpd_check_upload_size(hs) < 0) {
					httpd_state_reset(hs);
					free(hs);
					tcp_abort(pcb);
					if (need_free) free(data);
					pbuf_free(p);
					return ERR_ABRT;
				}
				httpd_check_upload_complete(hs);
			} else {
				data_start_found = 0;
			}
		} else {
			httpd_state_reset(hs);
			free(hs);
			tcp_abort(pcb);
			if (need_free) free(data);
			pbuf_free(p);
			return ERR_ABRT;
		}
		break;

	case STATE_UPLOAD_REQUEST:
		if (!data_start_found) {
			data[data_len] = '\0';
			if (!httpd_findandstore_firstchunk(hs, data, data_len)) {
				print_error("couldn't find start of data in next packet!");
				httpd_state_reset(hs);
				free(hs);
				tcp_abort(pcb);
				if (need_free) free(data);
				pbuf_free(p);
				return ERR_ABRT;
			}
			data_start_found = 1;
			if (httpd_check_upload_size(hs) < 0) {
				httpd_state_reset(hs);
				free(hs);
				tcp_abort(pcb);
				if (need_free) free(data);
				pbuf_free(p);
				return ERR_ABRT;
			}
			httpd_check_upload_complete(hs);
		} else {
			hs->upload += data_len;
			if (!webfailsafe_upload_failed_local)
				httpd_handle_upload_data(hs, data, data_len);
			httpd_check_upload_complete(hs);
		}
		break;

	case STATE_FILE_REQUEST:
		break;
	}

	tcp_recved(pcb, p->tot_len);
	if (need_free) free(data);
	pbuf_free(p);
	return ERR_OK;
}

static void httpd_err(void *arg, err_t err) {
	struct failsafe_httpd_state *hs = (struct failsafe_httpd_state *)arg;
	if (hs) {
		if (hs == hs_global)
			hs_global = NULL;
		httpd_state_reset(hs);
		free(hs);
	}
}

static err_t httpd_poll_cb(void *arg, struct tcp_pcb *pcb) {
	struct failsafe_httpd_state *hs = (struct failsafe_httpd_state *)arg;
	if (hs == NULL)
		return ERR_OK;

	if (get_timer(hs->last_activity) >= 300000) {
		if (hs == hs_global)
			hs_global = NULL;
		httpd_state_reset(hs);
		free(hs);
		tcp_abort(pcb);
		return ERR_ABRT;
	}

	if (hs->upload > 0)
		httpd_send_data(hs);

	return ERR_OK;
}

static err_t httpd_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
	struct failsafe_httpd_state *hs;

	hs = malloc(sizeof(struct failsafe_httpd_state));
	if (hs == NULL) {
		return ERR_MEM;
	}
	memset(hs, 0, sizeof(struct failsafe_httpd_state));
	hs->pcb = pcb;
	hs->state = STATE_NONE;
	hs->last_activity = get_timer(0);
	hs->owns_global = 0;

	tcp_arg(pcb, hs);
	tcp_recv(pcb, httpd_recv);
	tcp_sent(pcb, httpd_sent);
	tcp_err(pcb, httpd_err);
	tcp_poll(pcb, httpd_poll_cb, 4);
	tcp_setprio(pcb, TCP_PRIO_MIN);

	return ERR_OK;
}

static struct tcp_pcb *listen_pcb;

void failsafe_httpd_init(void) {
	struct tcp_pcb *pcb;

	fs_init();

	pcb = tcp_new();
	if (pcb == NULL)
		return;

	if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
		tcp_close(pcb);
		return;
	}

	listen_pcb = tcp_listen(pcb);
	if (listen_pcb == NULL) {
		tcp_close(pcb);
		return;
	}

	tcp_accept(listen_pcb, httpd_accept);
}

static struct netif failsafe_netif;

void failsafe_lwip_init(struct ip4_addr *ipaddr, struct ip4_addr *netmask, struct ip4_addr *gw) {
	lwip_init();

	netif_add(&failsafe_netif, ipaddr, netmask, gw, NULL,
		  ethernetif_init, ethernet_input);
	netif_set_default(&failsafe_netif);
	netif_set_up(&failsafe_netif);
	netif_set_link_up(&failsafe_netif);

	failsafe_httpd_init();
}

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
static void ppe_arp_kickstart(void);
#endif

void failsafe_httpd_poll(void) {
	static int httpd_progress_start_done = 0;
	static int eth_init_attempted = 0;
	static ulong periodic_timer = 0;
	ulong now = get_timer(0);
#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	int link_changed = 0;
#endif

	if (!webfailsafe_is_running)
		return;

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
#ifdef CONFIG_DHCPD
		dhcpd_poll_server();
#endif
	}

	sys_check_timeouts();

	if (get_timer(periodic_timer) >= 500) {
		periodic_timer = now;
		sys_check_timeouts();
	}
}

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
#include "lwip/prot/ethernet.h"

struct ppe_arp_hdr {
	struct eth_hdr ethhdr;
	u16_t hwtype;
	u16_t protocol;
	u8_t hwlen;
	u8_t protolen;
	u16_t opcode;
	struct eth_addr shwaddr;
	u16_t sipaddr[2];
	struct eth_addr dhwaddr;
	u16_t dipaddr[2];
};

static void ppe_arp_kickstart(void) {
	uchar pkt[60];
	struct ppe_arp_hdr *arp = (struct ppe_arp_hdr *)pkt;
	u16_t *hostaddr = (u16_t *)netif_ip4_addr(&failsafe_netif);

	memset(pkt, 0, sizeof(pkt));
	memset(arp->ethhdr.dest.addr, 0xff, 6);
	arp->ethhdr.src = *(struct eth_addr *)net_ethaddr;
	arp->ethhdr.type = lwip_htons(ETHTYPE_ARP);

	arp->hwtype = lwip_htons(1);
	arp->protocol = lwip_htons(ETHTYPE_IP);
	arp->hwlen = 6;
	arp->protolen = 4;
	arp->opcode = lwip_htons(1);

	arp->shwaddr = *(struct eth_addr *)net_ethaddr;
	arp->sipaddr[0] = hostaddr[0];
	arp->sipaddr[1] = hostaddr[1];

	arp->dhwaddr = *(struct eth_addr *)net_ethaddr;
	arp->dipaddr[0] = hostaddr[0];
	arp->dipaddr[1] = (hostaddr[1] & lwip_htons(0xFF00)) | lwip_htons(0x00FE);

	eth_send(pkt, sizeof(pkt));
}
#endif

void httpd_poll(void) {
	failsafe_httpd_poll();
}

void httpd_stop(void) {
	webfailsafe_is_running = 0;
}

int httpd_is_running(void) {
	return webfailsafe_is_running;
}