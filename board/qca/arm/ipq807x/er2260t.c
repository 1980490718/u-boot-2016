// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2026 ViKing <viking.yfy@qq.com> */

#include <common.h>
#include <nand.h>
#include <asm/errno.h>
#include <asm/arch-qca-common/smem.h>

#define ER2260T_FACTORY_INFO_OFFSET 0x100000

int er2260t_get_eth_mac_address(uchar *enetaddr, uint no_of_macs)
{
	uchar info[19];
	size_t len = sizeof(info);
	u32 start_blocks, size_blocks;
	loff_t offset, partition_size;
	int i, j, ret;
	unsigned int carry;

	if (!enetaddr || !no_of_macs || no_of_macs > CONFIG_IPQ_NO_MACS)
		return -EINVAL;
	ret = smem_getpart("0:ART", &start_blocks, &size_blocks);
	if (ret < 0)
		return ret;
	partition_size = (loff_t)size_blocks *
			 qca_smem_flash_info.flash_block_size;
	if (partition_size < ER2260T_FACTORY_INFO_OFFSET + sizeof(info))
		return -EINVAL;
	offset = (loff_t)start_blocks * qca_smem_flash_info.flash_block_size +
		 ER2260T_FACTORY_INFO_OFFSET;
	ret = nand_read(&nand_info[CONFIG_NAND_FLASH_INFO_IDX],
			offset, &len, info);
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
