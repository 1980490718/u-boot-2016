#include <common.h>
#include <net.h>
#include <malloc.h>
#include <asm/byteorder.h>
#ifdef CONFIG_CMD_NAND
#include <nand.h>
#endif
#include <ipq_api.h>
#include <asm/io.h>
#include <spi_flash.h>
#include <asm-generic/global_data.h>
#include <asm/arch-qca-common/smem.h>
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ6018) \
	|| defined(CONFIG_IPQ5018) || defined(CONFIG_IPQ5332) \
	|| defined(CONFIG_IPQ9574) || defined(CONFIG_IPQ806X)
#include <asm/arch-qca-common/qpic_nand.h>
#endif
#include <command.h>
#include <webterm.h>
#include <version.h>
#include <fdtdec.h>
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
#include <miiphy.h>
#include <linux/mdio.h>
#endif
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
extern struct spi_flash *spi_flash_ptr[MAX_SF_BUS_NUM][MAX_SF_CS_NUM];
extern int flashread_partition(const char *part_name, ulong addr, ulong user_size, int raw, ulong *out_offset, ulong *out_size);
extern int flashread_partition_chunk(const char *part_name, ulong addr, u64 user_offset, ulong user_size, int raw, ulong *out_offset, ulong *out_size, char *out_detail);
extern void (*flashread_yield_fn)(void);
#ifdef CONFIG_DHCPD
#include "../net/dhcpd.h"
#endif

#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
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

#define ISO_space	0x20
#define ISO_nl		0x0a
#define ISO_cr		0x0d
#define ISO_tab		0x09

#define is_digit(c) ((c) >= '0' && (c) <= '9')
#define is_http_whitespace(c) ((c) == ISO_space || (c) == ISO_cr || (c) == ISO_nl || (c) == ISO_tab)
#define is_http_method_separator(c) ((c) == ISO_space || (c) == ISO_tab)

#define WEBFAILSAFE_PROGRESS_START			0
#define WEBFAILSAFE_PROGRESS_UPLOADING		1
#define WEBFAILSAFE_PROGRESS_UPLOAD_READY	2
#define WEBFAILSAFE_PROGRESS_UPGRADING		3
#define WEBFAILSAFE_PROGRESS_UPGRADE_READY	4
#define WEBFAILSAFE_PROGRESS_UPGRADE_FAILED	5

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define PART_JSON_BUF_SIZE 2048

extern int webfailsafe_is_running;
extern int webfailsafe_ready_for_upgrade;
extern int webfailsafe_upgrade_type;
extern int webfailsafe_img_flash;
extern u32 net_boot_file_size;
extern int do_http_upgrade(ulong size, int upgrade_type);
extern void do_http_progress(int state);
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
static struct {
	u32_t ram_end;
	int data_start_found;
	u8_t packet_counter;
	u32_t led_counter;
	u32_t start_time;
	int file_too_big;
	int failed;
	int done;
} upload = { .packet_counter = 255 };

static struct {
	u32_t data_size;
	u32_t data_addr;
	int sending_header;
	u64 total_remaining;
	u64 total_sent;
	u64 total_size;
	u64 chunk_offset;
	int chunked;
	int chunk_busy;
	int chunk_num;
	int total_chunks;
	int raw;
	char part_name[64];
} backup;

extern u8_t *webfailsafe_data_pointer;
int upgrade_status = 0;
static char part_json_buf[PART_JSON_BUF_SIZE];
static struct failsafe_httpd_state *hs_global;

static void httpd_poll_wait(int count);

static void flashread_yield(void) {
	eth_rx();
	sys_check_timeouts();
}

static u64 atoi_local(const char *s) {
	u64 i = 0;
	while (is_digit(*s))
		i = i * 10 + *(s++) - '0';
	return i;
}

#define mib_int(b) ((b) / (1024 * 1024))
#define mib_frac(b) (((b) % (1024 * 1024)) * 100 / (1024 * 1024))

static void print_error(const char *msg) {
	printf("\n## Error: %s\n", msg);
}

static void print_file_size_error(u64 max_size) {
	printf("## Error: size too large, max size <= %llu bytes\n", max_size);
}

static void httpd_upload_progress(struct failsafe_httpd_state *hs) {
	enum { bar_width = 25 };
	u32_t data_written, elapsed, speed, percent, filled, i;
	char bar[bar_width + 1];

	if (hs->upload_total == 0)
		return;

	data_written = (u32_t)(webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
	percent = (u32_t)((u64_t)data_written * 100 / hs->upload_total);
	if (percent > 100)
		percent = 100;

	if (percent / 25 != upload.packet_counter / 25) {
		filled = (percent * bar_width) / 100;
		for (i = 0; i < bar_width; i++)
			bar[i] = (i < filled) ? '#' : '.';
		bar[bar_width] = '\0';
		elapsed = (u32_t)get_timer(upload.start_time);
		speed = (elapsed > 0) ? (u32_t)((u64_t)data_written * 1000 / elapsed) : 0;
		printf("\rUploading: [%s] %3u%% %u.%02u MiB/s", bar, percent, (u32)mib_int(speed), (u32)mib_frac(speed));
		upload.packet_counter = (u8_t)percent;
	}

	upload.led_counter++;
	if (upload.led_counter >= 10000) {
		upload.led_counter = 0;
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
		int done = upload.done, failed = upload.failed;
		hs->owns_global = 0;
		hs_global = NULL;
		tcp_setprio(hs->pcb, TCP_PRIO_MIN);
		memset(&upload, 0, sizeof(upload));
		upload.done = done;
		upload.failed = failed;
		upload.packet_counter = 255;
		memset(&backup, 0, sizeof(backup));
		flashread_yield_fn = NULL;
		led_on("blink_led");
		if (boundary_value) {
			free(boundary_value);
			boundary_value = NULL;
		}
	}
}

static void httpd_conn_abort(struct failsafe_httpd_state *hs, struct tcp_pcb *pcb) {
	tcp_arg(pcb, NULL);
	httpd_state_reset(hs);
	free(hs);
	tcp_abort(pcb);
}

static err_t httpd_recv_abort(struct failsafe_httpd_state *hs, struct tcp_pcb *pcb, char *data, int need_free, struct pbuf *p) {
	httpd_conn_abort(hs, pcb);
	if (need_free) free(data);
	pbuf_free(p);
	return ERR_ABRT;
}

typedef u64 (*get_max_size_fn)(void);

static const struct { const char *name; int type; const char *label; get_max_size_fn get_max_size; } upload_types[] = {
	{"name=\"firmware\"",	WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE,	"FIRMWARE",	get_firmware_upgrade_max_size},
	{"name=\"uboot\"",		WEBFAILSAFE_UPGRADE_TYPE_UBOOT,		"U-Boot",	get_uboot_size},
	{"name=\"art\"",		WEBFAILSAFE_UPGRADE_TYPE_ART,		"ART",		get_art_size},
	{"name=\"img\"",		WEBFAILSAFE_UPGRADE_TYPE_IMG,		"IMG",		NULL},
	{"name=\"cdt\"",		WEBFAILSAFE_UPGRADE_TYPE_CDT,		"CDT",		get_cdt_size},
	{"name=\"mibib\"",		WEBFAILSAFE_UPGRADE_TYPE_MIBIB,		"MIBIB",	get_mibib_size},
	{"name=\"ptable\"",		WEBFAILSAFE_UPGRADE_TYPE_PTABLE,	"PTABLE",	NULL},
	{"name=\"initramfs\"",	WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS,	"INITRAMFS",NULL},
};

static int httpd_findandstore_firstchunk(struct failsafe_httpd_state *hs, char *data, int data_len) {
	char *start = NULL, *end = NULL;
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
	printf("Upload size: %u.%02u MiB [%u bytes | 0x%x]\n", (u32)mib_int(hs->upload_total), (u32)mib_frac(hs->upload_total), hs->upload_total, hs->upload_total);

	if (upload_types[i].get_max_size) {
		u64 max_size = upload_types[i].get_max_size();
		if ((u64)hs->upload_total > max_size) {
			print_file_size_error(max_size);
			upload.failed = 1;
			upload.file_too_big = 1;
		}
	}

	hs->upload = (u32_t)(data_len - (end - data));
	if (upload.file_too_big)
		return 1;

	if (webfailsafe_data_pointer + hs->upload > (u8_t *)upload.ram_end) {
		print_error("data larger than available RAM space!");
		upload.failed = 1;
		upload.file_too_big = 1;
		return 1;
	}

	memcpy((void *)webfailsafe_data_pointer, (void *)end, hs->upload);
	webfailsafe_data_pointer += hs->upload;
	upload.start_time = (u32_t)get_timer(0);
	httpd_upload_progress(hs);
	return 1;
}

static int httpd_parse_content_length(struct failsafe_httpd_state *hs, char *data) {
	char *start = strstr(data, "Content-Length:"), *end;
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
	char *start = strstr(data, "boundary="), *end;
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
	upload.ram_end = (u32_t)CONFIG_SYS_SDRAM_END;
	if (!webfailsafe_data_pointer) {
		print_error("couldn't allocate RAM for data!");
		return -1;
	}
	printf("Upload RAM address: 0x%x\n", (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
	printf("Available RAM space: %u.%02u MiB\n",
		(u32)mib_int(upload.ram_end - (u32_t)webfailsafe_data_pointer),
		(u32)mib_frac(upload.ram_end - (u32_t)webfailsafe_data_pointer));
	memset_len = WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES;
	if (webfailsafe_data_pointer + memset_len > (u8_t *)upload.ram_end)
		memset_len = upload.ram_end - (u32_t)webfailsafe_data_pointer;
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
		printf("## Error: CDT data too small, minimum %d bytes!\n", WEBFAILSAFE_UPLOAD_CDT_MIN_SIZE_IN_BYTES);
		return -1;
	}
	return 0;
}

static void httpd_upload_complete(struct failsafe_httpd_state *hs) {
	if (upload.failed) {
		printf("\nfailed!\n");
	} else {
		printf("  Done!\n");
	}
	led_on("blink_led");
	upload.done = 1;
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
		if (!upload.failed) {
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
	u32_t bytes_to_write = (u32_t)data_len, data_written = (u32_t)(webfailsafe_data_pointer - (u8_t *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);

	if ((u64_t)data_written + bytes_to_write > hs->upload_total)
		bytes_to_write = hs->upload_total - data_written;

	if (bytes_to_write > 0 && webfailsafe_data_pointer + bytes_to_write > (u8_t *)upload.ram_end) {
		print_error("data larger than available RAM space!");
		upload.failed = 1;
		upload.file_too_big = 1;
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

#define ABOUT_BUF_SIZE 4096
static char about_json_buf[ABOUT_BUF_SIZE];

struct phy_id_name {
	u32 id;
	const char *name;
};

static const struct phy_id_name phy_c22_qca[] = {
	{ 0x004DD0B0, "QCA8075 V1.0 5P" },
	{ 0x004DD0B1, "QCA8075 V1.1 5P" },
	{ 0x004DD0B2, "QCA8075 V1.1 2P" },
	{ 0x004DD036, "QCA8337"          },
	{ 0x004DD074, "QCA8033"          },
};

#if defined(CONFIG_IPQ6018) || defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ9574) || defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ5018)
static const struct phy_id_name phy_c22_ext[] = {
	{ 0x004DD0C0, "GEPHY"        },
	{ 0x004DD100, "QCA8081 V1.0" },
	{ 0x004DD101, "QCA8081 V1.1" },
	{ 0x004DD180, "QCA8084"      },
};

static const struct phy_id_name phy_c45_aq[] = {
	{ 0x03a1b4e2, "AQR107"        },
	{ 0x03a1b502, "AQR109"        },
	{ 0x03a1b610, "AQR111"        },
	{ 0x03a1b612, "AQR111B0"      },
	{ 0x03a1b660, "AQR112"        },
	{ 0x03a1b792, "AQR112C"       },
	{ 0x31c31C10, "AQR113C A0"    },
	{ 0x31c31C11, "AQR113C A1"    },
	{ 0x31c31C12, "AQR113C B0"    },
	{ 0x31c31C13, "AQR113C B1"    },
	{ 0x31c31DD3, "Marvell X3410" },
};
#endif

static const char *phy_lookup(u32 id, const struct phy_id_name *tbl, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++)
		if (id == tbl[i].id)
			return tbl[i].name;
	return NULL;
}

static int phy_emit(int pos, int *first, const char *name, int addr, u16 id1, u16 id2)
{
	if (!*first)
		pos += sprintf(about_json_buf + pos, ", ");
	pos += sprintf(about_json_buf + pos,
		"%s@%d (ID %04x:%04x)", name, addr, id1, id2);
	*first = 0;
	return pos;
}

struct about_gpio_ctx {
	char *buf;
	int *pos;
	int *first;
	int env_rk_gpio;
};

static const char *fdt_find_alias_for_path(const char *path) {
	int aliases = fdt_path_offset(gd->fdt_blob, "/aliases");
	if (aliases < 0)
		return NULL;
	int prop_len;
	const struct fdt_property *prop;
	for (int offset = fdt_first_property_offset(gd->fdt_blob, aliases);
		 offset >= 0;
		 offset = fdt_next_property_offset(gd->fdt_blob, offset)) {
		prop = fdt_get_property_by_offset(gd->fdt_blob, offset, &prop_len);
		if (!prop)
			break;
		if (strcmp(prop->data, path) == 0)
			return fdt_string(gd->fdt_blob, fdt32_to_cpu(prop->nameoff));
	}
	return NULL;
}

static void about_gpio_cb(int gpio, const char *name, const char *dir, int value, const char *parent, void *ctx) {
	struct about_gpio_ctx *c = ctx;
	const char *src = getenv(name) ? "env" : "fdt";
	const char *override = "";
	if (c->env_rk_gpio >= 0 && strcmp(parent, "key_gpio") == 0 && src[0] == 'f')
		override = "reset_key";
	char path[80];
	snprintf(path, sizeof(path), "/tlmm-gpio/%s/%s", parent, name);
	const char *alias = fdt_find_alias_for_path(path);
	*c->pos += sprintf(c->buf + *c->pos,
		"%s{\"gpio\":%d,\"name\":\"%s\",\"alias\":\"%s\",\"dir\":\"%s\",\"value\":%d,\"parent\":\"%s\",\"source\":\"%s\",\"override\":\"%s\"}",
		*c->first ? "" : ",", gpio, name, alias ? alias : "", dir, value, parent, src, override);
	*c->first = 0;
}

static void httpd_handle_about(struct failsafe_httpd_state *hs) {
	int pos = 0, hdr_len;
	char hdr[128];
	pos += sprintf(about_json_buf + pos, "{\"version\":\"%s\",", version_string);
	pos += sprintf(about_json_buf + pos, "\"machid\":\"0x%lx\",", gd->bd->bi_arch_number);
	pos += sprintf(about_json_buf + pos, "\"ram_size\":%lu,", (unsigned long)gd->ram_size);

	{
		const char *fdt_model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
#ifdef CONFIG_BOARD_DISPLAY_NAME
		pos += sprintf(about_json_buf + pos, "\"model\":\"%s%s%s%s\",", CONFIG_BOARD_DISPLAY_NAME, fdt_model ? " (" : "", fdt_model ? fdt_model : "", fdt_model ? ")" : "");
#else
		pos += sprintf(about_json_buf + pos, "\"model\":\"%s\",", fdt_model ? fdt_model : "");
#endif
	}

	{
		const char *cn = fdt_getprop(gd->fdt_blob, 0, "config_name", NULL);
		const char *cn_env = getenv("config_name");
		if (cn_env) {
			pos += sprintf(about_json_buf + pos, "\"config_name\":\"%s\"," "\"config_name_source\":\"env\",", cn_env);
		} else if (cn) {
			int cnt = fdt_count_strings(gd->fdt_blob, 0, "config_name");
			pos += sprintf(about_json_buf + pos, "\"config_name\":\"");
			int first = 1;
			for (int i = 0; i < cnt && i < 8; i++) {
				const char *s;
				if (fdt_get_string_index(gd->fdt_blob, 0, "config_name", i, &s) == 0 && s && strchr(s, '@')) {
					if (!first) pos += sprintf(about_json_buf + pos, ", ");
					pos += sprintf(about_json_buf + pos, "%s", s);
					first = 0;
				}
			}
			pos += sprintf(about_json_buf + pos, "\",\"config_name_source\":\"fdt\",");
		} else {
			pos += sprintf(about_json_buf + pos, "\"config_name\":\"\"," "\"config_name_source\":\"\",");
		}
	}

	{
		static const char * const flash_type_names[] = {
			[SMEM_BOOT_NO_FLASH]        = "none",
			[SMEM_BOOT_NOR_FLASH]       = "nor",
			[SMEM_BOOT_NAND_FLASH]      = "nand",
			[SMEM_BOOT_ONENAND_FLASH]   = "onenand",
			[SMEM_BOOT_SDC_FLASH]       = "sdc",
			[SMEM_BOOT_MMC_FLASH]       = "emmc",
			[SMEM_BOOT_SPI_FLASH]       = "spi",
			[SMEM_BOOT_NORPLUSNAND]     = "nor+nand",
			[SMEM_BOOT_NORPLUSEMMC]     = "nor+emmc",
			[SMEM_BOOT_QSPI_NAND_FLASH] = "qspi-nand",
		};
		uint32_t ft;
		const char *ft_name = "unknown";
		if (get_current_flash_type(&ft) == 0 &&
			ft < ARRAY_SIZE(flash_type_names) && flash_type_names[ft])
			ft_name = flash_type_names[ft];
		pos += sprintf(about_json_buf + pos, "\"flash_type\":\"%s\",", ft_name);
	}

	{
		int fc_first = 1;
		pos += sprintf(about_json_buf + pos, "\"flash_chips\":\"");
		{
			int b, c;
			for (b = 0; b < MAX_SF_BUS_NUM; b++)
				for (c = 0; c < MAX_SF_CS_NUM; c++) {
					struct spi_flash *sf = spi_flash_ptr[b][c];
					if (sf && sf->name && sf->size) {
						if (!fc_first)
							pos += sprintf(about_json_buf + pos, ", ");
						pos += sprintf(about_json_buf + pos, "%s %luMiB",
							sf->name, (unsigned long)(sf->size >> 20));
						if (sf->jedec)
							pos += sprintf(about_json_buf + pos,
								" [ID %02x:%02x:%02x",
								sf->jedec >> 16,
								(sf->jedec >> 8) & 0xFF,
								sf->jedec & 0xFF);
						if (sf->jedec && sf->ext_jedec)
							pos += sprintf(about_json_buf + pos, ":%02x", sf->ext_jedec & 0xFF);
						if (sf->jedec)
							pos += sprintf(about_json_buf + pos, "]");
						fc_first = 0;
					}
				}
		}
#ifdef CONFIG_CMD_NAND
		{
			int i;
			for (i = 0; i < CONFIG_SYS_MAX_NAND_DEVICE; i++) {
				struct mtd_info *mtd = &nand_info[i];
#ifdef CONFIG_IPQ_SPI_NAND_INFO_IDX
				if (i == CONFIG_IPQ_SPI_NAND_INFO_IDX)
					continue;
#endif
#ifdef CONFIG_IPQ_SPI_NOR_INFO_IDX
				if (i == CONFIG_IPQ_SPI_NOR_INFO_IDX)
					continue;
#endif
				if (mtd->size > 0 && mtd->writesize > 0 && mtd->writesize != 1) {
					struct nand_chip *chip = mtd->priv;
					if (!fc_first)
						pos += sprintf(about_json_buf + pos, ", ");
					pos += sprintf(about_json_buf + pos, "%s %luMiB", mtd->name ? mtd->name : "nand", (unsigned long)(mtd->size >> 20));
					if (chip && chip->onfi_version) {
						int ml = 20;
						while (ml > 0 && chip->onfi_params.model[ml-1] == ' ')
							ml--;
						if (ml > 0)
							pos += sprintf(about_json_buf + pos, " %.*s", ml, chip->onfi_params.model);
#ifdef CONFIG_QPIC_NAND
						if (chip->priv) {
							struct { unsigned id; } *qd = chip->priv;
							if (qd->id)
								pos += sprintf(about_json_buf + pos, " [ONFI v%d ID %02x:%02x:%02x:%02x]", chip->onfi_version,
									qd->id & 0xff, (qd->id >> 8) & 0xff, (qd->id >> 16) & 0xff, (qd->id >> 24) & 0xff);
						} else
#endif
						pos += sprintf(about_json_buf + pos, " [ONFI v%d ID %02x]", chip->onfi_version, chip->onfi_params.jedec_id);
					}
					else if (chip && chip->jedec_version)
						pos += sprintf(about_json_buf + pos, " [JEDEC v%d ID:%02x:%02x:%02x]", chip->jedec_version,
							chip->jedec_params.jedec_id[0], chip->jedec_params.jedec_id[1], chip->jedec_params.jedec_id[2]);
					else if (chip && chip->cmdfunc && chip->read_byte) {
						u8 mid, did;
						chip->select_chip(mtd, 0);
						chip->cmdfunc(mtd, NAND_CMD_READID, 0x00, -1);
						mid = chip->read_byte(mtd);
						did = chip->read_byte(mtd);
						if (mid || did)
							pos += sprintf(about_json_buf + pos, " [ID %02x:%02x]", mid, did);
					}
#ifdef CONFIG_QPIC_NAND
					else if (chip && chip->priv) {
						struct { unsigned id; unsigned type;
							unsigned vendor; unsigned device; } *qd = chip->priv;
						if (qd->id) {
							extern struct nand_manufacturers nand_manuf_ids[];
							extern struct nand_flash_dev nand_flash_ids[];
							struct nand_manufacturers *mfr;
							struct nand_flash_dev *fdev;
							const char *mfr_name = NULL;
							const char *dev_name = NULL;
							u8 vid = qd->id & 0xff;
							u8 did = (qd->id >> 8) & 0xff;
							for (mfr = nand_manuf_ids; mfr->id; mfr++) {
								if (mfr->id == vid) {
									mfr_name = mfr->name;
									break;
								}
							}
							for (fdev = nand_flash_ids; fdev->name; fdev++) {
								if (fdev->id[0] == vid &&
									fdev->id[1] == did &&
									fdev->id_len >= 4) {
									dev_name = fdev->name;
									break;
								}
							}
							if (dev_name)
								pos += sprintf(about_json_buf + pos, " %s", dev_name);
							else if (mfr_name)
								pos += sprintf(about_json_buf + pos, " %s", mfr_name);
							pos += sprintf(about_json_buf + pos, " [ID %02x:%02x:%02x:%02x]", qd->id & 0xff,
								(qd->id >> 8) & 0xff, (qd->id >> 16) & 0xff, (qd->id >> 24) & 0xff);
						}
					}
#endif
#ifdef CONFIG_QPIC_SERIAL
					else {
						extern struct qpic_serial_nand_params *serial_params;
						if (serial_params)
							pos += sprintf(about_json_buf + pos, " [ID %02x:%02x:%02x:%02x]",
								serial_params->id[0], serial_params->id[1], serial_params->id[2], serial_params->id[3]);
					}
#endif
					fc_first = 0;
				}
			}
		}
#endif
#ifdef CONFIG_QCA_MMC
		{
			int i;
			for (i = 0; i < get_mmc_num(); i++) {
				struct mmc *m = find_mmc_device(i);
				if (m && m->has_init && m->capacity > 0) {
					if (!fc_first)
						pos += sprintf(about_json_buf + pos, ", ");
					pos += sprintf(about_json_buf + pos, "%s %s %luMiB",
						m->block_dev.vendor[0] ? m->block_dev.vendor : "emmc",
						m->block_dev.product[0] ? m->block_dev.product : "",
						(unsigned long)(m->capacity >> 20));
					fc_first = 0;
				}
			}
		}
#endif
		pos += sprintf(about_json_buf + pos, "\",");
	}

	{
		int ps_first = 1;
		pos += sprintf(about_json_buf + pos, "\"phy_switch_info\":\"");
#if defined(CONFIG_IPQ40XX)
		{
			struct mii_dev *bus = mdio_get_current_dev();
			if (bus) {
				int phy_addr;
				for (phy_addr = 0; phy_addr <= 6; phy_addr++) {
					int ret;
					u16 id1, id2;
					u32 phy_id;
					const char *name;
					ret = bus->read(bus, phy_addr,
						MDIO_DEVAD_NONE, 2);
					if (ret < 0)
						continue;
					id1 = (u16)ret;
					ret = bus->read(bus, phy_addr,
						MDIO_DEVAD_NONE, 3);
					if (ret < 0)
						continue;
					id2 = (u16)ret;
					phy_id = ((u32)id1 << 16) | id2;
					if (!phy_id || phy_id == 0xFFFFFFFF)
						continue;
					name = phy_lookup(phy_id, phy_c22_qca,
						ARRAY_SIZE(phy_c22_qca));
					if (name)
						pos = phy_emit(pos, &ps_first, name, phy_addr, id1, id2);
				}
			}
		}
#elif defined(CONFIG_IPQ6018) || defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ9574) || defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ5018)
		{
			extern int ipq_mdio_read(int mii_id, int regnum, ushort *data);
			int phy_addr;
			for (phy_addr = 0; phy_addr <= 31; phy_addr++) {
				int ret;
				u16 id1, id2;
				u32 phy_id;
				const char *name;
				ret = ipq_mdio_read(phy_addr, 2, NULL);
				if (ret < 0)
					continue;
				id1 = (u16)ret;
				ret = ipq_mdio_read(phy_addr, 3, NULL);
				if (ret < 0)
					continue;
				id2 = (u16)ret;
				phy_id = ((u32)id1 << 16) | id2;
				if (!phy_id || phy_id == 0xFFFFFFFF)
					continue;
				name = phy_lookup(phy_id, phy_c22_qca,
					ARRAY_SIZE(phy_c22_qca));
				if (!name)
					name = phy_lookup(phy_id, phy_c22_ext,
						ARRAY_SIZE(phy_c22_ext));
				if (name) {
					pos = phy_emit(pos, &ps_first, name, phy_addr, id1, id2);
					continue;
				}
				{
					u16 c45_id1, c45_id2;
					u32 c45_phy_id;
					c45_id1 = ipq_mdio_read(phy_addr,
						(1 << 30) | (1 << 16) | 2, NULL);
					c45_id2 = ipq_mdio_read(phy_addr,
						(1 << 30) | (1 << 16) | 3, NULL);
					c45_phy_id = ((u32)c45_id1 << 16) | c45_id2;
					if (!c45_phy_id || c45_phy_id == 0xFFFFFFFF)
						continue;
					name = phy_lookup(c45_phy_id, phy_c45_aq,
						ARRAY_SIZE(phy_c45_aq));
					if (name)
						pos = phy_emit(pos, &ps_first, name, phy_addr, c45_id1, c45_id2);
				}
			}
		}
#elif defined(CONFIG_IPQ806X)
		{
			struct mii_dev *bus = mdio_get_current_dev();
			if (bus) {
				int phy_addr;
				for (phy_addr = 0; phy_addr <= 4; phy_addr++) {
					int ret;
					u16 id1, id2;
					u32 phy_id;
					const char *name;
					ret = bus->read(bus, phy_addr,
						MDIO_DEVAD_NONE, 2);
					if (ret < 0)
						continue;
					id1 = (u16)ret;
					ret = bus->read(bus, phy_addr,
						MDIO_DEVAD_NONE, 3);
					if (ret < 0)
						continue;
					id2 = (u16)ret;
					phy_id = ((u32)id1 << 16) | id2;
					if (!phy_id || phy_id == 0xFFFFFFFF)
						continue;
					name = phy_lookup(phy_id, phy_c22_qca,
						ARRAY_SIZE(phy_c22_qca));
					if (name)
						pos = phy_emit(pos, &ps_first, name, phy_addr, id1, id2);
				}
			}
		}
#endif
		pos += sprintf(about_json_buf + pos, "\",");
	}

	pos += sprintf(about_json_buf + pos, "\"gpios\":[");
	{
		int rk_gpio = env_reset_gpio();
		struct about_gpio_ctx ctx = { about_json_buf, &pos, &(int){1}, rk_gpio };
		fdt_list_gpio("/tlmm-gpio", "", about_gpio_cb, &ctx);
		if (rk_gpio >= 0) {
			int rk_value = gpio_get_value(rk_gpio);
			about_gpio_cb(rk_gpio, "reset_key", "in", rk_value, "key_gpio", &ctx);
		}
	}
	pos += sprintf(about_json_buf + pos, "]}");

	hdr_len = sprintf(hdr, "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", pos);
	memmove(about_json_buf + hdr_len, about_json_buf, pos);
	memcpy(about_json_buf, hdr, hdr_len);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)about_json_buf;
	hs->upload = hdr_len + pos;
	httpd_send_data(hs);
}

static void httpd_handle_led(struct failsafe_httpd_state *hs, char *data) {
	char *q = strchr(&data[4], '?'), name[64] = "", action[16] = "";
	static char resp[128];
	int len, alen;
	if (q) {
		char *p = q + 1, *amp, *sp;
		if (strncmp(p, "name=", 5) == 0) {
			p += 5;
			amp = strchr(p, '&');
			sp = strchr(p, ' ');
			if (amp && (!sp || amp < sp)) { memcpy(name, p, amp - p); name[amp - p] = '\0'; p = amp + 1; }
			else if (sp) { memcpy(name, p, sp - p); name[sp - p] = '\0'; p = sp; }
			else { strncpy(name, p, sizeof(name) - 1); name[sizeof(name) - 1] = '\0'; p += strlen(p); }
		}
		if (strncmp(p, "action=", 7) == 0) {
			p += 7;
			sp = strchr(p, ' ');
			amp = strchr(p, '&');
			char *end = sp;
			if (amp && (!end || amp < end)) end = amp;
			if (end) alen = (int)(end - p);
			else { for (alen = 0; p[alen] && p[alen] != ' ' && p[alen] != '\r' && p[alen] != '\n'; alen++); }
			if (alen > (int)sizeof(action) - 1) alen = sizeof(action) - 1;
			memcpy(action, p, alen);
			action[alen] = '\0';
		}
	}
	if (name[0] && action[0]) {
		if (strcmp(action, "on") == 0) led_on(name);
		else if (strcmp(action, "off") == 0) led_off(name);
		else if (strcmp(action, "toggle") == 0) led_toggle(name);
	}
	len = sprintf(resp, "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nok");
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)resp;
	hs->upload = len;
	httpd_send_data(hs);
}

static void httpd_handle_btn_detect(struct failsafe_httpd_state *hs) {
	static char resp[4096];
	int pos = 0, hdr_len, len, first = 1, i, resp_size = sizeof(resp) - 256;
	char hdr[128];

	pos += sprintf(resp + pos, "[");
	{
		for (i = 0; i < GPIO_MAX; i++) {
			unsigned int cfg;
			if (pos + 32 > resp_size)
				break;
			cfg = readl(GPIO_CONFIG_ADDR(i));
			if ((cfg & 0x1C) || (cfg & (1 << 9)))
				continue;
			if (!first)
				pos += sprintf(resp + pos, ", ");
			pos += sprintf(resp + pos, "{\"gpio\":%d,\"value\":%d}", i, gpio_get_value(i));
			first = 0;
		}
	}
	pos += sprintf(resp + pos, "]");

	hdr_len = sprintf(hdr, "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", pos);
	len = hdr_len + pos;
	memmove(resp + hdr_len, resp, pos);
	memcpy(resp, hdr, hdr_len);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)resp;
	hs->upload = len;
	httpd_send_data(hs);
}

static void httpd_handle_env_set(struct failsafe_httpd_state *hs, char *data, int data_len) {
	static char resp[128];
	int len, ok = 0;
	char *body = strstr(data, "\r\n\r\n");
	if (body) {
		body += 4;
		char *amp = strchr(body, '&');
		char name[64] = "", value[128] = "";
		char *eq = strchr(body, '=');
		if (eq && eq < (amp ? amp : body + strlen(body))) {
			memcpy(name, body, eq - body);
			name[eq - body] = '\0';
			if (amp)
				memcpy(value, eq + 1, amp - eq - 1);
			else
				strcpy(value, eq + 1);
			if (strcmp(name, "reset_key") == 0 || strcmp(name, "config_name") == 0) {
				setenv(name, value[0] ? value : NULL);
				saveenv();
				if (strcmp(name, "reset_key") == 0)
					env_reset_gpio_invalidate();
				ok = 1;
			}
		}
	}
	len = sprintf(resp, "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s", ok ? "ok" : "error");
	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)resp;
	hs->upload = len;
	httpd_send_data(hs);
}

static void httpd_handle_partitions(struct failsafe_httpd_state *hs) {
	int i, pos = 0, hdr_len, count = 0, smem_count;
	char name[SMEM_PTN_NAME_MAX], hdr[128];
	uint32_t start, size, flash_type;
	uint32_t bsize;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
#ifdef CONFIG_QCA_MMC
	int gpt_count;
	block_dev_desc_t *blk_dev = NULL;
	disk_partition_t disk_info;
#endif

	get_current_flash_type(&flash_type);
	bsize = sfi->flash_block_size;

	pos += sprintf(part_json_buf + pos, "{\"parts\":[");

#ifdef CONFIG_QCA_MMC
	if (flash_type == SMEM_BOOT_MMC_FLASH) {
		blk_dev = mmc_get_dev(mmc_host.dev_num);
		if (blk_dev != NULL) {
			pos += sprintf(part_json_buf + pos,
				"{\"name\":\"0:GPT\",\"start\":0,\"size\":%llu,\"flash\":\"emmc\"}",
				(unsigned long long)34 * (unsigned long long)blk_dev->blksz);
			count++;
			gpt_count = get_partition_count_efi(blk_dev);
			for (i = 1; i <= gpt_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
				if (get_partition_info_efi(blk_dev, i, &disk_info) == 0) {
					pos += sprintf(part_json_buf + pos,
						",{\"name\":\"%s\",\"start\":%llu,\"size\":%llu,\"flash\":\"emmc\"}",
						disk_info.name,
						(unsigned long long)disk_info.start * (unsigned long long)blk_dev->blksz,
						(unsigned long long)disk_info.size * (unsigned long long)blk_dev->blksz);
					count++;
				}
			}
			pos += sprintf(part_json_buf + pos,
				",{\"name\":\"0:GPTBACKUP\",\"start\":%llu,\"size\":%llu,\"flash\":\"emmc\"}",
				(unsigned long long)(blk_dev->lba - 33) * (unsigned long long)blk_dev->blksz,
				(unsigned long long)33 * (unsigned long long)blk_dev->blksz);
			count++;
		}
	} else
#endif
	{
		smem_count = smem_getpart_count();
		for (i = 0; i < smem_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
			if (smem_getpart_by_index(i, name, sizeof(name), &start, &size) == 0) {
				const char *pflash = "nor";
#ifdef CONFIG_CMD_NAND
				if (get_which_flash_param(name))
					pflash = "nand";
#endif
				pos += sprintf(part_json_buf + pos,
					"%s{\"name\":\"%s\",\"start\":%lu,\"size\":%lu,\"flash\":\"%s\"}",
					(count > 0 ? "," : ""), name,
					(unsigned long)start * (unsigned long)bsize,
					(unsigned long)size, pflash);
				count++;
			}
		}
#ifdef CONFIG_QCA_MMC
		if ((sfi->flash_type == SMEM_BOOT_SPI_FLASH || flash_type == SMEM_BOOT_NORPLUSEMMC) &&
			(sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH ||
			 sfi->rootfs.offset == 0xBAD0FF5E || flash_type == SMEM_BOOT_NORPLUSEMMC)) {
			blk_dev = mmc_get_dev(mmc_host.dev_num);
			if (blk_dev != NULL) {
				pos += sprintf(part_json_buf + pos,
					"%s{\"name\":\"0:GPT\",\"start\":0,\"size\":%llu,\"flash\":\"emmc\"}",
					(count > 0 ? "," : ""),
					(unsigned long long)34 * (unsigned long long)blk_dev->blksz);
				count++;
				gpt_count = get_partition_count_efi(blk_dev);
				for (i = 1; i <= gpt_count && pos < PART_JSON_BUF_SIZE - 80; i++) {
					if (get_partition_info_efi(blk_dev, i, &disk_info) == 0) {
						pos += sprintf(part_json_buf + pos,
						",{\"name\":\"%s\",\"start\":%llu,\"size\":%llu,\"flash\":\"emmc\"}",
						disk_info.name,
						(unsigned long long)disk_info.start * (unsigned long long)blk_dev->blksz,
						(unsigned long long)disk_info.size * (unsigned long long)blk_dev->blksz);
					count++;
					}
				}
				pos += sprintf(part_json_buf + pos,
					",{\"name\":\"0:GPTBACKUP\",\"start\":%llu,\"size\":%llu,\"flash\":\"emmc\"}",
					(unsigned long long)(blk_dev->lba - 33) * (unsigned long long)blk_dev->blksz,
					(unsigned long long)33 * (unsigned long long)blk_dev->blksz);
				count++;
			}
		}
#endif
	}

	pos += sprintf(part_json_buf + pos, "],\"has_spi\":%s,\"spi_size\":%lu,\"has_nand\":%s,\"nand_size\":%lu,\"nand_raw_size\":%lu,\"ram_available\":%lu,\"has_emmc\":%s,\"emmc_size\":%llu,\"nand_type\":\"%s\"}",
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
		,(unsigned long long)(blk_dev ? (unsigned long long)blk_dev->lba * (unsigned long long)blk_dev->blksz : 0ULL)
#else
		,"false",0ULL
#endif
#ifdef CONFIG_CMD_NAND
		,(nand_info[0].size > 0 ? "parallel" : (CONFIG_SYS_MAX_NAND_DEVICE > 1 && nand_info[1].size > 0 ? "spi" : "parallel"))
#else
		,"none"
#endif
	);

	hdr_len = sprintf(hdr, "HTTP/1.0 200 OK\r\n" "Content-Type: application/json\r\n" "Content-Length: %d\r\n" "Connection: close\r\n\r\n", pos);

	memmove(part_json_buf + hdr_len, part_json_buf, pos);
	memcpy(part_json_buf, hdr, hdr_len);

	hs->state = STATE_FILE_REQUEST;
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len + pos;
	httpd_send_data(hs);
}

static void httpd_handle_backup(struct failsafe_httpd_state *hs, char *data, int data_len) {
	char *query = strchr(&data[4], '?'), part_name[64], filename[96], *size_param, *amp;
	ulong offset, size;
	int hdr_len, raw = 0;
	u32_t ram_avail;
	u64 total_size = 0;

	if (!query || strncmp(query + 1, "part=", 5) != 0) {
		static const char *err = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\nMissing partition";
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)err;
		hs->upload = strlen(err);
		httpd_send_data(hs);
		return;
	}

	strncpy(part_name, query + 6, sizeof(part_name) - 1);
	part_name[sizeof(part_name) - 1] = '\0';
	str_trim_crlf(part_name);
	url_decode(part_name);

	amp = strchr(part_name, '&');
	if (amp) *amp = '\0';

	if (strstr(query, "raw=1"))
		raw = 1;

	size_param = strstr(query, "size=");
	if (size_param)
		total_size = atoi_local(size_param + 5);

	printf("Backup request: %s%s size [%llu.%02llu MiB | %llu bytes]\n",
		part_name, raw ? " (raw)" : "",
		mib_int(total_size), mib_frac(total_size),
		total_size);

	if (total_size == 0) {
		static const char *err = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\nMissing size";
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)err;
		hs->upload = strlen(err);
		httpd_send_data(hs);
		return;
	}

	ram_avail = (u32_t)CONFIG_SYS_SDRAM_END - (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
	backup.chunked = (total_size > ram_avail) ? 1 : 0;
	backup.raw = raw;
	backup.total_size = total_size;
	strncpy(backup.part_name, part_name, sizeof(backup.part_name) - 1);
	backup.part_name[sizeof(backup.part_name) - 1] = '\0';
	flashread_yield_fn = flashread_yield;

	if (backup.chunked) {
		backup.total_chunks = (int)((total_size + ram_avail - 1) / ram_avail);
		backup.chunk_num = 1;
		printf("Backup: %llu.%02llu MiB > RAM %u.%02u MiB %d chunks transfer\n", mib_int(total_size), mib_frac(total_size),
			(u32)mib_int(ram_avail), (u32)mib_frac(ram_avail), backup.total_chunks);
		char chunk_detail[32] = "";
		if (flashread_partition_chunk(part_name, WEBFAILSAFE_UPLOAD_RAM_ADDRESS, 0, ram_avail, raw, NULL, &size, chunk_detail) != CMD_RET_SUCCESS) {
			static const char *err = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\nRead failed";
			hs->state = STATE_FILE_REQUEST;
			hs->dataptr = (u8_t *)err;
			hs->upload = strlen(err);
			httpd_send_data(hs);
			return;
		}
		backup.data_addr = (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
		backup.data_size = (u32_t)size;
		backup.chunk_offset = (u64)size;
		backup.total_sent = (u64)size;
		backup.total_remaining = backup.total_size - backup.total_sent;
		printf("Backup: chunk %d/%d read %llu.%02llu MiB [0x%x | %s] remaining %llu.%02llu MiB\n",
			   backup.chunk_num, backup.total_chunks,
			   mib_int(backup.total_sent), mib_frac(backup.total_sent),
			   backup.data_size, chunk_detail,
			   mib_int(backup.total_remaining), mib_frac(backup.total_remaining));
		backup.chunk_num++;
	} else {
		if (flashread_partition(part_name, WEBFAILSAFE_UPLOAD_RAM_ADDRESS, 0, raw, &offset, &size) != CMD_RET_SUCCESS) {
			static const char *err = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\n\r\nRead failed";
			hs->state = STATE_FILE_REQUEST;
			hs->dataptr = (u8_t *)err;
			hs->upload = strlen(err);
			httpd_send_data(hs);
			return;
		}
		backup.data_addr = (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
		backup.data_size = (u32_t)size;
		backup.total_remaining = 0;
		backup.total_sent = 0;
		backup.chunk_offset = 0;
	}

	httpd_poll_wait(1);

	sprintf(filename, "%s%s.bin", part_name, raw ? "_oob" : "");
	hdr_len = sprintf(part_json_buf, "HTTP/1.0 200 OK\r\n" "Content-Type: application/octet-stream\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n" "Content-Length: %llu\r\n" "Connection: close\r\n\r\n", filename, total_size);

	hs->state = STATE_FILE_REQUEST;
	hs->owns_global = 1;
	hs_global = hs;
	tcp_setprio(hs->pcb, TCP_PRIO_NORMAL);
	hs->dataptr = (u8_t *)part_json_buf;
	hs->upload = hdr_len;
	httpd_send_data(hs);

	backup.sending_header = 1;
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
		httpd_conn_abort(hs, hs->pcb);
		return;
	}

	{
		char *q = strstr((char *)&data[4], "?");
		if (q) *q = 0;
	}

	if (!strstr(&data[4], ".css") && !strstr(&data[4], ".js") && !strstr(&data[4], ".svg") && !strstr(&data[4], ".ico"))
		printf("GET: %s\n", &data[4]);

	if (data[4] == ISO_slash && data[5] == 0) {
		fs_open(file_index_html[0].name, &fsfile);
	} else {
		if (!fs_open((const char *)&data[4], &fsfile)) {
			if (!strstr(&data[4], "favicon.ico"))
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

static void backup_chunk_next(void) {
	u32_t ram_avail = (u32_t)CONFIG_SYS_SDRAM_END - (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
		  chunk_size = (backup.total_remaining > ram_avail) ? ram_avail : (u32)backup.total_remaining;
	ulong rd_size;
	char chunk_detail[32] = "";

	if (flashread_partition_chunk(backup.part_name, WEBFAILSAFE_UPLOAD_RAM_ADDRESS, backup.chunk_offset, chunk_size, backup.raw, NULL, &rd_size, chunk_detail) == CMD_RET_SUCCESS && rd_size > 0) {
		backup.data_addr = (u32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
		backup.data_size = (u32_t)rd_size;
		backup.chunk_offset += backup.data_size;
		backup.total_sent += backup.data_size;
		if (backup.total_sent > backup.total_size) {
			backup.data_size -= (u32_t)(backup.total_sent - backup.total_size);
			backup.total_sent = backup.total_size;
		}
		backup.total_remaining = backup.total_size - backup.total_sent;
		printf("Backup: chunk %d/%d read %llu.%02llu MiB [0x%x | %s] remaining %llu.%02llu MiB\n",
			   backup.chunk_num, backup.total_chunks,
			   mib_int(backup.total_sent), mib_frac(backup.total_sent),
			   backup.data_size, chunk_detail,
			   mib_int(backup.total_remaining), mib_frac(backup.total_remaining));
		backup.chunk_num++;
		httpd_poll_wait(2);
		if (hs_global) {
			hs_global->dataptr = (u8_t *)(uintptr_t)backup.data_addr;
			hs_global->upload = backup.data_size;
			httpd_send_data(hs_global);
		}
		backup.chunk_busy = 0;
	} else {
		printf("Backup: chunk failed at offset %llu.%02llu MiB\n",
			mib_int(backup.chunk_offset), mib_frac(backup.chunk_offset));
		backup.chunk_busy = 0;
		backup.chunked = 0;
		backup.total_remaining = 0;
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

	if (backup.sending_header && hs->upload <= 0) {
		backup.sending_header = 0;
		hs->state = STATE_FILE_REQUEST;
		hs->dataptr = (u8_t *)(uintptr_t)backup.data_addr;
		hs->upload = backup.data_size;
	}

	if (hs->upload <= 0) {
		if (backup.chunked && backup.total_remaining > 0 && !backup.chunk_busy) {
			backup.chunk_busy = 1;
			return ERR_OK;
		}
		if (backup.chunk_busy)
			return ERR_OK;
		if (upload.done) {
			if (!upload.failed)
				webfailsafe_ready_for_upgrade = 1;
			upload.done = 0;
			upload.failed = 0;
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
	int data_len, need_free = 0, offset;
	struct pbuf *q;

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

	data = malloc(p->tot_len + 1);
	if (!data) {
		pbuf_free(p);
		httpd_conn_abort(hs, pcb);
		return ERR_ABRT;
	}
	offset = 0;
	for (q = p; q; q = q->next) {
		memcpy(data + offset, q->payload, q->len);
		offset += q->len;
	}
	data_len = p->tot_len;
	data[data_len] = '\0';
	need_free = 1;

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
			if (strncmp(&data[4], "/about", 6) == 0 && data[10] == ISO_space) {
				httpd_handle_about(hs);
				break;
			}
			if (strncmp(&data[4], "/led?", 5) == 0) {
				httpd_handle_led(hs, data);
				break;
			}
			if (strncmp(&data[4], "/btn_detect", 11) == 0 && data[15] == ISO_space) {
				httpd_handle_btn_detect(hs);
				break;
			}
			httpd_handle_file_request(hs, data, data_len);
		} else if (strncmp(data, "POST", 4) == 0 && is_http_method_separator(data[4])) {
			if (strncmp(&data[5], "/webterm", 8) == 0) {
				webterm_http_handler(hs, data, data_len);
				break;
			}
			if (strncmp(&data[5], "/env_set", 8) == 0) {
				httpd_handle_env_set(hs, data, data_len);
				break;
			}
			data[data_len] = '\0';
			if (httpd_parse_content_length(hs, data) < 0)
				return httpd_recv_abort(hs, pcb, data, need_free, p);
			hs->state = STATE_UPLOAD_REQUEST;
			hs->owns_global = 1;
			hs_global = hs;
			tcp_setprio(pcb, TCP_PRIO_NORMAL);
			led_off("blink_led");
			if (httpd_parse_boundary(data) < 0 || httpd_init_upload_ram() < 0)
				return httpd_recv_abort(hs, pcb, data, need_free, p);
			if (httpd_findandstore_firstchunk(hs, data, data_len)) {
				upload.data_start_found = 1;
				if (httpd_check_upload_size(hs) < 0)
					return httpd_recv_abort(hs, pcb, data, need_free, p);
				httpd_check_upload_complete(hs);
			} else {
				upload.data_start_found = 0;
			}
		} else {
			return httpd_recv_abort(hs, pcb, data, need_free, p);
		}
		break;

	case STATE_UPLOAD_REQUEST:
		if (!upload.data_start_found) {
			data[data_len] = '\0';
			if (!httpd_findandstore_firstchunk(hs, data, data_len)) {
				print_error("couldn't find start of data in next packet!");
				return httpd_recv_abort(hs, pcb, data, need_free, p);
			}
			upload.data_start_found = 1;
			if (httpd_check_upload_size(hs) < 0)
				return httpd_recv_abort(hs, pcb, data, need_free, p);
			httpd_check_upload_complete(hs);
		} else {
			hs->upload += data_len;
			if (!upload.failed)
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
		httpd_conn_abort(hs, pcb);
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

static int httpd_progress_start_done = 0;
static int eth_init_attempted = 0;
static ulong periodic_timer = 0;

static void abort_port_pcb(struct tcp_pcb **list) {
	struct tcp_pcb *pcb, *next;
	for (pcb = *list; pcb != NULL; pcb = next) {
		next = pcb->next;
		if (pcb->local_port == 80) {
			tcp_arg(pcb, NULL);
			tcp_err(pcb, NULL);
			tcp_recv(pcb, NULL);
			tcp_sent(pcb, NULL);
			tcp_poll(pcb, NULL, 0);
			tcp_abort(pcb);
		}
	}
}

void failsafe_httpd_stop(void) {
	if (listen_pcb != NULL) {
		tcp_close(listen_pcb);
		listen_pcb = NULL;
	}
	abort_port_pcb(&tcp_active_pcbs);
	abort_port_pcb(&tcp_tw_pcbs);
	hs_global = NULL;
	netif_remove(&failsafe_netif);
	httpd_progress_start_done = 0;
	eth_init_attempted = 0;
	periodic_timer = 0;
}

static int lwip_initialized = 0;

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
static void ppe_arp_kickstart(void);
#endif

void failsafe_lwip_init(struct ip4_addr *ipaddr, struct ip4_addr *netmask, struct ip4_addr *gw) {
	if (!lwip_initialized) {
		lwip_init();
		lwip_initialized = 1;
	}

	netif_add(&failsafe_netif, ipaddr, netmask, gw, NULL,
		  ethernetif_init, ethernet_input);
	netif_set_default(&failsafe_netif);
	netif_set_up(&failsafe_netif);
	netif_set_link_up(&failsafe_netif);

	failsafe_httpd_init();

#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	ppe_arp_kickstart();
#endif
}

void failsafe_httpd_poll(void) {
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
		}
	}
#if defined(CONFIG_IPQ5332) || defined(CONFIG_IPQ9574)
	else if (link_changed > 0) {
		ppe_arp_kickstart();
	}
#endif

	if (!httpd_progress_start_done && eth_is_active(eth_get_dev())) {
		do_http_progress(WEBFAILSAFE_PROGRESS_START);
		httpd_progress_start_done = 1;
	}

	if (backup.chunked && backup.chunk_busy && backup.total_remaining > 0)
		backup_chunk_next();

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