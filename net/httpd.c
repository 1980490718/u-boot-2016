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

static int arptimer = 0;
struct in_addr net_httpd_ip;
void HttpdStart(void) {
    struct uip_eth_addr eaddr;
    unsigned short int ip[2];
    ulong tmp_ip_addr = ntohl(net_ip.s_addr);
    printf("HTTP server is starting at IP: %ld.%ld.%ld.%ld\n", (tmp_ip_addr & 0xff000000) >> 24, (tmp_ip_addr & 0x00ff0000) >> 16, (tmp_ip_addr & 0x0000ff00) >> 8, (tmp_ip_addr & 0x000000ff));
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

    printf("done set host addr 0x%x 0x%x\n", uip_hostaddr[0], uip_hostaddr[1]);
    ip[0] = htons((0xFFFFFF00 & 0xFFFF0000) >> 16);
    ip[1] = htons(0xFFFFFF00 & 0x0000FFFF);

    net_netmask.s_addr = 0x00FFFFFF;
    uip_setnetmask(ip);

    do_http_progress(WEBFAILSAFE_PROGRESS_START);
    webfailsafe_is_running = 1;
}

void HttpdStop(void) {
    webfailsafe_is_running = 0;
    webfailsafe_ready_for_upgrade = 0;
    webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE;
    do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_FAILED);
}

void HttpdDone(void) {
    webfailsafe_is_running = 0;
    webfailsafe_ready_for_upgrade = 0;
    webfailsafe_upgrade_type = WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE;
    do_http_progress(WEBFAILSAFE_PROGRESS_UPGRADE_READY);
}

#ifdef CONFIG_MD5
#include <u-boot/md5.h>
void printChecksumMd5(int address, unsigned int size) {
    void *buf = (void *)(address);
    u8 output[16];
    md5_wd(buf, size, output, CHUNKSZ_MD5);
    printf("md5 for %08x ... %08x ==> ", address, address + size);
    for (int i = 0; i < 16; i++) printf("%02x", output[i] & 0xFF);
    printf("\n");
}
#else
void printChecksumMd5(int address, unsigned int size) {}
#endif

int do_http_upgrade(const ulong size, const int upgrade_type) {
    char buf[576];
    printChecksumMd5(WEBFAILSAFE_UPLOAD_RAM_ADDRESS, size);

    if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE) {
        if (qca_smem_flash_info.flash_type == 5) {
            int fw_type = check_fw_type((void *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            if (fw_type == FW_TYPE_NOR) {
                printf("\n* Factory FIRMWARE UPGRADING *\n");
                sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:HLOS 0x%lx 0x600000 && flash rootfs 0x%lx 0x%lx && mmc read 0x%lx 0x622 0x200 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && flash 0:BOOTCONFIG 0x%lx 0x40000 && flash 0:BOOTCONFIG1 0x%lx 0x40000",
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + size),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0x600000),
                        (unsigned long)(size - 0x600000),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0x80),
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0x94),
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0xA8),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            } else if (fw_type == FW_TYPE_QSDK) {
                printf("\n* Original FIRMWARE UPGRADING *\n");
                sprintf(buf, "imxtract 0x%lx hlos-0cc33b23252699d495d79a843032498bfa593aba && mmc dev 0 && flash 0:HLOS $fileaddr $filesize && imxtract 0x%lx rootfs-f3c50b484767661151cfb641e2622703e45020fe && flash rootfs $fileaddr $filesize && imxtract 0x%lx wififw-45b62ade000c18bfeeb23ae30e5a6811eac05e2f && flash 0:WIFIFW $fileaddr $filesize && flasherase rootfs_data && mmc read 0x%lx 0x622 0x200 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && mw.b 0x%lx 0x00 0x1 && flash 0:BOOTCONFIG 0x%lx 0x40000 && flash 0:BOOTCONFIG1 0x%lx 0x40000",
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0x80),
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0x94),
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + 0xA8),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            } else {
                printf("\n* Unsupported FIRMWARE type *\n");
                return -1;
            }
        } else {
            int fw_type = check_fw_type((void *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            if (fw_type == FW_TYPE_NOR) {
                printf("\n* FIRMWARE UPGRADING *\n");
                sprintf(buf, "sf probe && sf update 0x%lx 0x%lx 0x%lx",
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_FW_ADDRESS,
                        (unsigned long)size);
            } else if (fw_type == FW_TYPE_QSDK) {
                printf("\n* FIRMWARE UPGRADING *\n");
                sprintf(buf, "sf probe; imgaddr=0x%lx && source $imgaddr:script",
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            } else if (fw_type == FW_TYPE_UBI) {
                printf("\n* FIRMWARE UPGRADING *\n");
                sprintf(buf, "nand erase 0xa00000 0x7300000; nand write 0x%lx 0xa00000 0x%lx",
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)size);
            } else {
                printf("\n* Unsupported FIRMWARE type *\n");
                return -1;
            }
        }
    } else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_UBOOT) {
        printf("\n* U-BOOT UPGRADING *\n");
        if (qca_smem_flash_info.flash_type == 5) {
            if (check_fw_type((void *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS) == FW_TYPE_ELF) {
                sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:APPSBL 0x%lx $filesize && flash 0:APPSBL_1 0x%lx $filesize",
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + size),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            } else {
                printf("\n* Unsupported UBOOT ELF *\n");
                return -1;
            }
        } else if (qca_smem_flash_info.flash_type == 2) {
            sprintf(buf, "nand erase 0x%lx 0x%lx; nand write 0x%lx 0x%lx 0x%lx",
                    (unsigned long)WEBFAILSAFE_UPLOAD_UBOOT_ADDRESS_NAND,
                    (unsigned long)WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES_NAND,
                    (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                    (unsigned long)WEBFAILSAFE_UPLOAD_UBOOT_ADDRESS_NAND,
                    (unsigned long)((size / 131072 + (size % 131072 != 0)) * 131072));
        } else if (qca_smem_flash_info.flash_type == 6) {
            sprintf(buf, "sf probe && sf update 0x%lx 0x%lx 0x%lx",
                    (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                    (unsigned long)WEBFAILSAFE_UPLOAD_UBOOT_ADDRESS,
                    (unsigned long)size);
        } else {
            printf("\n* Unsupported flash type for U-Boot *\n");
            return -1;
        }
    } else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_ART) {
        printf("\n* ART UPGRADING *\n");
        if (qca_smem_flash_info.flash_type == 5) {
            sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:ART 0x%lx $filesize",
                    (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + size),
                    (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
        } else if (qca_smem_flash_info.flash_type == 2) {
            sprintf(buf, "nand erase 0x%lx 0x%lx; nand write 0x%lx 0x%lx 0x%lx",
                    (unsigned long)WEBFAILSAFE_UPLOAD_ART_ADDRESS_NAND,
                    (unsigned long)WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES_NAND,
                    (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                    (unsigned long)WEBFAILSAFE_UPLOAD_ART_ADDRESS_NAND,
                    (unsigned long)((size / 131072 + (size % 131072 != 0)) * 131072));
        } else if (qca_smem_flash_info.flash_type == 6) {
            sprintf(buf, "sf probe && sf update 0x%lx 0x%lx 0x%lx",
                    (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                    (unsigned long)WEBFAILSAFE_UPLOAD_ART_ADDRESS,
                    (unsigned long)size);
        } else {
            printf("\n* Unsupported flash type for ART *\n");
            return -1;
        }
    } else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_IMG) {
        printf("\n* IMG UPGRADING *\n");
        if (qca_smem_flash_info.flash_type == 5) {
            if (check_fw_type((void *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS) == FW_TYPE_EMMC) {
                sprintf(buf, "mmc dev 0 && mmc erase 0x0 0x%lx && mmc write 0x%lx 0x0 0x%lx",
                        (unsigned long)((size - 1) / 512 + 1),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)((size - 1) / 512 + 1));
            } else {
                printf("\n* Unsupported IMG type *\n");
                return -1;
            }
        } else {
            printf("\n* Unsupported flash type for IMG *\n");
            return -1;
        }
    } else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT) {
        printf("\n* CDT UPGRADING *\n");
        if (qca_smem_flash_info.flash_type == 5) {
            if (check_fw_type((void *)WEBFAILSAFE_UPLOAD_RAM_ADDRESS) == FW_TYPE_CDT) {
                sprintf(buf, "mw 0x%lx 0x00 0x200 && mmc dev 0 && flash 0:CDT 0x%lx $filesize && flash 0:CDT_1 0x%lx $filesize",
                        (unsigned long)(WEBFAILSAFE_UPLOAD_RAM_ADDRESS + size),
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS,
                        (unsigned long)WEBFAILSAFE_UPLOAD_RAM_ADDRESS);
            } else {
                printf("\n* Unsupported CDT type *\n");
                return -1;
            }
        } else {
            printf("\n* Unsupported flash type for CDT *\n");
            return -1;
        }
    } else {
        printf("\n* Unsupported upgrade type *\n");
        return -1;
    }

    printf("Executing: %s\n", buf);
    return run_command(buf, 0);
}

int do_http_progress(const int state) {
    switch (state) {
        case WEBFAILSAFE_PROGRESS_START:
            led_off("power_led");
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
