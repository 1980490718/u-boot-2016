// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2026 ViKing <viking.yfy@qq.com> */

#include <common.h>
#include <nand.h>
#include <asm/errno.h>

#define ER2260T_FACTORY_INFO 0x0700000

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
