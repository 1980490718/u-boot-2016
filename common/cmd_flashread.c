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

#ifdef CONFIG_LWIP_HTTPD
#define LOAD_ADDR	(u32)WEBFAILSAFE_UPLOAD_RAM_ADDRESS
#else
#define LOAD_ADDR	(u32)CONFIG_LOADADDR
#endif

extern u32 flash_type_new;
extern unsigned int get_spi_flash_size(void);

int flashread_partition(const char *part_name, u32 load_addr, u32 user_size, int raw, u32 *out_offset, u32 *out_size);
int flashread_partition_chunk(const char *part_name, u32 load_addr, u64 user_offset, u32 user_size, int raw, u32 *out_offset, u32 *out_size, char *out_detail);

void (*flashread_yield_fn)(void);

static int read_from_flash(int flash_type, u32 address, u32 offset, u32 part_size, char *layout, int raw) {
	char runcmd[256];
	int runcmd_len = 0;
#ifdef CONFIG_IPQ40XX
	int nand_dev = is_spi_nand_available();
#else
	int nand_dev = CONFIG_NAND_FLASH_INFO_IDX;
#endif

	if (((flash_type == SMEM_BOOT_NAND_FLASH) || (flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

		runcmd_len = snprintf(runcmd, sizeof(runcmd), "nand device %d && ", nand_dev);

		if (strcmp(layout, "default") != 0) {
			runcmd_len += snprintf(runcmd + runcmd_len, sizeof(runcmd) - runcmd_len, "ipq_nand %s && ", layout);
		}

#ifdef CONFIG_CMD_NAND
		if (raw) {
			ulong pagecount = nand_info[nand_dev].size / nand_info[nand_dev].writesize;
			snprintf(runcmd + runcmd_len, sizeof(runcmd) - runcmd_len, "nand read.raw 0x%x 0x%x %lx", address, offset, pagecount);
		} else
#endif
		{
			snprintf(runcmd + runcmd_len, sizeof(runcmd) - runcmd_len, "nand read 0x%x 0x%x 0x%x", address, offset, part_size);
		}

#ifdef CONFIG_QCA_MMC
	} else if (flash_type == SMEM_BOOT_MMC_FLASH) {
		snprintf(runcmd, sizeof(runcmd), "mmc read 0x%x 0x%x 0x%x", address, offset, part_size);
#endif
	} else if (flash_type == SMEM_BOOT_SPI_FLASH) {

		snprintf(runcmd, sizeof(runcmd), "sf probe && " "sf read 0x%x 0x%x 0x%x", address, offset, part_size);
	} else {
		return CMD_RET_FAILURE;
	}

	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;

	return CMD_RET_SUCCESS;
}

int flashread_partition(const char *part_name, u32 load_addr, u32 user_size, int raw, u32 *out_offset, u32 *out_size) {
	u32 offset, part_size, start_block, size_block, byte_size;
	int flash_type, ret;
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
			if (sfi->flash_type != SMEM_BOOT_SPI_FLASH || (part_size = get_spi_flash_size()) == 0) {
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
			part_size = (u32)nand_info[nand_dev].size;
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
					*out_size = (u32)nand_info[nd].size / nand_info[nd].writesize * (nand_info[nd].writesize + nand_info[nd].oobsize);
#else
					*out_size = part_size;
#endif
				} else {
					*out_size = part_size;
				}
			}
		}
		return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
	}

	flash_type = (flash_type_new != -1) ? flash_type_new : sfi->flash_type;

	if (((flash_type == SMEM_BOOT_NAND_FLASH) || (flash_type == SMEM_BOOT_QSPI_NAND_FLASH))) {

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
	} else if (flash_type == SMEM_BOOT_MMC_FLASH || flash_type == SMEM_BOOT_NO_FLASH) {

		blk_dev = mmc_get_dev(mmc_host.dev_num);
		if (blk_dev != NULL) {

			flash_type = SMEM_BOOT_MMC_FLASH;
			if (strncmp(GPT_PART_NAME, (const char *)part_name, sizeof(GPT_PART_NAME)) == 0) {
				offset = 0;
				part_size = GPT_PRIMARY_SIZE;
			}
			else if (strncmp(GPT_BACKUP_PART_NAME, (const char *)part_name, sizeof(GPT_BACKUP_PART_NAME)) == 0) {
				offset = (u32)blk_dev->lba - GPT_BACKUP_SIZE;
				part_size = GPT_BACKUP_SIZE;
			}
			else
			{
				ret = get_partition_info_efi_by_name(blk_dev, (char *)part_name, &disk_info);
#ifdef CONFIG_LWIP_HTTPD
				if (ret) {
					printf("Partition %s not found, skipped\n", part_name);
					return CMD_RET_FAILURE;
				}
#else
				if (ret)
					goto exit;
#endif

				offset = (u32)disk_info.start;
				part_size = (u32)disk_info.size;
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

		} else if (((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH) || (sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH)) && (strncmp(part_name, "rootfs", 6) == 0)) {

			flash_type = sfi->flash_secondary_type;

			if (sfi->rootfs.offset == 0xBAD0FF5E) {
				if (smem_bootconfig_info() == 0)
					active_part = get_rootfs_active_partition();

				offset = (u32)active_part * IPQ_NAND_ROOTFS_SIZE;
				part_size = (u32)IPQ_NAND_ROOTFS_SIZE;
			}

#ifdef CONFIG_QCA_MMC
#ifdef CONFIG_LWIP_HTTPD
		} else if ((sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH || sfi->rootfs.offset == 0xBAD0FF5E) &&
			(smem_getpart((char *)part_name, &start_block, &size_block) == -ENOENT || (start_block == 0 && size_block == 0))) {
#else
		} else if (smem_getpart((char *)part_name, &start_block, &size_block) == -ENOENT && sfi->rootfs.offset == 0xBAD0FF5E) {
#endif

			flash_type = SMEM_BOOT_MMC_FLASH;

			blk_dev = mmc_get_dev(mmc_host.dev_num);
			if (blk_dev != NULL) {

				if (strncmp(GPT_PART_NAME, (const char *)part_name, sizeof(GPT_PART_NAME)) == 0) {
					offset = 0;
					part_size = GPT_PRIMARY_SIZE;
				}
				else if (strncmp(GPT_BACKUP_PART_NAME, (const char *)part_name, sizeof(GPT_BACKUP_PART_NAME)) == 0) {
					offset = (u32)blk_dev->lba - GPT_BACKUP_SIZE;
					part_size = GPT_BACKUP_SIZE;
				}
				else
				{
					ret = get_partition_info_efi_by_name(blk_dev, (char *)part_name, &disk_info);
#ifdef CONFIG_LWIP_HTTPD
					if (ret) {
						printf("Partition %s not found, skipped\n", part_name);
					return CMD_RET_FAILURE;
					}
#else
					if (ret)
						goto exit;
#endif

					offset = (u32)disk_info.start;
					part_size = (u32)disk_info.size;
				}
			} else {
#ifdef CONFIG_LWIP_HTTPD
			printf("eMMC not initialized, skipped %s\n", part_name);
				return CMD_RET_FAILURE;
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
			u32 blksz = (u32)blk_dev->blksz;
			u32 user_sectors = (user_size + blksz - 1) / blksz;
			if (user_sectors > part_size) {
				printf("size 0x%x > part 0x%x, capped\n",
					user_size, part_size * blksz);
			} else {
				part_size = user_sectors;
			}
		} else
#endif
		{
			if (user_size > part_size) {
				printf("size 0x%x > part 0x%x, capped\n", user_size, part_size);
			} else {
				part_size = user_size;
			}
		}
	}

	ret = read_from_flash(flash_type, load_addr, offset, part_size, layout, 0);

	if (ret == CMD_RET_SUCCESS) {
		byte_size = part_size;
#ifdef CONFIG_QCA_MMC
		if (flash_type == SMEM_BOOT_MMC_FLASH && blk_dev)
			byte_size = part_size * (u32)blk_dev->blksz;
#endif
		if (out_offset)
			*out_offset = offset;
		if (out_size)
			*out_size = byte_size;
	}

exit:
	if (ret) {
		flash_type_new = -1;
		return CMD_RET_FAILURE;
	}
	return CMD_RET_SUCCESS;
}

#ifdef CONFIG_CMD_NAND
static int nand_raw_chunk_read(u32 load_addr, u64 user_offset, u32 user_size, u32 *out_offset, u32 *out_size, char *out_detail) {
#ifdef CONFIG_IPQ40XX
	int nand_dev = is_spi_nand_available();
#else
	int nand_dev = CONFIG_NAND_FLASH_INFO_IDX;
#endif
	struct mtd_info *mtd;
	u32 page_size, oob_size, total_pages, raw_page_size, start_page, pages_avail, read_bytes = 0, p;
	u8 *buf;

	if (nand_dev < 0 || nand_info[nand_dev].size == 0) {
		printf("NAND flash not available\n");
		return CMD_RET_FAILURE;
	}

	mtd = &nand_info[nand_dev];
	page_size = mtd->writesize;
	oob_size = mtd->oobsize;
	total_pages = (u32)(mtd->size / mtd->writesize);
	raw_page_size = page_size + oob_size;
	start_page = (u32)(user_offset / raw_page_size);
	pages_avail = user_size / raw_page_size;
	buf = (u8 *)load_addr;

	if (pages_avail == 0)
		pages_avail = 1;
	if (start_page >= total_pages) {
		if (out_offset) *out_offset = (u32)user_offset;
		if (out_size) *out_size = 0;
		return CMD_RET_SUCCESS;
	}
	if (start_page + pages_avail > total_pages)
		pages_avail = total_pages - start_page;
	if (pages_avail == 0) {
		if (out_offset) *out_offset = (u32)user_offset;
		if (out_size) *out_size = 0;
		return CMD_RET_SUCCESS;
	}
	for (p = start_page; p < start_page + pages_avail; p++) {
		struct mtd_oob_ops ops;
		memset(&ops, 0, sizeof(ops));
		ops.mode = MTD_OPS_RAW;
		ops.datbuf = buf;
		ops.oobbuf = buf + page_size;
		ops.len = page_size;
		ops.ooblen = oob_size;
		{
			int mtd_ret = mtd_read_oob(mtd, (loff_t)p * page_size, &ops);
			if (mtd_ret < 0 && mtd_ret != -EUCLEAN) {
				printf("nand raw chunk read page %u failed: %d\n", p, mtd_ret);
				break;
			}
		}
		buf += raw_page_size;
		read_bytes += raw_page_size;
		if (flashread_yield_fn && ((p - start_page + 1) % 128 == 0))
			flashread_yield_fn();
	}
	if (read_bytes == 0)
		return CMD_RET_FAILURE;
	if (out_offset) *out_offset = (u32)user_offset;
	if (out_size) *out_size = read_bytes;
	if (out_detail) snprintf(out_detail, 32, "%u pg", read_bytes / raw_page_size);
	return CMD_RET_SUCCESS;
}
#endif

#ifdef CONFIG_QCA_MMC
static u64 emmc_cached_base_offset = 0;
static u64 emmc_cached_part_size = 0;
static char emmc_cached_part_name[32] = "";

void emmc_cache_invalidate(void) {
	emmc_cached_base_offset = 0;
	emmc_cached_part_size = 0;
	emmc_cached_part_name[0] = '\0';
}

static int emmc_chunk_read(const char *part_name, u32 load_addr, u64 user_offset, u32 user_size, u32 *out_offset, u32 *out_size, char *out_detail) {
	block_dev_desc_t *blk_dev = mmc_get_dev(mmc_host.dev_num);
	u64 base_offset = 0, part_total_size = 0, chunk_offset, remain;
	u32 blksz, start_sector, num_sectors, chunk;
	disk_partition_t disk_info;
	int gpt_count, i;

	if (!blk_dev)
		return CMD_RET_FAILURE;
	if (strcmp(part_name, emmc_cached_part_name) == 0 && emmc_cached_part_size > 0) {
		base_offset = emmc_cached_base_offset;
		part_total_size = emmc_cached_part_size;
	} else {
		gpt_count = get_partition_count_efi(blk_dev);
		for (i = 1; i <= gpt_count; i++) {
			if (get_partition_info_efi(blk_dev, i, &disk_info) == 0 && strcmp((const char *)disk_info.name, part_name) == 0) {
				base_offset = (u64)disk_info.start * (u64)blk_dev->blksz;
				part_total_size = (u64)disk_info.size * (u64)blk_dev->blksz;
				break;
			}
		}
		if (part_total_size == 0)
			return CMD_RET_FAILURE;
		emmc_cached_base_offset = base_offset;
		emmc_cached_part_size = part_total_size;
		strncpy(emmc_cached_part_name, part_name, sizeof(emmc_cached_part_name) - 1);
	}
	if (user_offset >= part_total_size) {
		if (out_offset) *out_offset = (u32)user_offset;
		if (out_size) *out_size = 0;
		return CMD_RET_SUCCESS;
	}
	remain = part_total_size - user_offset;
	chunk = (user_size && (u64)user_size < remain) ? user_size : (u32)remain;
	chunk_offset = base_offset + user_offset;
	blksz = (u32)blk_dev->blksz;
	start_sector = (u32)(chunk_offset / blksz);
	num_sectors = (chunk + blksz - 1) / blksz;
	if (blk_dev->block_read(mmc_host.dev_num, start_sector, num_sectors, (void *)load_addr) != num_sectors) {
		printf("MMC read failed\n");
		return CMD_RET_FAILURE;
	}
	if (out_offset) *out_offset = (u32)chunk_offset;
	if (out_size) *out_size = num_sectors * blksz;
	if (out_detail) snprintf(out_detail, 32, "%u blk", num_sectors);
	return CMD_RET_SUCCESS;
}
#endif

static int sf_chunk_read(const char *part_name, u32 load_addr, u64 user_offset, u32 user_size, int raw, u32 *out_offset, u32 *out_size, char *out_detail) {
	u32 base_offset32 = 0, part_total_size32 = 0, chunk_size_bytes, chunk;
	u64 base_offset, part_total_size, chunk_offset, remain;
	char runcmd[256];

	if (flashread_partition(part_name, load_addr, 0, raw, &base_offset32, &part_total_size32) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;
	base_offset = base_offset32;
	part_total_size = part_total_size32;
	if (user_offset >= part_total_size) {
		if (out_offset) *out_offset = (u32)user_offset;
		if (out_size) *out_size = 0;
		return CMD_RET_SUCCESS;
	}
	remain = part_total_size - user_offset;
	chunk = (user_size && (u64)user_size < remain) ? user_size : (u32)remain;
	chunk_offset = base_offset + user_offset;
	chunk_size_bytes = chunk;
	snprintf(runcmd, sizeof(runcmd), "sf probe && sf read 0x%x 0x%llx 0x%x", load_addr, chunk_offset, chunk_size_bytes);
	if (run_command(runcmd, 0) != CMD_RET_SUCCESS)
		return CMD_RET_FAILURE;
	if (out_offset) *out_offset = (u32)chunk_offset;
	if (out_size) *out_size = chunk_size_bytes;
	if (out_detail) out_detail[0] = '\0';
	return CMD_RET_SUCCESS;
}

int flashread_partition_chunk(const char *part_name, u32 load_addr, u64 user_offset, u32 user_size, int raw, u32 *out_offset, u32 *out_size, char *out_detail) {
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	int flash_type = (flash_type_new != -1) ? flash_type_new : sfi->flash_type;
#ifdef CONFIG_CMD_NAND
	int nand_dev, nand_ret;
	struct mtd_info *mtd;
	u64 remain, cur_offset, end_offset;
	u32 chunk_size, erasesize;
	size_t left_to_read, block_offset, read_length;
	u_char *p_buf;
#endif

#ifdef CONFIG_CMD_NAND
	if (!strcmp(part_name, "nand_full") || flash_type == SMEM_BOOT_NAND_FLASH || flash_type == SMEM_BOOT_QSPI_NAND_FLASH) {
		if (!strcmp(part_name, "nand_full") && raw)
			return nand_raw_chunk_read(load_addr, user_offset, user_size, out_offset, out_size, out_detail);
#ifdef CONFIG_IPQ40XX
		nand_dev = is_spi_nand_available();
#else
		nand_dev = CONFIG_NAND_FLASH_INFO_IDX;
#endif
		mtd = &nand_info[nand_dev];
		remain = (user_offset < mtd->size) ? mtd->size - user_offset : 0;
		chunk_size = (user_size && (u64)user_size < remain) ? user_size : (u32)remain;
		left_to_read = chunk_size;
		cur_offset = user_offset;
		end_offset = user_offset + chunk_size;
		p_buf = (u_char *)(uintptr_t)load_addr;
		erasesize = mtd->erasesize;

		while (left_to_read > 0) {
			block_offset = cur_offset & (erasesize - 1);

			if (cur_offset >= end_offset)
				break;

			if (nand_block_isbad(mtd, cur_offset & ~(erasesize - 1))) {
				cur_offset += erasesize - block_offset;
				continue;
			}
			if (left_to_read < (erasesize - block_offset))
				read_length = left_to_read;
			else
				read_length = erasesize - block_offset;

			nand_ret = nand_read(mtd, cur_offset, &read_length, p_buf);
			if (nand_ret < 0 && nand_ret != -EUCLEAN) {
				printf("nand read error at 0x%llx: %d\n", cur_offset, nand_ret);
				return CMD_RET_FAILURE;
			}

			left_to_read -= read_length;
			cur_offset += read_length;
			p_buf += read_length;

			if (flashread_yield_fn)
				flashread_yield_fn();
		}
		if (out_offset) *out_offset = (u32)user_offset;
		if (out_size) *out_size = chunk_size;
		if (out_detail) out_detail[0] = '\0';
		return CMD_RET_SUCCESS;
	}
#endif
#ifdef CONFIG_QCA_MMC
	if (flash_type == SMEM_BOOT_MMC_FLASH || flash_type == SMEM_BOOT_NO_FLASH)
		return emmc_chunk_read(part_name, load_addr, user_offset, user_size, out_offset, out_size, out_detail);
#endif
	if (flash_type == SMEM_BOOT_SPI_FLASH) {
#ifdef CONFIG_CMD_NAND
		if (get_which_flash_param((char *)part_name) > 0 || ((sfi->flash_secondary_type == SMEM_BOOT_NAND_FLASH || sfi->flash_secondary_type == SMEM_BOOT_QSPI_NAND_FLASH) && !strncmp(part_name, "rootfs", 6)))
			return nand_raw_chunk_read(load_addr, user_offset, user_size, out_offset, out_size, out_detail);
#endif
#ifdef CONFIG_QCA_MMC
		if (sfi->flash_secondary_type == SMEM_BOOT_MMC_FLASH && !strncmp(part_name, "rootfs", 6))
			return emmc_chunk_read(part_name, load_addr, user_offset, user_size, out_offset, out_size, out_detail);
#endif
	}
	return sf_chunk_read(part_name, load_addr, user_offset, user_size, raw, out_offset, out_size, out_detail);
}

static int do_flashread(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]) {
	u32 load_addr, user_size = 0;
	char *part_name = NULL;
	int ret;
	if (argc < 2 || argc > 4)
		return CMD_RET_USAGE;

	part_name = argv[1];

	if (argc >= 3)
		load_addr = simple_strtoul(argv[2], NULL, 16);
	else
		load_addr = LOAD_ADDR;

	if (argc == 4)
		user_size = simple_strtoul(argv[3], NULL, 16);

	ret = flashread_partition(part_name, load_addr, user_size, 0, NULL, NULL);

	return ret ? CMD_RET_USAGE : CMD_RET_SUCCESS;
}

static int do_backup(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]) {
	u32 load_addr, offset, size;
	char *part_name = NULL, *serverip, runcmd[256], filename[80];
	int ret;

	if (argc < 2 || argc > 3)
		return CMD_RET_USAGE;

	part_name = argv[1];

	if (argc == 3)
		load_addr = simple_strtoul(argv[2], NULL, 16);
	else
		load_addr = LOAD_ADDR;

	ret = flashread_partition(part_name, load_addr, 0, 0, &offset, &size);
	if (ret)
		return CMD_RET_USAGE;

	serverip = getenv("serverip");
	if (serverip)
		snprintf(filename, sizeof(filename), "%s:%s.bin", serverip, part_name);
	else
		snprintf(filename, sizeof(filename), "%s.bin", part_name);

	snprintf(runcmd, sizeof(runcmd), "tftpput 0x%x 0x%x %s", load_addr, size, filename);

	ret = run_command(runcmd, 0);
	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

#ifdef CONFIG_CMD_TFTPPUT
U_BOOT_CMD(
	backup, 3, 0, do_backup,
	"backup partition or full flash to TFTP server",
	"part_name|nor_full|nand_full [load_addr]"
);
#endif

U_BOOT_CMD(
	flashread, 4, 0, do_flashread,
	"read partition from flash to RAM address",
	"part_name [load_addr] [size]"
);