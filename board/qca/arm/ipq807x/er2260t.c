// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2026 ViKing <viking.yfy@qq.com> */

#include <common.h>
#include <command.h>
#include <nand.h>
#include <asm/errno.h>

#define ER2260T_FACTORY_INFO 0x0700000
#define ER2260T_ROOTFS       0x1700000

int er2260t_get_eth_mac_address(uchar *enetaddr, uint no_of_macs)
{
	uchar info[19];
	size_t len = sizeof(info);
	int i, j, ret;
	unsigned int carry;

	if (!enetaddr || !no_of_macs || no_of_macs > CONFIG_IPQ_NO_MACS)
		return -EINVAL;
	ret = nand_read(&nand_info[CONFIG_NAND_FLASH_INFO_IDX],
			ER2260T_FACTORY_INFO, &len, info);
	if (ret < 0 || len != sizeof(info))
		return ret < 0 ? ret : -EIO;
	if (memcmp(info, "M1CT", 4) || memcmp(info + 8, "{mac:", 5) ||
	    !is_valid_ethaddr(info + 13))
		return -EINVAL;

	for (i = 0; i < no_of_macs; i++) {
		memcpy(enetaddr + i * 6, info + 13, 6);
		carry = i;
		for (j = 5; j >= 0; j--) {
			carry += enetaddr[i * 6 + j];
			enetaddr[i * 6 + j] = carry;
			carry >>= 8;
		}
	}
	return 0;
}

static int do_er2260t_boot(cmd_tbl_t *cmdtp, int flag, int argc,
			   char *const argv[])
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	uchar magic[4];
	loff_t offset;
	size_t len;
	int ret;

	/* UBI headers may start after a factory bad block. */
	for (offset = ER2260T_ROOTFS;
	     offset < ER2260T_ROOTFS + 4 * nand->erasesize;
	     offset += nand->erasesize) {
		if (nand_block_isbad(nand, offset))
			continue;
		len = sizeof(magic);
		ret = nand_read(nand, offset, &len, magic);
		if (ret >= 0 && len == sizeof(magic) &&
		    !memcmp(magic, "UBI#", sizeof(magic)))
			return run_command("bootipq", 0);
	}

	/* Retain stock boot until an OpenWrt UBI has actually been installed. */
	puts("Booting stock TL-ER2260T firmware\n");
	ret = run_command("setenv bootargs console=ttyMSM0,115200n8 "
			  "root=/dev/mtdblock7 rootfstype=squashfs "
			  "ubi.mtd=8 ubi.mtd=3 ubi.mtd=4 rootwait swiotlb=1; "
			  "nand read 0x44000000 0x1800000 0x700000 && "
			  "bootm 0x44000000#config@1", 0);
	if (ret)
		return run_command("httpd", 0);
	return ret;
}

U_BOOT_CMD(er2260t_boot, 1, 0, do_er2260t_boot,
	   "boot TL-ER2260T OpenWrt UBI or stock firmware", "");
