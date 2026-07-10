/*
 * Copyright (c) 2025 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * FlashRead command support - read partition data from flash to RAM
 * This is the read counterpart of cmd_flashwrite.c
 */

#include <common.h>
#include <command.h>
#include <asm/arch-qca-common/smem.h>
#include <part.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <mmc.h>
#include <sdhci.h>
#include <fdtdec.h>
#include <asm/arch-qca-common/qpic_nand.h>
#ifdef CONFIG_IPQ40XX
#include <../board/qca/arm/common/fdt_info.h>
#endif
#ifdef CONFIG_LWIP_HTTPD
#include <ipq_api.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

#define SMEM_PTN_NAME_MAX     16
#ifdef CONFIG_QCA_MMC
#define GPT_PART_NAME "0:GPT"
#define GPT_BACKUP_PART_NAME "0:GPTBACKUP"
#define GPT_PRIMARY_SIZE 34
#define GPT_BACKUP_SIZE 33
#endif

extern uint32_t flash_type_new;
extern unsigned int get_spi_flash_size(void);

int flashread_partition(const char *part_name, uint32_t load_addr,
			uint32_t user_size, int raw, uint32_t *out_offset, uint32_t *out_size);

static int read_from_flash(int flash_type, uint32_t address, uint32_t offset,
			   uint32_t part_size, char *layout, int raw)
{
	char runcmd[256];
	int runcmd_len = 0;
#ifdef CONFIG_IPQ40XX
	int nand_dev = is_spi_nand_available();
#else
	int nand_dev = CONFIG_NAND_FLASH_INFO_IDX;
#endif

	if (((flash_type == SMEM_BOOT_NAND_FLASH) ||
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

		runcmd_len = snprintf(runcmd, sizeof(runcmd),
				"nand device %d && ", nand_dev);

		if (strcmp(layout, "default") != 0) {
			runcmd_len += snprintf(runcmd + runcmd_len,
					sizeof(runcmd) - runcmd_len,
					"ipq_nand %s && ", layout);
		}

#ifdef CONFIG_CMD_NAND
		if (raw) {
			ulong pagecount = nand_info[nand_dev].size / nand_info[nand_dev].writesize;
			snprintf(runcmd + runcmd_len, sizeof(runcmd) - runcmd_len,
				"nand read.raw 0x%x 0x%x %lx", address, offset, pagecount);
		} else
#endif
		{
			snprintf(runcmd + runcmd_len, sizeof(runcmd) - runcmd_len,
				"nand read 0x%x 0x%x 0x%x", address, offset, part_size);
		}

	} else if (flash_type == SMEM_BOOT_MMC_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
			"mmc read 0x%x 0x%x 0x%x",
			address, offset, part_size);

	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {

		snprintf(runcmd, sizeof(runcmd),
			"sf probe && "
			"sf read 0x%x 0x%x 0x%x",
			address, offset, part_size);
	} else {
		return CMD_RET_FAILURE;
	}

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}

int flashread_partition(const char *part_name, uint32_t load_addr,
			uint32_t user_size, int raw, uint32_t *out_offset, uint32_t *out_size)
{
	uint32_t offset, part_size;
	int flash_type, ret;
	uint32_t start_block, size_block;
	char *layout = NULL;
	unsigned int active_part = 0;
#ifdef CONFIG_IPQ806X
	char *layout_linux[] = {"rootfs", "0:BOOTCONFIG", "0:BOOTCONFIG1"};
	int len, i;
#endif
#ifdef CONFIG_QCA_MMC
	block_dev_desc_t *blk_dev = NULL;
	disk_partition_t disk_info = {0};
#endif
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;

	offset = 0;
	part_size = 0;
	layout = "default";
	ret = -1;

	if (!strcmp(part_name, "nor_full") || !strcmp(part_name, "nand_full")) {
		if (!strcmp(part_name, "nor_full")) {
			if (sfi->flash_type != SMEM_BOOT_SPI_FLASH ||
			    (part_size = get_spi_flash_size()) == 0) {
				printf("SPI flash not available\n");
				return CMD_RET_FAILURE;
			}
			flash_type = SMEM_BOOT_SPI_FLASH;
		} else {
#ifdef CONFIG_CMD_NAND
#ifdef CONFIG_IPQ40XX
			int nand_dev = is_spi_nand_available();
#else
			int nand_dev = CONFIG_NAND_FLASH_INFO_IDX;
#endif
			if (nand_dev < 0 || nand_info[nand_dev].size == 0) {
				printf("NAND flash not available\n");
				return CMD_RET_FAILURE;
			}
			part_size = (uint32_t)nand_info[nand_dev].size;
			flash_type = SMEM_BOOT_NAND_FLASH;
#else
			printf("NAND flash not available\n");
			return CMD_RET_FAILURE;
#endif
		}
		ret = read_from_flash(flash_type, load_addr, 0, part_size, "default", raw);
		if (ret == CMD_RET_SUCCESS) {
			if (out_offset) *out_offset = 0;
			if (out_size) {
				if (raw && !strcmp(part_name, "nand_full")) {
#ifdef CONFIG_CMD_NAND
#ifdef CONFIG_IPQ40XX
					int nd = is_spi_nand_available();
#else
					int nd = CONFIG_NAND_FLASH_INFO_IDX;
#endif
					*out_size = (uint32_t)nand_info[nd].size /
					    nand_info[nd].writesize *
					    (nand_info[nd].writesize + nand_info[nd].oobsize);
#else
					*out_size = part_size;
#endif
				} else {
					*out_size = part_size;
				}
			}
			printf("Read %x hex from %s@0x0 to 0x%x%s\n",
				*out_size, part_name, load_addr,
				raw ? " (raw)" : "");
		}
		return ret;
	}

	flash_type = (flash_type_new != -1) ? flash_type_new : sfi->flash_type;

	if (((flash_type == SMEM_BOOT_NAND_FLASH) ||
		(flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

		ret = smem_getpart((char *)part_name, &start_block, &size_block);
		if (ret)
			goto exit;

		offset = sfi->flash_block_size * start_block;
		part_size = sfi->flash_block_size * size_block;

#ifdef CONFIG_IPQ806X
		len = sizeof(layout_linux) / sizeof(layout_linux[0]);
		for (i = 0; i < len; i++) {
			if (!strncmp(layout_linux[i], part_name, SMEM_PTN_NAME_MAX)) {
				layout = "linux";
				break;
			}
		}
		if (i == len)
			layout = "sbl";
#endif

#ifdef CONFIG_QCA_MMC
	} else if (flash_type == SMEM_BOOT_MMC_FLASH ||
		flash_type == SMEM_BOOT_NO_FLASH) {

		blk_dev = mmc_get_dev(mmc_host.dev_num);
		if (blk_dev != NULL) {

			flash_type = SMEM_BOOT_MMC_FLASH;
			if (strncmp(GPT_PART_NAME,
					(const char *)part_name,
					sizeof(GPT_PART_NAME)) == 0) {
				offset = 0;
				part_size = GPT_PRIMARY_SIZE;
			}
			else if (strncmp(GPT_BACKUP_PART_NAME,
					(const char *)part_name,
					sizeof(GPT_BACKUP_PART_NAME)) == 0) {
				offset = (uint32_t)blk_dev->lba - GPT_BACKUP_SIZE;
				part_size = GPT_BACKUP_SIZE;
			}
			else
			{
				ret = get_partition_info_efi_by_name(blk_dev,
						(char *)part_name, &disk_info);
#ifdef CONFIG_LWIP_HTTPD
				if (ret) {
					printf("Partition %s not found, skipped\n", part_name);
					return -1;
				}
#else
				if (ret)
					goto exit;
#endif

				offset = (uint32_t)disk_info.start;
				part_size = (uint32_t)disk_info.size;
			}
		} else {
			ret = -1;
			goto exit;
		}
#endif
	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {

		if (get_which_flash_param((char *)part_name) > 0) {

			flash_type = SMEM_BOOT_NAND_FLASH;
			ret = getpart_offset_size((char *)part_name, &offset, &part_size);
			if (ret)
				goto exit;

		} else if (((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH) ||
				(sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH))
				&& (strncmp(part_name, "rootfs", 6) == 0)) {

			flash_type = sfi->flash_secondary_type;

			if (sfi->rootfs.offset == 0xBAD0FF5E) {
				if (smem_bootconfig_info() == 0)
					active_part = get_rootfs_active_partition();

				offset = (uint32_t)active_part * IPQ_NAND_ROOTFS_SIZE;
				part_size = (uint32_t)IPQ_NAND_ROOTFS_SIZE;
			}

#ifdef CONFIG_QCA_MMC
#ifdef CONFIG_LWIP_HTTPD
		} else if ((sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH ||
				sfi->rootfs.offset == 0xBAD0FF5E) &&
			(smem_getpart((char *)part_name, &start_block, &size_block) == -ENOENT ||
			 (start_block == 0 && size_block == 0))) {
#else
		} else if (smem_getpart((char *)part_name, &start_block, &size_block) == -ENOENT &&
				sfi->rootfs.offset == 0xBAD0FF5E) {
#endif

			flash_type = SMEM_BOOT_MMC_FLASH;

			blk_dev = mmc_get_dev(mmc_host.dev_num);
			if (blk_dev != NULL) {

				if (strncmp(GPT_PART_NAME,
					(const char *)part_name,
					sizeof(GPT_PART_NAME)) == 0) {
					offset = 0;
					part_size = GPT_PRIMARY_SIZE;
				}
				else if (strncmp(GPT_BACKUP_PART_NAME,
					(const char *)part_name,
					sizeof(GPT_BACKUP_PART_NAME)) == 0) {
					offset = (uint32_t)blk_dev->lba - GPT_BACKUP_SIZE;
					part_size = GPT_BACKUP_SIZE;
				}
				else
				{
					ret = get_partition_info_efi_by_name(blk_dev,
							(char *)part_name, &disk_info);
#ifdef CONFIG_LWIP_HTTPD
					if (ret) {
						printf("Partition %s not found, skipped\n", part_name);
					return -1;
					}
#else
					if (ret)
						goto exit;
#endif

					offset = (uint32_t)disk_info.start;
					part_size = (uint32_t)disk_info.size;
				}
			} else {
#ifdef CONFIG_LWIP_HTTPD
			printf("eMMC not initialized, skipped %s\n", part_name);
				return -1;
#else
				ret = -1;
				goto exit;
#endif
			}
#endif
		} else {
			ret = smem_getpart((char *)part_name, &start_block, &size_block);
			if (ret)
				goto exit;

			offset = sfi->flash_block_size * start_block;
			part_size = sfi->flash_block_size * size_block;
		}
	}

	if (user_size) {
#ifdef CONFIG_QCA_MMC
		if (flash_type == SMEM_BOOT_MMC_FLASH && blk_dev) {
			uint32_t blksz = (uint32_t)blk_dev->blksz;
			uint32_t user_sectors = (user_size + blksz - 1) / blksz;
			if (user_sectors > part_size) {
				printf("Warning: requested size 0x%x exceeds partition size 0x%x, capped\n",
					user_size, part_size * blksz);
			} else {
				part_size = user_sectors;
			}
		} else
#endif
		{
			if (user_size > part_size) {
				printf("Warning: requested size 0x%x exceeds partition size 0x%x, capped\n",
					user_size, part_size);
			} else {
				part_size = user_size;
			}
		}
	}

	ret = read_from_flash(flash_type, load_addr, offset, part_size, layout, 0);

	if (ret == CMD_RET_SUCCESS) {
		uint32_t byte_size = part_size;
#ifdef CONFIG_QCA_MMC
		if (flash_type == SMEM_BOOT_MMC_FLASH && blk_dev)
			byte_size = part_size * (uint32_t)blk_dev->blksz;
#endif
		if (out_offset)
			*out_offset = offset;
		if (out_size)
			*out_size = byte_size;
		printf("Read %x hex from %s@0x%x to 0x%x\n",
			byte_size, part_name, offset, load_addr);
	}

exit:
	if (ret)
		flash_type_new = -1;
	return ret;
}

static int do_flashread(cmd_tbl_t *cmdtp, int flag, int argc,
			char * const argv[])
{
	uint32_t load_addr = 0, user_size = 0;
	char *part_name = NULL;
	int ret;
	if (argc < 3 || argc > 4)
		return CMD_RET_USAGE;

	part_name = argv[1];
	load_addr = simple_strtoul(argv[2], NULL, 16);

	if (argc == 4)
		user_size = simple_strtoul(argv[3], NULL, 16);

	ret = flashread_partition(part_name, load_addr, user_size, 0, NULL, NULL);

	return ret;
}

static int do_flread(cmd_tbl_t *cmdtp, int flag, int argc,
			char * const argv[])
{
	uint32_t load_addr;
	char *part_name = NULL;

	if (argc < 2 || argc > 3)
		return CMD_RET_USAGE;

	part_name = argv[1];

	if (argc == 3)
		load_addr = simple_strtoul(argv[2], NULL, 16);
#ifdef CONFIG_LWIP_HTTPD
	else
		load_addr = (uint32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
#else
	else
		load_addr = (uint32_t)CONFIG_LOADADDR;
#endif

	return flashread_partition(part_name, load_addr, 0, 0, NULL, NULL);
}

U_BOOT_CMD(
	flread, 3, 0, do_flread,
	"flread part_name [load_addr]\n",
	"read partition from flash to RAM address\n"
);

static int do_backup(cmd_tbl_t *cmdtp, int flag, int argc,
			char * const argv[])
{
	uint32_t load_addr, offset, size;
	char *part_name = NULL;
	char runcmd[256], filename[80], *serverip;
	int ret;

	if (argc < 2 || argc > 3)
		return CMD_RET_USAGE;

	part_name = argv[1];

	if (argc == 3)
		load_addr = simple_strtoul(argv[2], NULL, 16);
#ifdef CONFIG_LWIP_HTTPD
	else
		load_addr = (uint32_t)WEBFAILSAFE_UPLOAD_RAM_ADDRESS;
#else
	else
		load_addr = (uint32_t)CONFIG_LOADADDR;
#endif

	ret = flashread_partition(part_name, load_addr, 0, 0, &offset, &size);
	if (ret)
		return ret;

	serverip = getenv("serverip");
	if (serverip)
		snprintf(filename, sizeof(filename), "%s:%s.bin", serverip, part_name);
	else
		snprintf(filename, sizeof(filename), "%s.bin", part_name);

	snprintf(runcmd, sizeof(runcmd), "tftpput 0x%x 0x%x %s",
		load_addr, size, filename);

	return run_command(runcmd, 0);
}

#ifdef CONFIG_CMD_TFTPPUT
U_BOOT_CMD(
	backup, 3, 0, do_backup,
	"backup part_name|nor_full|nand_full [load_addr]\n",
	"backup partition or full flash to TFTP server\n"
);
#endif

U_BOOT_CMD(
	flashread, 4, 0, do_flashread,
	"flashread part_name load_addr [size]\n",
	"read partition from flash to RAM address\n"
);