#include <common.h>
#include <command.h>
#include <net.h>
#ifdef CONFIG_NAND_FLASH
#include <nand.h>
#endif
#include <spi.h>
#include <spi_flash.h>
#include <mmc.h>
#include <part.h>
#include <sdhci.h>
#include <asm/errno.h>
#include <asm/arch-qca-common/smem.h>
#include <asm/arch-qca-common/qca_common.h>
#include "fdt_info.h"
#include "macaddr_modify.h"

#define ART_BASE_SIZE	0x1000
#define ART_BASE_MAC0	0x0000
#define ART_MAC_LEN	6

#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
#define ATH10K_WIFI_COUNT	3
#define ATH10K_CAL_MAC_OFF	6
#define ATH10K_CAL_CRC		0x02
#define ATH10K_CAL_CHECKSUM_SIZE	12064
#else
#define ART_CAL_SIZE_64K	0x10000
#define ART_CAL_SIZE_128K	0x20000
#define ART_CAL_CRC		0x0A
#define ART_CAL_MAC0		0x0E
#define WIFI_CAL_BLOCKS		3
#define WIFI_CAL_MAX_IDX	(WIFI_CAL_BLOCKS * 2)
#endif

#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
static const u32 cal_offsets[ATH10K_WIFI_COUNT] = { 0x1000, 0x5000, 0x9000 };
#else
static const u32 cal_offsets[WIFI_CAL_BLOCKS] = { 0x1000, 0x26800, 0x4C000 };
#endif

#ifdef CONFIG_QCA_MMC
#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif
#endif

static u16 xor16_checksum(const void *buf, size_t len) {
	const u16 *p = buf;
	u16 cs = 0;
	size_t i;
	for (i = 0; i < len / 2; i++)
		cs ^= le16_to_cpu(p[i]);
	return cs;
}

#if !defined(CONFIG_IPQ40XX) && !defined(CONFIG_IPQ806X)
static size_t art_cal_size(const u8 *hdr) {
	if (hdr[9] == 0x80)
		return ART_CAL_SIZE_128K;
	return ART_CAL_SIZE_64K;
}
#endif

static int is_nor_flash(uint32_t type) {
	return type == SMEM_BOOT_NOR_FLASH || type == SMEM_BOOT_SPI_FLASH ||
		   type == SMEM_BOOT_NORPLUSNAND || type == SMEM_BOOT_NORPLUSEMMC;
}

static int is_nand_flash(uint32_t type) {
	return type == SMEM_BOOT_NAND_FLASH || type == SMEM_BOOT_QSPI_NAND_FLASH;
}

static loff_t art_part_offset(void) {
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	u32 start_blocks, size_blocks;

	if (smem_getpart("0:ART", &start_blocks, &size_blocks) < 0)
		return -1;

	return (loff_t)sfi->flash_block_size * start_blocks;
}

static int art_read(loff_t offset, size_t size, void *buf) {
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	loff_t base;
	int ret;

#ifdef CONFIG_QCA_MMC
	if (sfi->flash_type == SMEM_BOOT_MMC_FLASH) {
		block_dev_desc_t *blk_dev = mmc_get_dev(mmc_host.dev_num);
		disk_partition_t disk_info;
		struct mmc *mmc;
		u32 blk_off, blk_cnt, intra, i;
		u8 *blk_buf;
		if (!blk_dev)
			return -ENODEV;
		ret = get_partition_info_efi_by_name(blk_dev, "0:ART", &disk_info);
		if (ret < 0)
			return ret;
		mmc = mmc_host.mmc;
		intra = offset % blk_dev->blksz;
		blk_off = disk_info.start + (offset / blk_dev->blksz);
		blk_cnt = (intra + size + blk_dev->blksz - 1) / blk_dev->blksz;
		blk_buf = memalign(ARCH_DMA_MINALIGN, blk_dev->blksz);
		if (!blk_buf)
			return -ENOMEM;
		for (i = 0; i < blk_cnt; i++) {
			u32 skip = (i == 0) ? intra : 0;
			u32 dst_off = (i == 0) ? 0 : i * blk_dev->blksz - intra;
			u32 copy_len = min((u32)size - dst_off, (u32)blk_dev->blksz - skip);
			ret = mmc->block_dev.block_read(mmc_host.dev_num, blk_off + i, 1, blk_buf);
			if (ret < 0)
				break;
			memcpy(buf + dst_off, blk_buf + skip, copy_len);
		}
		free(blk_buf);
		return (ret < 0) ? ret : 0;
	}
#endif

	base = art_part_offset();
	if (base < 0)
		return -ENOENT;

	if (is_nor_flash(sfi->flash_type)) {
		struct spi_flash *flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS, CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		if (!flash)
			return -ENODEV;
		ret = spi_flash_read(flash, base + offset, size, buf);
	} else if (is_nand_flash(sfi->flash_type)) {
#if defined(CONFIG_NAND_FLASH)
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
		int idx = is_spi_nand_available();
#else
		int idx = CONFIG_NAND_FLASH_INFO_IDX;
#endif
		size_t len = size;
		ret = nand_read(&nand_info[idx], base + offset, &len, buf);
		if (len != size)
			ret = -EIO;
#else
		return -EINVAL;
#endif
	} else {
		return -EINVAL;
	}

	return ret;
}

static int art_write(loff_t offset, size_t size, const void *buf) {
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	loff_t base;
	int ret;

#ifdef CONFIG_QCA_MMC
	if (sfi->flash_type == SMEM_BOOT_MMC_FLASH) {
		block_dev_desc_t *blk_dev = mmc_get_dev(mmc_host.dev_num);
		disk_partition_t disk_info;
		struct mmc *mmc;
		u32 blk_off, blk_cnt, i;
		u8 *blk_buf;
		if (!blk_dev)
			return -ENODEV;
		ret = get_partition_info_efi_by_name(blk_dev, "0:ART", &disk_info);
		if (ret < 0)
			return ret;
		mmc = mmc_host.mmc;
		blk_off = disk_info.start + (offset / blk_dev->blksz);
		blk_cnt = (size + blk_dev->blksz - 1) / blk_dev->blksz;
		blk_buf = memalign(ARCH_DMA_MINALIGN, blk_dev->blksz);
		if (!blk_buf)
			return -ENOMEM;
		for (i = 0; i < blk_cnt; i++) {
			u32 byte_off = i * blk_dev->blksz;
			u32 copy_len = min((u32)size - byte_off, (u32)blk_dev->blksz);
			memset(blk_buf, 0xFF, blk_dev->blksz);
			memcpy(blk_buf, buf + byte_off, copy_len);
			ret = mmc->block_dev.block_write(mmc_host.dev_num,
				blk_off + i, 1, blk_buf);
			if (ret < 0)
				break;
		}
		free(blk_buf);
		return (ret < 0) ? ret : 0;
	}
#endif

	base = art_part_offset();
	if (base < 0)
		return -ENOENT;

	if (is_nor_flash(sfi->flash_type)) {
		struct spi_flash *flash = spi_flash_probe(
			CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
			CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		size_t erase_size;
		if (!flash)
			return -ENODEV;
		erase_size = (size + flash->sector_size - 1) & ~(flash->sector_size - 1);
		ret = spi_flash_erase(flash, base + offset, erase_size);
		if (ret < 0)
			return ret;
		ret = spi_flash_write(flash, base + offset, size, buf);
	} else if (is_nand_flash(sfi->flash_type)) {
#if defined(CONFIG_NAND_FLASH)
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
		int idx = is_spi_nand_available();
#else
		int idx = CONFIG_NAND_FLASH_INFO_IDX;
#endif
		size_t len = size;
		ret = nand_erase(&nand_info[idx], base + offset, size);
		if (ret < 0)
			return ret;
		ret = nand_write(&nand_info[idx], base + offset, &len, (u_char *)buf);
		if (len != size)
			ret = -EIO;
#else
		return -EINVAL;
#endif
	} else {
		return -EINVAL;
	}

	return ret;
}

int macaddr_read_base(u8 *mac, int mac_index) {
	u8 *buf;
	int ret;

	if (!mac || mac_index < 0 || mac_index >= CONFIG_IPQ_NO_MACS)
		return -EINVAL;

	buf = memalign(ARCH_DMA_MINALIGN, ART_BASE_SIZE);
	if (!buf)
		return -ENOMEM;

	ret = art_read(0, ART_BASE_SIZE, buf);
	if (ret >= 0)
		memcpy(mac, &buf[ART_BASE_MAC0 + mac_index * ART_MAC_LEN], ART_MAC_LEN);

	free(buf);
	return ret;
}

int macaddr_modify_base(const u8 *new_mac, int mac_index) {
	u8 *buf;
	int ret;

	if (!new_mac || !is_valid_ethaddr(new_mac) ||
	    mac_index < 0 || mac_index >= CONFIG_IPQ_NO_MACS)
		return -EINVAL;

	buf = memalign(ARCH_DMA_MINALIGN, ART_BASE_SIZE);
	if (!buf)
		return -ENOMEM;

	ret = art_read(0, ART_BASE_SIZE, buf);
	if (ret < 0)
		goto out;

	memcpy(&buf[ART_BASE_MAC0 + mac_index * ART_MAC_LEN], new_mac, ART_MAC_LEN);

	ret = art_write(0, ART_BASE_SIZE, buf);
out:
	free(buf);
	return ret;
}

#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
int macaddr_read_wifi(u8 *mac, int wifi_index) {
	u8 hdr[2], *cal;
	u16 cal_size;
	int ret;

	if (!mac || wifi_index < 0 || wifi_index >= ATH10K_WIFI_COUNT)
		return -EINVAL;

	ret = art_read(cal_offsets[wifi_index], sizeof(hdr), hdr);
	if (ret < 0)
		return ret;

	cal_size = hdr[0] | (hdr[1] << 8);
	if (cal_size == 0xFFFF || cal_size == 0 || cal_size > 0x10000)
		return -ENODATA;

	cal = memalign(ARCH_DMA_MINALIGN, cal_size);
	if (!cal)
		return -ENOMEM;

	ret = art_read(cal_offsets[wifi_index], cal_size, cal);
	if (ret >= 0)
		memcpy(mac, &cal[ATH10K_CAL_MAC_OFF], ART_MAC_LEN);

	free(cal);
	return ret;
}

int macaddr_modify_wifi(const u8 *new_mac, int wifi_index) {
	u8 hdr[2], *cal;
	u16 cal_size, cs;
	int ret;

	if (!new_mac || !is_valid_ethaddr(new_mac) ||
	    wifi_index < 0 || wifi_index >= ATH10K_WIFI_COUNT)
		return -EINVAL;

	ret = art_read(cal_offsets[wifi_index], sizeof(hdr), hdr);
	if (ret < 0)
		return ret;

	cal_size = hdr[0] | (hdr[1] << 8);
	if (cal_size == 0xFFFF || cal_size == 0 || cal_size > 0x10000)
		return -ENODATA;

	cal = memalign(ARCH_DMA_MINALIGN, ATH10K_CAL_CHECKSUM_SIZE);
	if (!cal)
		return -ENOMEM;

	ret = art_read(cal_offsets[wifi_index], ATH10K_CAL_CHECKSUM_SIZE, cal);
	if (ret < 0)
		goto out;

	cs = xor16_checksum(cal, ATH10K_CAL_CHECKSUM_SIZE);
	if (cs != 0xFFFF)
		printf("ART cal XOR16 mismatch (0x%04X), updating anyway\n", cs);

	memcpy(&cal[ATH10K_CAL_MAC_OFF], new_mac, ART_MAC_LEN);

	*(u16 *)&cal[ATH10K_CAL_CRC] = cpu_to_le16(0xFFFF);
	cs = xor16_checksum(cal, ATH10K_CAL_CHECKSUM_SIZE);
	*(u16 *)&cal[ATH10K_CAL_CRC] = cpu_to_le16(cs);

	ret = art_write(cal_offsets[wifi_index], ATH10K_CAL_CHECKSUM_SIZE, cal);
out:
	free(cal);
	return ret;
}
#else
int macaddr_read_wifi(u8 *mac, int wifi_index) {
	u8 hdr[16], *cal;
	size_t cal_size;
	u32 cal_off;
	int mac_off, ret;

	if (!mac || wifi_index < 0 || wifi_index >= WIFI_CAL_MAX_IDX)
		return -EINVAL;

	cal_off = cal_offsets[wifi_index / 2];
	mac_off = ART_CAL_MAC0 + (wifi_index % 2) * ART_MAC_LEN;

	ret = art_read(cal_off, sizeof(hdr), hdr);
	if (ret < 0)
		return ret;

	if (hdr[0] != 0x01)
		return -ENOSYS;

	cal_size = art_cal_size(hdr);

	cal = memalign(ARCH_DMA_MINALIGN, cal_size);
	if (!cal)
		return -ENOMEM;

	ret = art_read(cal_off, cal_size, cal);
	if (ret >= 0)
		memcpy(mac, &cal[mac_off], ART_MAC_LEN);

	free(cal);
	return ret;
}

int macaddr_modify_wifi(const u8 *new_mac, int wifi_index) {
	u8 hdr[16], *cal;
	size_t cal_size;
	u32 cal_off;
	int mac_off;
	u16 cs;
	int ret;

	if (!new_mac || !is_valid_ethaddr(new_mac) ||
	    wifi_index < 0 || wifi_index >= WIFI_CAL_MAX_IDX)
		return -EINVAL;

	cal_off = cal_offsets[wifi_index / 2];
	mac_off = ART_CAL_MAC0 + (wifi_index % 2) * ART_MAC_LEN;

	ret = art_read(cal_off, sizeof(hdr), hdr);
	if (ret < 0)
		return ret;

	if (hdr[0] != 0x01)
		return -ENOSYS;

	cal_size = art_cal_size(hdr);

	cal = memalign(ARCH_DMA_MINALIGN, cal_size);
	if (!cal)
		return -ENOMEM;

	ret = art_read(cal_off, cal_size, cal);
	if (ret < 0)
		goto out;

	memset(&cal[ART_CAL_CRC], 0, 2);
	cs = xor16_checksum(cal, cal_size);
	if (cs != 0xFFFF)
		printf("ART cal XOR16 mismatch (0x%04X), updating anyway\n", cs);

	memcpy(&cal[mac_off], new_mac, ART_MAC_LEN);

	memset(&cal[ART_CAL_CRC], 0, 2);
	cs = xor16_checksum(cal, cal_size);
	*(u16 *)&cal[ART_CAL_CRC] = cpu_to_le16(cs ^ 0xFFFF);

	ret = art_write(cal_off, cal_size, cal);
out:
	free(cal);
	return ret;
}
#endif

static void print_mac(const char *name, const u8 *mac) {
	printf("%s: %02X:%02X:%02X:%02X:%02X:%02X\n", name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

u32 macaddr_wifi_offset(int wifi_index) {
	if (wifi_index < 0 || wifi_index >= MACADDR_WIFI_MAX)
		return 0;
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
	return cal_offsets[wifi_index] + ATH10K_CAL_MAC_OFF;
#else
	return cal_offsets[wifi_index / 2] + ART_CAL_MAC0 + (wifi_index % 2) * ART_MAC_LEN;
#endif
}

u32 macaddr_wifi_cs_offset(int wifi_index) {
	if (wifi_index < 0 || wifi_index >= MACADDR_WIFI_MAX)
		return 0;
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
	return cal_offsets[wifi_index];
#else
	return cal_offsets[wifi_index / 2] + ART_CAL_CRC;
#endif
}

u16 macaddr_wifi_checksum(int wifi_index) {
	u8 buf[2];
	u32 off = macaddr_wifi_cs_offset(wifi_index);
	if (wifi_index < 0 || wifi_index >= MACADDR_WIFI_MAX || art_read(off, 2, buf) < 0)
		return 0;
	return (buf[0] << 8) | buf[1];
}

int macaddr_wifi_cs_valid(int wifi_index) {
	u8 *cal;
	int ret;
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
	u32 cal_off = cal_offsets[wifi_index];
#else
	u8 hdr[16];
	size_t cal_size;
	u32 cal_off = cal_offsets[wifi_index / 2];
#endif

	if (wifi_index < 0 || wifi_index >= MACADDR_WIFI_MAX)
		return 0;

#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
	cal = memalign(ARCH_DMA_MINALIGN, ATH10K_CAL_CHECKSUM_SIZE);
	if (!cal)
		return 0;
	ret = art_read(cal_off, ATH10K_CAL_CHECKSUM_SIZE, cal);
	if (ret < 0) {
		free(cal);
		return 0;
	}
	ret = xor16_checksum(cal, ATH10K_CAL_CHECKSUM_SIZE) == 0xFFFF;
	free(cal);
#else
	ret = art_read(cal_off, sizeof(hdr), hdr);
	if (ret < 0 || hdr[0] != 0x01)
		return 0;
	cal_size = art_cal_size(hdr);
	cal = memalign(ARCH_DMA_MINALIGN, cal_size);
	if (!cal)
		return 0;
	ret = art_read(cal_off, cal_size, cal);
	if (ret < 0) {
		free(cal);
		return 0;
	}
	ret = xor16_checksum(cal, cal_size) == 0xFFFF;
	free(cal);
#endif
	return ret;
}

static int parse_iface(const char *arg, int *type, int *index) {
	if ((!strncmp(arg, "eth", 3) && arg[3] >= '0' && arg[3] <= '9') ||
	    (!strncmp(arg, "wifi", 4) && arg[4] >= '0' && arg[4] <= '9')) {
		*type = arg[0] == 'e' ? 0 : 1;
		*index = simple_strtoul(arg + (*type ? 4 : 3), NULL, 0);
		return 0;
	}
	return -1;
}

static int do_mac(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[]) {
	u8 mac[6];
	char name[8];
	int i, type, index, ret, is_write;

	if (argc == 1) {
		for (i = 0; i < CONFIG_IPQ_NO_MACS; i++) {
			if (macaddr_read_base(mac, i) >= 0) {
				sprintf(name, "eth%d", i);
				print_mac(name, mac);
			}
		}
		for (i = 0; i < MACADDR_WIFI_MAX; i++) {
			if (macaddr_read_wifi(mac, i) >= 0 && is_valid_ethaddr(mac)) {
				sprintf(name, "wifi%d", i);
				print_mac(name, mac);
			}
		}
		return CMD_RET_USAGE;
	}

	if (argc < 3 || parse_iface(argv[2], &type, &index) < 0)
		return CMD_RET_USAGE;

	is_write = strcmp(argv[1], "w") == 0;
	if (!is_write && strcmp(argv[1], "r") != 0)
		return CMD_RET_USAGE;

	if (is_write) {
		if (argc != 4)
			return CMD_RET_USAGE;
		eth_parse_enetaddr(argv[3], mac);
		if (!is_valid_ethaddr(mac)) {
			printf("Invalid MAC address\n");
			return CMD_RET_FAILURE;
		}
	}

	ret = is_write
		? (type == 0 ? macaddr_modify_base(mac, index) : macaddr_modify_wifi(mac, index))
		: (type == 0 ? macaddr_read_base(mac, index) : macaddr_read_wifi(mac, index));
	if (ret < 0) {
		printf("MAC %s failed: %d\n", is_write ? "write" : "read", ret);
		return CMD_RET_FAILURE;
	}

	if (is_write)
		printf("MAC address updated\n");
	else
		print_mac(argv[2], mac);
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(mac, 4, 0, do_mac,
	"Read/Write MAC address in ART partition",
	"- Read all MAC addresses\n"
	"  r eth|wifi<n>       - Read MAC\n"
	"  w eth|wifi<n> <mac> - Write MAC\n"
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
	"wifi0:2.4G wifi1:5G wifi2:QCA988x"
#else
	"wifi0:2.4G wifi1:5G (wifi2/3:2nd wifi4/5:3rd)"
#endif
);