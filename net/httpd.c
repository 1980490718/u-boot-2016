/*
 *	Copyright 1994, 1995, 2000 Neil Russell.
 *	(See License)
 *	Copyright 2000, 2001 DENX Software Engineering, Wolfgang Denk, wd@denx.de
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <asm/byteorder.h>
#include "httpd.h"
#include "../httpd/uipopt.h"
#include "../httpd/uip.h"
#include "../httpd/uip_arp.h"
#include <ipq_api.h>
#include <asm/gpio.h>
#ifdef CONFIG_DHCPD
#include "dhcpd.h"
#endif

static int do_firmware_upgrade(const ulong size);
static int do_uboot_upgrade(const ulong size);
static int do_art_upgrade(const ulong size);
static int do_gpt_upgrade(const ulong size);
static int do_cdt_upgrade(const ulong size);
static int do_mibib_upgrade(const ulong size);
static int execute_command(const char *cmd);
static void print_upgrade_warning(const char *upgrade_type);

static int arptimer = 0;
struct in_addr net_httpd_ip;
void HttpdStart(void) {
#ifdef CONFIG_DHCPD
	dhcpd_ip_settings();
	mdelay(1500);
	dhcpd_request_nonblocking();
	mdelay(500);
	dhcpd_poll_server();
	mdelay(1500);
	printf("Starting HTTP server with DHCP\n");

	struct uip_eth_addr eaddr;
	unsigned short int ip[2];
	ulong tmp_ip_addr = ntohl(dhcpd_svr_cfg.server_ip.s_addr);
	printf("Starting HTTP server at IP: %ld.%ld.%ld.%ld\n",
		   (tmp_ip_addr & 0xff000000) >> 24,
		   (tmp_ip_addr & 0x00ff0000) >> 16,
		   (tmp_ip_addr & 0x0000ff00) >> 8,
		   (tmp_ip_addr & 0x000000ff));
	eaddr.addr[0] = net_ethaddr[0];
	eaddr.addr[1] = net_ethaddr[1];
	eaddr.addr[2] = net_ethaddr[2];
	eaddr.addr[3] = net_ethaddr[3];
	eaddr.addr[4] = net_ethaddr[4];
	eaddr.addr[5] = net_ethaddr[5];
	uip_setethaddr(eaddr);
	uip_init();
	httpd_init();
	ip[0] = htons((tmp_ip_addr & 0xFFFF0000) >> 16);
	ip[1] = htons(tmp_ip_addr & 0x0000FFFF);
	uip_sethostaddr(ip);
	ip[0] = htons((ntohl(dhcpd_svr_cfg.netmask.s_addr) & 0xFFFF0000) >> 16);
	ip[1] = htons(ntohl(dhcpd_svr_cfg.netmask.s_addr) & 0x0000FFFF);
	net_netmask.s_addr = dhcpd_svr_cfg.netmask.s_addr;
	uip_setnetmask(ip);
#else
	struct uip_eth_addr eaddr;
	unsigned short int ip[2];
	ulong tmp_ip_addr = ntohl(net_ip.s_addr);

	printf("Starting HTTP server at IP: %ld.%ld.%ld.%ld\n",
		   (tmp_ip_addr & 0xff000000) >> 24,
		   (tmp_ip_addr & 0x00ff0000) >> 16,
		   (tmp_ip_addr & 0x0000ff00) >> 8,
		   (tmp_ip_addr & 0x000000ff));

	eaddr.addr[0] = net_ethaddr[0];
	eaddr.addr[1] = net_ethaddr[1];
	eaddr.addr[2] = net_ethaddr[2];
	eaddr.addr[3] = net_ethaddr[3];
	eaddr.addr[4] = net_ethaddr[4];
	eaddr.addr[5] = net_ethaddr[5];
	uip_setethaddr(eaddr);

	uip_init();
	httpd_init();

	ip[0] = htons((tmp_ip_addr & 0xFFFF0000) >> 16);
	ip[1] = htons(tmp_ip_addr & 0x0000FFFF);

	uip_sethostaddr(ip);

	u16_t hostaddr0 = ntohs(uip_hostaddr[0]);
	u16_t hostaddr1 = ntohs(uip_hostaddr[1]);
	u8_t byte1 = (hostaddr0 >> 8) & 0xff;
	u8_t byte2 = hostaddr0 & 0xff;
	u8_t byte3 = (hostaddr1 >> 8) & 0xff;
	u8_t byte4 = hostaddr1 & 0xff;

	printf("Host IP set to: %d.%d.%d.%d\n", byte1, byte2, byte3, byte4);

	ip[0] = htons((0xFFFFFF00 & 0xFFFF0000) >> 16);
	ip[1] = htons(0xFFFFFF00 & 0x0000FFFF);

	net_netmask.s_addr = 0xFFFFFF00;
	uip_setnetmask(ip);
#endif
	do_http_progress(WEBFAILSAFE_PROGRESS_START);
	webfailsafe_is_running = 1;
}

static void reset_webfailsafe_state(void) {
	webfailsafe_is_running = 0;
	webfailsafe_ready_for_upgrade = 0;
	webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE;
}

void HttpdStop(void) {
	reset_webfailsafe_state();
	do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_FAILED);
}

void HttpdDone(void) {
	reset_webfailsafe_state();
	do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_READY);
}

static int execute_command(const char *cmd) {
	printf("Executing: %s\n", cmd);
	return run_command(cmd, 0);
}

static void print_upgrade_warning(const char *upgrade_type) {
	printf("\n******************************\n     %s UPGRADING     \n DO NOT POWER OFF DEVICE! \n******************************\n", upgrade_type);
}

#ifdef CONFIG_MD5
#include <u-boot/md5.h>
void printChecksumMd5(int address, unsigned int size) {
	void *buf = (void *)(address);
	u8 output[16];
	md5_wd(buf, size, output, CHUNKSZ_MD5);
	printf("The md5sum from 0x%08x to 0x%08x is: ", address, address + size);
	for (int i = 0; i < 16; i++) printf("%02x", output[i] & 0xFF);
}
#else
void printChecksumMd5(int address, unsigned int size) {}
#endif

static const char *fw_type_to_string(int fw_type) {
	switch (fw_type) {
		case FW_TYPE_NOR: return "NOR";
		case FW_TYPE_GPT: return "GPT";
		case FW_TYPE_QSDK: return "QSDK";
		case FW_TYPE_UBI: return "UBI";
		case FW_TYPE_CDT: return "CDT";
		case FW_TYPE_ELF: return "ELF";
		case FW_TYPE_MIBIB: return "MIBIB";
		default: return "UNKNOWN";
	}
}

int do_http_upgrade(const ulong size, const int upgrade_type) {
	printChecksumMd5(UPLOAD_ADDR, size);
	switch (upgrade_type) {
		case WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE: return do_firmware_upgrade(size);
		case WEBFAILSAFE_UPGRADE_TYPE_UBOOT: return do_uboot_upgrade(size);
		case WEBFAILSAFE_UPGRADE_TYPE_ART: return do_art_upgrade(size);
		case WEBFAILSAFE_UPGRADE_TYPE_IMG: return do_gpt_upgrade(size);
		case WEBFAILSAFE_UPGRADE_TYPE_CDT: return do_cdt_upgrade(size);
		case WEBFAILSAFE_UPGRADE_TYPE_MIBIB: return do_mibib_upgrade(size);
		default: printf("\n* Unsupported upgrade type *\n");
			return -1;
	}
}

static int do_firmware_upgrade(const ulong size) {
	char buf[576];
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_MMC: {
			int fw_type = check_fw_type((void *)UPLOAD_ADDR);
			if (fw_type == FW_TYPE_NOR) {
				print_upgrade_warning("FIRMWARE");
				sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:HLOS 0x%lx 0x600000 && flash rootfs 0x%lx 0x%lx && mmc read 0x%lx 0x622 0x200 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && flash 0:BOOTCONFIG 0x%lx 0x40000 && flash 0:BOOTCONFIG1 0x%lx 0x40000",
						UPLOAD_ADDR + size, UPLOAD_ADDR, UPLOAD_ADDR + 0x600000, (size - 0x600000), UPLOAD_ADDR, UPLOAD_ADDR + 0x80, UPLOAD_ADDR + 0x94, UPLOAD_ADDR + 0xA8, UPLOAD_ADDR, UPLOAD_ADDR);
			} else if (fw_type == FW_TYPE_QSDK) {
				print_upgrade_warning("FIRMWARE");
				sprintf(buf, "imxtract 0x%lx %s && mmc dev 0 && flash 0:HLOS $fileaddr $filesize && imxtract 0x%lx %s && flash rootfs $fileaddr $filesize && imxtract 0x%lx %s && flash 0:WIFIFW $fileaddr $filesize && flasherase rootfs_data && mmc read 0x%lx 0x622 0x200 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && flash 0:BOOTCONFIG 0x%lx 0x40000 && flash 0:BOOTCONFIG1 0x%lx 0x40000",
						UPLOAD_ADDR, HLOS_NAME, UPLOAD_ADDR, ROOTFS_NAME, UPLOAD_ADDR, WIFIFW_NAME, UPLOAD_ADDR, UPLOAD_ADDR + 0x80, UPLOAD_ADDR + 0x94, UPLOAD_ADDR + 0xA8, UPLOAD_ADDR, UPLOAD_ADDR);
			} else {
				printf("\n* Unsupported FIRMWARE type *\n");
				return -1;
			}
			break;
		}
		case FLASH_TYPE_NAND:
		case FLASH_TYPE_SPI:
		case FLASH_TYPE_NOR:
		default: {
			int fw_type = check_fw_type((void *)UPLOAD_ADDR);
			if (fw_type == FW_TYPE_NOR || fw_type == FW_TYPE_QSDK || fw_type == FW_TYPE_UBI) {
				print_upgrade_warning("FIRMWARE");
				if (fw_type == FW_TYPE_NOR) {
					sprintf(buf, "sf probe && sf update 0x%lx 0x%lx 0x%lx", UPLOAD_ADDR, FIRMWARE_START_ADDR_NOR, size);
				} else if (fw_type == FW_TYPE_QSDK) {
					sprintf(buf, "sf probe; imgaddr=0x%lx && source $imgaddr:script", UPLOAD_ADDR);
				} else { // fw_type == FW_TYPE_UBI
#if defined(CONFIG_IPQ807X_AP8220)
					sprintf(buf, "nand device 0 && nand erase.chip && flash %s 0x%lx $filesize", ROOTFS_NAME0, UPLOAD_ADDR);
#elif defined(CONFIG_IPQ807X_AX6) || defined(CONFIG_IPQ6018_M2) || defined(CONFIG_IPQ6018_360V6)
					sprintf(buf, "flash %s 0x%lx $filesize", ROOTFS_NAME0, UPLOAD_ADDR);
#elif defined(CONFIG_IPQ807X_XGLINK_5GCPE)
					sprintf(buf, "flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", ROOTFS_NAME0, UPLOAD_ADDR, ROOTFS_NAME1, UPLOAD_ADDR);
#else
					sprintf(buf, "nand erase 0x%lx 0x%lx; nand write 0x%lx 0x%lx 0x%lx", FIRMWARE_START_ADDR_NAND, FIRMWARE_SIZE_NAND, UPLOAD_ADDR, FIRMWARE_START_ADDR_NAND, size);
#endif
				}
			} else {
				printf("\n* Unsupported FIRMWARE type *\n");
				return -1;
			}
			break;
		}
	}
	return execute_command(buf);
}

static int do_uboot_upgrade(const ulong size) {
	char buf[576];
	if (check_fw_type((void *)UPLOAD_ADDR) != FW_TYPE_ELF) {
		printf("\n* Uploaded file is not UBOOT ELF type. Actual type is %s *\n", fw_type_to_string(check_fw_type((void *)UPLOAD_ADDR)));
		return -1;
	}
	print_upgrade_warning("U-BOOT");
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_MMC:
			sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:APPSBL 0x%lx $filesize && flash 0:APPSBL_1 0x%lx $filesize", UPLOAD_ADDR + size, UPLOAD_ADDR, UPLOAD_ADDR);
			break;
		case FLASH_TYPE_NAND:
#if defined(CONFIG_IPQ807X_AX6)
			sprintf(buf, "flash %s 0x%lx $filesize", UBOOT_NAME, UPLOAD_ADDR);
#elif defined(CONFIG_IPQ6018_M2) || defined(CONFIG_IPQ6018_360V6)
			sprintf(buf, "flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", UBOOT_NAME, UPLOAD_ADDR, UBOOT_NAME_1, UPLOAD_ADDR);
#else
			sprintf(buf, "nand erase 0x%lx 0x%lx && nand write 0x%lx 0x%lx 0x%lx",
					UBOOT_START_ADDR_NAND, UBOOT_SIZE_NAND, UPLOAD_ADDR, UBOOT_START_ADDR_NAND, ((size / 131072 + (size % 131072 != 0)) * 131072));
#endif
			break;
		case FLASH_TYPE_SPI:
#if defined(CONFIG_IPQ807X_XGLINK_5GCPE)
			sprintf(buf, "flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", UBOOT_NAME, UPLOAD_ADDR, UBOOT_NAME_1, UPLOAD_ADDR);
#else
			sprintf(buf, "flash %s 0x%lx $filesize", UBOOT_NAME, UPLOAD_ADDR);
#endif
			break;
		default:
			printf("\n* Unsupported flash type for U-Boot *\n");
			return -1;
	}
	return execute_command(buf);
}

static int do_art_upgrade(const ulong size) {
	char buf[576];
	int fw_type = check_fw_type((void *)UPLOAD_ADDR);
	if (fw_type == FW_TYPE_CDT || fw_type == FW_TYPE_ELF || fw_type == FW_TYPE_GPT || fw_type == FW_TYPE_MIBIB) {
		printf("\n* The %s type is not allowed to upgrade to the ART partition *\n", fw_type_to_string(fw_type));
		return -1;
	}
	print_upgrade_warning("ART");
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_MMC:
			sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash %s 0x%lx $filesize", UPLOAD_ADDR + size, ART_NAME, UPLOAD_ADDR);
			break;
		case FLASH_TYPE_NAND:
#if defined(CONFIG_IPQ807X_AX6) || defined(CONFIG_IPQ6018_M2) || defined(CONFIG_IPQ6018_360V6)
			sprintf(buf, "flash %s 0x%lx $filesize", ART_NAME, UPLOAD_ADDR);
#else
			sprintf(buf, "nand erase 0x%lx 0x%lx && nand write 0x%lx 0x%lx 0x%lx", ART_START_ADDR_NAND, ART_SIZE_NAND, UPLOAD_ADDR, ART_START_ADDR_NAND, ((size / 131072 + (size % 131072 != 0)) * 131072));
#endif
			break;
		case FLASH_TYPE_SPI:
			sprintf(buf, "flash %s 0x%lx $filesize", ART_NAME, UPLOAD_ADDR);
			break;
		default:
			printf("\n* Unsupported flash type for ART *\n");
			return -1;
	}
	return execute_command(buf);
}

static int do_gpt_upgrade(const ulong size) {
	char buf[576];
	if (check_fw_type((void *)UPLOAD_ADDR) != FW_TYPE_GPT) {
		printf("\n* Uploaded file is not GPT type. Actual type is %s *\n", fw_type_to_string(check_fw_type((void *)UPLOAD_ADDR)));
		return -1;
	}
	print_upgrade_warning("GPT");
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_MMC:
			sprintf(buf, "mmc dev 0 && mmc erase 0x0 0x%lx && mmc write 0x%lx 0x0 0x%lx", ((size - 1) / 512 + 1), UPLOAD_ADDR, ((size - 1) / 512 + 1));
			break;
		case FLASH_TYPE_NAND:
		case FLASH_TYPE_SPI:
		case FLASH_TYPE_NOR:
		default:
			printf("\n* Flash type %d is not supported for GPT upgrade! Please return and select upgrade type \"mibib\"\n", qca_smem_flash_info.flash_type);
			return -1;
	}
	return execute_command(buf);
}

static int do_cdt_upgrade(const ulong size) {
	char buf[576];
	if (check_fw_type((void *)UPLOAD_ADDR) != FW_TYPE_CDT) {
		printf("\n* Uploaded file is not CDT type. Actual type is %s *\n", fw_type_to_string(check_fw_type((void *)UPLOAD_ADDR)));
		return -1;
	}
	print_upgrade_warning("CDT");
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_MMC:
			sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", UPLOAD_ADDR + size, CDT_NAME, UPLOAD_ADDR, CDT_NAME_1, UPLOAD_ADDR);
			break;
		case FLASH_TYPE_NAND:
#if defined(CONFIG_IPQ807X_AX6)
			sprintf(buf, "flash %s 0x%lx $filesize", CDT_NAME, UPLOAD_ADDR);
#elif defined(CONFIG_IPQ6018_M2) || defined(CONFIG_IPQ6018_360V6)
			sprintf(buf, "flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", CDT_NAME, UPLOAD_ADDR, CDT_NAME_1, UPLOAD_ADDR);
#else
			sprintf(buf, "nand erase 0x%lx 0x%lx && nand write 0x%lx 0x%lx 0x%lx", CDT_START_ADDR_NAND, CDT_SIZE_NAND, UPLOAD_ADDR, CDT_START_ADDR_NAND, ((size / 131072 + (size % 131072 != 0)) * 131072));
#endif
			break;
		case FLASH_TYPE_SPI:
#if defined(CONFIG_IPQ807X_XGLINK_5GCPE)
			sprintf(buf, "flash %s 0x%lx $filesize && flash %s 0x%lx $filesize", CDT_NAME, UPLOAD_ADDR, CDT_NAME_1, UPLOAD_ADDR);
#else
			sprintf(buf, "flash %s 0x%lx $filesize", CDT_NAME, UPLOAD_ADDR);
#endif
			break;
		default:
			printf("\n* Unsupported flash type for CDT *\n");
			return -1;
	}
	return execute_command(buf);
}

static int do_mibib_upgrade(const ulong size) {
	char buf[576];
	if (check_fw_type((void *)UPLOAD_ADDR) != FW_TYPE_MIBIB) {
		printf("\n* Uploaded file is not MIBIB type. Actual type is %s *\n", fw_type_to_string(check_fw_type((void *)UPLOAD_ADDR)));
		return -1;
	}
	print_upgrade_warning("MIBIB");
	switch (qca_smem_flash_info.flash_type) {
		case FLASH_TYPE_NAND:
#if defined(CONFIG_IPQ807X_AX6) || defined(CONFIG_IPQ6018_M2) || defined(CONFIG_IPQ6018_360V6)
			sprintf(buf, "flash %s 0x%lx $filesize", MIBIB_NAME, UPLOAD_ADDR);
#else
			sprintf(buf, "nand erase 0x%lx 0x%lx && nand write 0x%lx 0x%lx 0x%lx", MIBIB_START_ADDR_NAND, MIBIB_SIZE_NAND, UPLOAD_ADDR, MIBIB_START_ADDR_NAND, ((size / 131072 + (size % 131072 != 0)) * 131072));
#endif
			break;
		case FLASH_TYPE_SPI:
			sprintf(buf, "flash %s 0x%lx $filesize", MIBIB_NAME, UPLOAD_ADDR);
			break;
		default:
			printf("\n* Unsupported flash type for MIBIB *\n");
			return -1;
	}
	return execute_command(buf);
}

int do_http_progress(const int state) {
	switch (state) {
		case WEBFAILSAFE_PROGRESS_START:
#if defined(CONFIG_IPQ807X_AP8220)
			led_on("power_led");
#else
			led_off("power_led");
#endif
			led_on("blink_led");
			led_off("system_led");
			printf("HTTP server is ready!\n");
			break;
		case WEBFAILSAFE_PROGRESS_UPLOAD_READY:
			printf("HTTP upload is done! Upgrading...\n");
			break;
		case WEBFAILSAFE_PROGRESS_UPGRADE_READY:
			led_off("power_led");
			led_off("blink_led");
			led_on("system_led");
			printf("HTTP upgrade is done! Rebooting...\n");
			mdelay(3000);
			break;
		case WEBFAILSAFE_PROGRESS_UPGRADE_FAILED:
			led_on("power_led");
			led_off("blink_led");
			led_off("system_led");
			printf("## Error: HTTP upgrade failed!\n");
			break;
	}
	return 0;
}

void NetSendHttpd(void) {
	volatile uchar *tmpbuf = net_tx_packet;
	int i;
	for (i = 0; i < 40 + UIP_LLH_LEN; i++) tmpbuf[i] = uip_buf[i];
	for (; i < uip_len; i++) tmpbuf[i] = uip_appdata[i - 40 - UIP_LLH_LEN];
	eth_send(net_tx_packet, uip_len);
}

void NetReceiveHttpd(volatile uchar *inpkt, int len) {
	memcpy(uip_buf, (const void *)inpkt, len);
	uip_len = len;
	struct uip_eth_hdr *tmp = (struct uip_eth_hdr *)&uip_buf[0];
	if (tmp->type == htons(UIP_ETHTYPE_IP)) {
		uip_arp_ipin();
		uip_input();
		if (uip_len > 0) {
			uip_arp_out();
			NetSendHttpd();
		}
	} else if (tmp->type == htons(UIP_ETHTYPE_ARP)) {
		uip_arp_arpin();
		if (uip_len > 0) NetSendHttpd();
	}
}

void HttpdHandler(void) {
	for (int i = 0; i < UIP_CONNS; i++) {
		uip_periodic(i);
		if (uip_len > 0) {
			uip_arp_out();
			NetSendHttpd();
		}
	}
	if (++arptimer == 20) {
		uip_arp_timer();
		arptimer = 0;
	}
}