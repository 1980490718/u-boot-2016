/*
 * Copyright (c) 2025, The Linux Foundation. All rights reserved.
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

#include <common.h>
#include <net.h>
#include <asm/io.h>
#include <config.h>
#include "rtl8372n_switch.h"

extern int ipq_mdio_write(int mii_id, int regnum, u16 value);
extern int ipq_mdio_read(int mii_id, int regnum, ushort *data);

static int rtl8372n_mdio_wait_busy(u32 mdio_addr) {
	u16 ctrl = RTL8372N_MDC_MDIO_BUSY_BIT;
	int i;

	for (i = 0; i < RTL8372N_SDS_BUSY_POLL_CNT; i++) {
		ipq_mdio_read(mdio_addr, RTL8372N_MDC_MDIO_CTRL_REG, &ctrl);
		if (!(ctrl & RTL8372N_MDC_MDIO_BUSY_BIT))
			return 0;
		udelay(10);
	}

#ifdef CONFIG_RTL8372N_DEBUG
	printf("RTL8372N: MDIO busy timeout\n");
#endif
	return -1;
}

static int rtl8372n_reg_read(u32 mdio_addr, u32 reg, u32 *pval) {
	u16 val_l, val_h;

	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_ADDR_REG, (u16)(reg & 0xFFFF));
	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_CTRL_REG, RTL8372N_MDC_MDIO_READ_CMD);

	if (rtl8372n_mdio_wait_busy(mdio_addr) < 0) {
		if (pval)
			*pval = 0;
		return -1;
	}

	ipq_mdio_read(mdio_addr, RTL8372N_MDC_MDIO_DATA_LOW, &val_l);
	ipq_mdio_read(mdio_addr, RTL8372N_MDC_MDIO_DATA_HIGH, &val_h);

	if (pval)
		*pval = ((u32)val_h << 16) | val_l;
	return 0;
}

static int rtl8372n_reg_write(u32 mdio_addr, u32 reg, u32 val) {
	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_ADDR_REG, (u16)(reg & 0xFFFF));
	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_DATA_LOW, (u16)(val & 0xFFFF));
	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_DATA_HIGH, (u16)((val >> 16) & 0xFFFF));
	ipq_mdio_write(mdio_addr, RTL8372N_MDC_MDIO_CTRL_REG, RTL8372N_MDC_MDIO_WRITE_CMD);

	return rtl8372n_mdio_wait_busy(mdio_addr);
}

static int rtl8372n_reg_set_bits(u32 mdio_addr, u32 reg, u32 mask, u32 val) {
	u32 reg_val, shift;

	if (!mask)
		return 0;

	shift = 0;
	while (!(mask & (1 << shift)))
		shift++;

	if (shift >= 32 || (val & ~(mask >> shift)) || rtl8372n_reg_read(mdio_addr, reg, &reg_val) < 0)
		return -1;

	reg_val = (reg_val & ~mask) | ((val << shift) & mask);
	return rtl8372n_reg_write(mdio_addr, reg, reg_val);
}

static int rtl8372n_phy_access(u32 mdio_addr, u32 phy_mask_or_id, u32 page, u32 reg, u16 val, int is_write) {
	u32 ctrl1, cmd_val;
	int i;

	rtl8372n_reg_write(mdio_addr, RTL8372N_SMI_ACCESS_PHY_CTRL_0_ADDR, phy_mask_or_id);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SMI_ACCESS_PHY_CTRL_3_ADDR, RTL8372N_SMI_ACCESS_PHY_CTRL_3_INDATA_MASK, is_write ? val : phy_mask_or_id);

	ctrl1 = (page << RTL8372N_SMI_ACCESS_PHY_CTRL_1_MMD_DEVAD_OFFSET) | (reg << RTL8372N_SMI_ACCESS_PHY_CTRL_1_MMD_REG_OFFSET) |
		((is_write ? 1 : 0) << RTL8372N_SMI_ACCESS_PHY_CTRL_1_RWOP_OFFSET) | (1 << RTL8372N_SMI_ACCESS_PHY_CTRL_1_TYPE_OFFSET) |
		(1 << RTL8372N_SMI_ACCESS_PHY_CTRL_1_CMD_OFFSET);
	rtl8372n_reg_write(mdio_addr, RTL8372N_SMI_ACCESS_PHY_CTRL_1_ADDR, ctrl1);

	for (i = 0; i < RTL8372N_SMI_ACCESS_PHY_CTRL_MAX_POLL; i++) {
		if (rtl8372n_reg_read(mdio_addr, RTL8372N_SMI_ACCESS_PHY_CTRL_1_ADDR, &cmd_val) >= 0 &&
		    !(cmd_val & ((1 << RTL8372N_SMI_ACCESS_PHY_CTRL_1_CMD_OFFSET) | RTL8372N_SMI_ACCESS_PHY_CTRL_1_FAIL_MASK)))
			return 0;
		udelay(10);
	}

#ifdef CONFIG_RTL8372N_DEBUG
	printf("RTL8372N: PHY %s timeout (mask=0x%x page=%d reg=0x%x)\n", is_write ? "write" : "read", phy_mask_or_id, page, reg);
#endif
	return -1;
}

static int rtl8372n_phy_write(u32 mdio_addr, u32 phy_mask, u32 page, u32 reg, u16 val) {
	return rtl8372n_phy_access(mdio_addr, phy_mask, page, reg, val, 1);
}

static int rtl8372n_phy_read(u32 mdio_addr, u32 phy_id, u32 page, u32 reg, u16 *pval) {
	u32 data;

	if (rtl8372n_phy_access(mdio_addr, phy_id, page, reg, 0, 0) < 0) {
		if (pval)
			*pval = 0;
		return -1;
	}

	if (pval) {
		rtl8372n_reg_read(mdio_addr, RTL8372N_SMI_ACCESS_PHY_CTRL_2_ADDR, &data);
		*pval = (u16)(data & 0xFFFF);
	}
	return 0;
}

static int rtl8372n_phy_bits_read(u32 mdio_addr, u32 phy_id, u32 page, u32 reg, u16 mask, u16 *pval) {
	u16 val;
	int ret, shift = 0;

	ret = rtl8372n_phy_read(mdio_addr, phy_id, page, reg, &val);
	if (ret < 0)
		return ret;

	if (pval) {
		while (!(mask & (1 << shift)))
			shift++;
		*pval = (val & mask) >> shift;
	}
	return 0;
}

static int rtl8372n_phy_bits_write(u32 mdio_addr, u32 phy_id, u32 page, u32 reg, u16 mask, u16 val) {
	u16 old_val;
	int ret, shift = 0;

	if (!mask)
		return 0;

	while (!(mask & (1 << shift)))
		shift++;

	ret = rtl8372n_phy_read(mdio_addr, phy_id, page, reg, &old_val);
	if (ret < 0)
		return ret;

	old_val &= ~mask;
	old_val |= (val << shift) & mask;

	return rtl8372n_phy_write(mdio_addr, 1 << phy_id, page, reg, old_val);
}

static int rtl8372n_uc1_sram_read_8b(u32 mdio_addr, u32 phy, u16 addr, u16 *pval) {
	int ret;
	ret = rtl8372n_phy_write(mdio_addr, 1 << phy, 31, 0xa436, addr);
	if (ret) return ret;
	return rtl8372n_phy_bits_read(mdio_addr, phy, 31, 0xa438, 0xff << 8, pval);
}

static int rtl8372n_uc1_sram_write_8b(u32 mdio_addr, u32 phy, u16 addr, u16 val) {
	int ret;
	ret = rtl8372n_phy_write(mdio_addr, 1 << phy, 31, 0xa436, addr);
	if (ret) return ret;
	return rtl8372n_phy_bits_write(mdio_addr, phy, 31, 0xa438, 0xff << 8, val);
}

static int rtl8372n_uc2_sram_write_8b(u32 mdio_addr, u32 phy, u16 addr, u16 val) {
	int ret;
	ret = rtl8372n_phy_write(mdio_addr, 1 << phy, 31, 0xb87c, addr);
	if (ret) return ret;
	return rtl8372n_phy_bits_write(mdio_addr, phy, 31, 0xb87e, 0xff << 8, val);
}

static int rtl8372n_data_ram_write_8b(u32 mdio_addr, u32 phy, u16 addr, u16 val) {
	int ret;
	u16 bits;

	ret = rtl8372n_phy_write(mdio_addr, 1 << phy, 31, 0xb88e, addr);
	if (ret) return ret;

	bits = (addr & 1) ? 0xff : 0xff << 8;
	return rtl8372n_phy_bits_write(mdio_addr, phy, 31, 0xB890, bits, val);
}

static const struct rtl8372n_ext_reg_patch RTCT_para_6818C_231206_patch[] = {
	{0x81a3, 0x32}, {0x81a4, 0xc0}, {0x81a5, 0x32}, {0x81a8, 0x1d},	{0x81af, 0x25}, {0x81b2, 0x09},
	{0x81b6, 0x3f}, {0x81b7, 0x48},	{0x81b8, 0x0c}, {0x81b9, 0x04}, {0x81ba, 0x00}, {0x81bb, 0x20},
	{0x81bc, 0x04}, {0x81bd, 0x20}, {0x81be, 0x1a}, {0x81bf, 0xe0},	{0x81c0, 0x01}, {0x81c1, 0x3a},
	{0x81c2, 0x1c}, {0x81c3, 0x60},	{0x81c4, 0x00}, {0x81c5, 0x11}, {0x81c6, 0x00}, {0x81c7, 0xcf},
	{0x81c8, 0xff}, {0x81c9, 0xb0}, {0x81ca, 0x00}, {0x81cb, 0x0f},	{0x81cc, 0x11}, {0x81cd, 0xc0},
	{0x81ce, 0x00}, {0x81cf, 0x0e},	{0x81d0, 0xff}, {0x81d1, 0xbe}, {0x81d2, 0x00}, {0x81d3, 0x18},
	{0x81d4, 0x08}, {0x81d5, 0x70}, {0x81d6, 0xff}, {0x81d7, 0x37},	{0x81d8, 0x00}, {0x81d9, 0x48},
	{0x81da, 0xff}, {0x81db, 0xf4}, {0x81dc, 0xeb}, {0x81dd, 0xa0}, {0x81de, 0x00}, {0x81df, 0x2b},
	{0x81e0, 0x00}, {0x81e1, 0x9c}, {0x81e2, 0xff}, {0x81e3, 0xb2},	{0x81e4, 0xeb}, {0x81e5, 0xaf},
	{0x81e6, 0x02}, {0x81e7, 0x92},	{0x81e8, 0xfe}, {0x81e9, 0xe4}, {0x81ea, 0x00}, {0x81eb, 0x3c},
	{0x81ec, 0x06}, {0x81ed, 0xf2}, {0x81ee, 0xff}, {0x81ef, 0x3a},	{0x81f0, 0x00}, {0x81f1, 0x8a},
	{0x81f2, 0xff}, {0x81f3, 0xd0},	{0x81f4, 0x0a}, {0x81f5, 0x09}, {0x81f6, 0xff}, {0x81f7, 0xdc},
	{0x81f8, 0xff}, {0x81f9, 0x30}, {0x81fa, 0x00}, {0x8700, 0x01},	{0x8701, 0x04}, {0x8018, 0x70},
	{0x81a6, 0xc0}, {0x81a9, 0x00},	{0x81b0, 0x05}, {0x81b3, 0x1d}, {0x81fb, 0x6c}, {0x8702, 0x50},
};

static const struct rtl8372n_sds_patch rtl8372n_10g_an_patch[] = {
	{ 0x21, 0x10, 0x4480 }, { 0x21, 0x13, 0x0400 }, { 0x21, 0x18, 0x6d02 },
	{ 0x21, 0x1b, 0x424e },	{ 0x21, 0x1d, 0x0002 }, { 0x36, 0x1c, 0x1390 },
	{ 0x36, 0x14, 0x003f },	{ 0x36, 0x10, 0x0200 },	{ 0x2e, 0x04, 0x0080 },
	{ 0x2e, 0x06, 0x0408 }, { 0x2e, 0x07, 0x020d },	{ 0x2e, 0x09, 0x0601 },
	{ 0x2e, 0x0b, 0x222c }, { 0x2e, 0x0c, 0xa217 }, { 0x2e, 0x0d, 0xfe40 },
	{ 0x2e, 0x15, 0xf5c1 },	{ 0x2e, 0x16, 0x0443 }, { 0x2e, 0x1d, 0xabb0 },
};

static const struct rtl8372n_sds_patch rtl8372n_10g_mac_patch[] = {
	{ 0x06, 0x12, 0x5078 }, { 0x07, 0x06, 0x9401 }, { 0x07, 0x08, 0x9401 }, { 0x07, 0x0a, 0x9401 },
	{ 0x07, 0x0c, 0x9401 }, { 0x1f, 0x0b, 0x0003 }, { 0x06, 0x03, 0xc45c }, { 0x06, 0x1f, 0x2100 },
};

static int rtl8372n_RTCT_para_6818C_231206(u32 mdio_addr, u32 port_mask) {
	int port_index, i;

	for (port_index = 0; port_index < 8; port_index++) {
		if (!(port_mask & (1 << port_index)))
			continue;

		for (i = 0; i < ARRAY_SIZE(RTCT_para_6818C_231206_patch); i++) {
			if (rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, RTCT_para_6818C_231206_patch[i].addr) < 0 ||
			    rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xA438, 0xFF00, RTCT_para_6818C_231206_patch[i].data) < 0)
				return -1;
		}
	}
	return 0;
}

static int rtl8372n_data_ram_patch_6818C_221026(u32 mdio_addr, u32 port_mask) {
	int port_index;

	for (port_index = 0; port_index < 8; port_index++) {
		if (!(port_mask & (1 << port_index)))
			continue;

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB876, 0x1, 0);
		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB872, 0xFF00, 0);
		rtl8372n_data_ram_write_8b(mdio_addr, port_index, 0xC206, 0xB1);
		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB876, 0x1, 1);
	}

	return 0;
}

static int rtl8372n_afe_patch_6818C_220607(u32 mdio_addr, u32 port_mask) {
	int port_index;

	for (port_index = 0; port_index < 8; port_index++) {
		if (!(port_mask & (1 << port_index)))
			continue;

		if (rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xBF84, 0x7, 4) < 0 ||
		    rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xBF8C, 0x7C0, 0) < 0)
			return -1;
	}

	return 0;
}

static int rtl8372n_patch_phy_v008(u32 mdio_addr, u32 port_mask, u32 *patched_mask) {
	int port_index, ret, i;
	u16 ic_ver, fw_ver, tmp16;

	*patched_mask = 0;

	for (port_index = 0; port_index < 8; port_index++) {

		if (!(port_mask & (1 << port_index)))
			continue;

		ret = rtl8372n_uc1_sram_read_8b(mdio_addr, port_index, 0x0005, &ic_ver);
		if (ret) return ret;
		if (ic_ver != 2)
			continue;

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0x801E);
		rtl8372n_phy_read(mdio_addr, port_index, 31, 0xA438, &fw_ver);

		if (fw_ver == 0x008)
			continue;

#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: PHY%d fw_ver=0x%04x, applying v008 patch...\n", port_index, fw_ver);
#endif

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB820, 0x10, 1);

		for (i = 0; i < 30; i++) {
			rtl8372n_phy_bits_read(mdio_addr, port_index, 31, 0xB800, 0x40, &tmp16);
			if (tmp16 == 1) break;
		}
		if (i == 30) return -1;

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0x8023);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA438, 0x1802);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0xB82E);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA438, 1);

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB820, 0x80, 1);
		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB820, 0x80, 0);

		rtl8372n_data_ram_patch_6818C_221026(mdio_addr, 1 << port_index);

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA438, 0);
		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB82E, 0x1, 0);

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0x8023);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA438, 0);

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xB820, 0x10, 0);

		for (i = 0; i < 30; i++) {
			rtl8372n_phy_bits_read(mdio_addr, port_index, 31, 0xB800, 0x40, &tmp16);
			if (tmp16 == 0) break;
		}
		if (i == 30) return -1;

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xA4A0, 0x400, 1);

		for (i = 0; i < 30; i++) {
			rtl8372n_phy_bits_read(mdio_addr, port_index, 31, 0xA600, 0xFF, &tmp16);
			if (tmp16 == 1) break;
		}
		if (i == 30) return -1;

		rtl8372n_RTCT_para_6818C_231206(mdio_addr, 1 << port_index);

		rtl8372n_uc1_sram_write_8b(mdio_addr, port_index, 0x8FFB, 1);
		rtl8372n_uc1_sram_write_8b(mdio_addr, port_index, 0x80DC, 0xA);
		rtl8372n_uc1_sram_write_8b(mdio_addr, port_index, 0x8378, 0x22);

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xA47E, 0xC0, 1);

		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8217, 0x1E);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8384, 4);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD6, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD7, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD8, 0xC);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD9, 0x80);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDA, 0xA);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDB, 0x19);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDC, 0x19);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDD, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDE, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FDF, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FE0, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FE1, 0x20);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FE2, 0xC);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD3, 0);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD4, 0x15);
		rtl8372n_uc2_sram_write_8b(mdio_addr, port_index, 0x8FD5, 0x15);

		rtl8372n_afe_patch_6818C_220607(mdio_addr, 1 << port_index);

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA5D0, 0);
		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xA428, 0x200, 0);

		*patched_mask |= (1 << port_index);

#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: PHY%d v008 patch applied\n", port_index);
#endif
	}

	return 0;
}

static int rtl8372n_patch_phy_v008_rls_lockmain(u32 mdio_addr, u32 port_mask) {
	int port_index, i;
	u16 tmp16;

	for (port_index = 0; port_index < 8; port_index++) {

		if (!(port_mask & (1 << port_index)))
			continue;

		rtl8372n_phy_bits_write(mdio_addr, port_index, 31, 0xA4A0, 0x400, 0);

		for (i = 0; i < 30; i++) {
			rtl8372n_phy_bits_read(mdio_addr, port_index, 31, 0xA600, 0xFF, &tmp16);
			if (tmp16 != 1) break;
		}
		if (i == 30) return -1;

		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA436, 0x801E);
		rtl8372n_phy_write(mdio_addr, 1 << port_index, 31, 0xA438, 0x008);
	}

	return 0;
}

static int rtl8372n_chip_probe(u32 mdio_addr) {
	u32 reg_val;

	if (rtl8372n_reg_read(mdio_addr, 0x4, &reg_val) < 0 ||
	    (reg_val >> 8) != RTL8372N_CHIP_ID) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: chip probe failed, id=0x%x (expected 0x%x)\n", reg_val >> 8, RTL8372N_CHIP_ID);
#endif
		return -1;
	}

	return 0;
}

static int rtl8372n_sds_wait_cmd_done(u32 mdio_addr) {
	u32 cmd = (1 << RTL8372N_SDS_INDACS_CMD_SDS_CMD_OFFSET);
	int i;

	for (i = 0; i < RTL8372N_SDS_BUSY_POLL_CNT; i++) {
		if (rtl8372n_reg_read(mdio_addr, RTL8372N_SDS_INDACS_CMD_ADDR, &cmd) >= 0 &&
		    !((cmd >> RTL8372N_SDS_INDACS_CMD_SDS_CMD_OFFSET) & 1))
			break;
		udelay(10);
	}

	return (i == RTL8372N_SDS_BUSY_POLL_CNT) ? -1 : 0;
}

static int rtl8372n_sds_access(u32 mdio_addr, u32 sds_index, u32 sds_page, u32 sds_reg, int is_write) {
	u32 cmd, fields_mask;

	if (sds_index > 1)
		return -1;

	if (rtl8372n_sds_wait_cmd_done(mdio_addr) < 0) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: SDS %s busy timeout\n", is_write ? "write" : "read");
#endif
		return -1;
	}

	fields_mask = RTL8372N_SDS_INDACS_CMD_SDS_PAGE_MASK | RTL8372N_SDS_INDACS_CMD_SDS_REGAD_MASK |
		      (1 << RTL8372N_SDS_INDACS_CMD_SDS_INDEX_OFFSET) | (1 << RTL8372N_SDS_INDACS_CMD_SDS_RWOP_OFFSET);

	if (rtl8372n_reg_read(mdio_addr, RTL8372N_SDS_INDACS_CMD_ADDR, &cmd) < 0)
		return -1;

	cmd &= ~fields_mask;
	cmd |= (sds_index << RTL8372N_SDS_INDACS_CMD_SDS_INDEX_OFFSET) | (sds_page << RTL8372N_SDS_INDACS_CMD_SDS_PAGE_OFFSET) |
	       (sds_reg << RTL8372N_SDS_INDACS_CMD_SDS_REGAD_OFFSET) | (is_write << RTL8372N_SDS_INDACS_CMD_SDS_RWOP_OFFSET);

	rtl8372n_reg_write(mdio_addr, RTL8372N_SDS_INDACS_CMD_ADDR, cmd);
	cmd |= (1 << RTL8372N_SDS_INDACS_CMD_SDS_CMD_OFFSET);
	rtl8372n_reg_write(mdio_addr, RTL8372N_SDS_INDACS_CMD_ADDR, cmd);

	if (rtl8372n_sds_wait_cmd_done(mdio_addr) < 0) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: SDS %s cmd timeout\n", is_write ? "write" : "read");
#endif
		return -1;
	}

	return 0;
}

static int rtl8372n_sds_reg_read(u32 mdio_addr, u32 sds_index, u32 sds_page, u32 sds_reg, u32 *pdata) {
	if (rtl8372n_sds_access(mdio_addr, sds_index, sds_page, sds_reg, 0) < 0)
		return -1;
	if (pdata)
		rtl8372n_reg_read(mdio_addr, RTL8372N_SDS_INDACS_RD_ADDR, pdata);
	return 0;
}

static int rtl8372n_sds_reg_write(u32 mdio_addr, u32 sds_index, u32 sds_page, u32 sds_reg, u32 regdata) {
	rtl8372n_reg_write(mdio_addr, RTL8372N_SDS_INDACS_WD_ADDR, regdata);
	return rtl8372n_sds_access(mdio_addr, sds_index, sds_page, sds_reg, 1);
}

static int rtl8372n_sds_regbits_write(u32 mdio_addr, u32 sds_index, u32 sds_page, u32 sds_reg, u32 bitmask, u32 value) {
	u32 regdata, bits_shift;

	if (!bitmask)
		return 0;

	bits_shift = 0;
	while (!(bitmask & (1 << bits_shift)))
		bits_shift++;

	if (bits_shift >= 32 || (value & ~(bitmask >> bits_shift)) || rtl8372n_sds_reg_read(mdio_addr, sds_index, sds_page, sds_reg, &regdata) < 0)
		return -1;

	regdata = (regdata & ~bitmask) | ((value << bits_shift) & bitmask);
	return rtl8372n_sds_reg_write(mdio_addr, sds_index, sds_page, sds_reg, regdata);
}

static int rtl8372n_fw_reset_flow_tgr(u32 mdio_addr, u32 sds) {
	u32 value;

	if (rtl8372n_sds_regbits_write(mdio_addr, sds, 0x21, 0, 0x4, 1) < 0 ||
	    rtl8372n_sds_regbits_write(mdio_addr, sds, 0x36, 5, 0x7800, 8) < 0 ||
	    rtl8372n_sds_reg_write(mdio_addr, sds, 0x1f, 2, 0x1f) < 0)
		return -1;
	if (rtl8372n_sds_reg_read(mdio_addr, sds, 0x1f, 0x15, &value) < 0)
		return -1;
	if (!((value & 0x40) | ((value & 0x80) == 0)))
		return 0;
	if (rtl8372n_sds_reg_read(mdio_addr, sds, 5, 0, &value) < 0)
		return -1;
	if (value & 1) {
		if (rtl8372n_sds_reg_read(mdio_addr, sds, 5, 0, &value) < 0)
			return -1;
		if (!((value & 0x2) | ((value & 0x1000) == 0)))
			return 0;
	}

	if (rtl8372n_sds_regbits_write(mdio_addr, sds, 0x20, 0, 0x30, 3) < 0 || rtl8372n_sds_regbits_write(mdio_addr, sds, 0x20, 0, 0x30, 1) < 0 ||
	    rtl8372n_sds_regbits_write(mdio_addr, sds, 0x20, 0, 0x30, 3) < 0 || rtl8372n_sds_regbits_write(mdio_addr, sds, 0x20, 0, 0x30, 0) < 0)
		return -1;

	if (!(value & 1))
		return rtl8372n_sds_reg_read(mdio_addr, sds, 5, 0, &value);
	return 0;
}

static int rtl8372n_sds_apply_patch(u32 mdio_addr, const struct rtl8372n_sds_patch *patch, size_t count) {
	size_t i;

	for (i = 0; i < count; i++) {
		if (rtl8372n_sds_reg_write(mdio_addr, 1, patch[i].page, patch[i].reg, patch[i].val) < 0)
			return -1;
	}

	return 0;
}

static int rtl8372n_sds_power_down(u32 mdio_addr) {
	static const struct { u16 mask;	u8 val;	u16 delay_us; } steps[] = {
		{ 0x0030, 3, 10 }, { 0x0030, 1, 100 }, { 0x00c0, 1, 10 },
		{ 0x00c0, 3, 100 }, { 0x0c00, 3, 10 }, { 0x0c00, 1, 100 },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(steps); i++) {
		if (rtl8372n_sds_regbits_write(mdio_addr, 1, 0x20, 0, steps[i].mask, steps[i].val) < 0)
			return -1;
		udelay(steps[i].delay_us);
	}

	return 0;
}

static int rtl8372n_sds_mode_toggle(u32 mdio_addr) {
	static const struct { u16 mask; u8 val; u16 delay_us; } steps[] = {
		{ 0x0030, 3, 10 }, { 0x0030, 1, 100 }, { 0x00c0, 1, 10 },
		{ 0x00c0, 3, 100 }, { 0x0c00, 3, 10 }, { 0x0c00, 1, 10 },
		{ 0x0c00, 1, 10 }, { 0x0c00, 3, 100 }, { 0x0c00, 0, 10 },
		{ 0x00c0, 3, 10 }, { 0x00c0, 1, 100 }, { 0x00c0, 0, 10 },
		{ 0x0030, 1, 10 }, { 0x0030, 3, 100 }, { 0x0030, 0, 100 },
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(steps); i++) {
		if (rtl8372n_sds_regbits_write(mdio_addr, 1, 0x20, 0, steps[i].mask, steps[i].val) < 0)
			return -1;
		udelay(steps[i].delay_us);
	}

	if (rtl8372n_sds_reg_write(mdio_addr, 1, 0x1f, 0, 0x000b) < 0 ||
	    (udelay(100), rtl8372n_sds_reg_write(mdio_addr, 1, 0x1f, 0, 0x0000) < 0))
		return -1;
	udelay(100);

	return 0;
}

static int rtl8372n_sds_mode_set(u32 mdio_addr, u32 sds_index, u32 sds_mode) {
	u32 usx_mask, mode_mask;

	if (sds_index > 1)
		return -1;

	if (sds_mode == RTL8372N_SDS_MODE_OFF)
		return 0;

	if (sds_mode != RTL8372N_SDS_MODE_SGMII && sds_mode != RTL8372N_SDS_MODE_1000BASEX && sds_mode != RTL8372N_SDS_MODE_HSGMII &&
		sds_mode != RTL8372N_SDS_MODE_2500BASEX && sds_mode != RTL8372N_SDS_MODE_10GQXG && sds_mode != RTL8372N_SDS_MODE_10GKR) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: invalid SDS%d mode 0x%x\n", sds_index, sds_mode);
#endif
		return -1;
	}

	usx_mask = (sds_index == 0) ? RTL8372N_SDS_MODE_SEL_SDS0_USX_SUB_MODE_MASK : RTL8372N_SDS_MODE_SEL_SDS1_USX_SUB_MODE_MASK;
	mode_mask = (sds_index == 0) ? RTL8372N_SDS_MODE_SEL_SDS0_MODE_SEL_MASK : RTL8372N_SDS_MODE_SEL_SDS1_MODE_SEL_MASK;

	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, usx_mask, 0);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, mode_mask, 0x1A);

	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, usx_mask, sds_mode == RTL8372N_SDS_MODE_10GQXG ? 2 : 0);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, mode_mask, sds_mode == RTL8372N_SDS_MODE_10GQXG ? 0xD : sds_mode);

	return 0;
}

static int rtl8372n_port_isolation_setup(u32 mdio_addr, u32 cpu_port, u32 port_mask) {
	u32 user_ports = port_mask & ~BIT(cpu_port), pmsk;
	int port;

	for (port = 0; port <= 8; port++) {
		if (!(port_mask & BIT(port)))
			continue;

		pmsk = (port == cpu_port) ? user_ports : BIT(cpu_port);
		if (rtl8372n_reg_write(mdio_addr, RTL8372N_PORT_ISO_PORT_PMSK_ADDR(port), pmsk) < 0) {
#ifdef CONFIG_RTL8372N_DEBUG
			printf("RTL8372N: port %d isolation set failed\n", port);
#endif
			return -1;
		}
	}

#ifdef CONFIG_RTL8372N_DEBUG
	printf("RTL8372N: port isolation done (cpu=%d, users=0x%x)\n", cpu_port, user_ports);
#endif
	return 0;
}

static int _rtl8372n_switch_init(u32 mdio_addr, u32 sds0_mode, u32 sds1_mode, u32 cpu_port, u32 port_mask) {
	u32 init_state, reg_val, reg_index, user_mask, patched_mask;
	int port;

	if ((cpu_port != 3 && cpu_port != 8) ||
	    (cpu_port == 3 ? sds0_mode : cpu_port == 8 ? sds1_mode : RTL8372N_SDS_MODE_OFF) == RTL8372N_SDS_MODE_OFF) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: bad cpu_port=%d\n", cpu_port);
#endif
		return -1;
	}

	rtl8372n_reg_read(mdio_addr, 0x7F60, &reg_val);
	init_state = reg_val & 0x3;
	if (init_state != 2) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: init_state=%d (expected 2), chip not ready\n", init_state);
#endif
		return -1;
	}

	rtl8372n_reg_set_bits(mdio_addr, 0x6330, 0x30000, 0);
	rtl8372n_reg_set_bits(mdio_addr, 0x6330, 0xC0, 0);
	rtl8372n_reg_set_bits(mdio_addr, 0x6334, 0xF0, 0xF);
	rtl8372n_reg_set_bits(mdio_addr, 0x6454, 0x7000, 7);
	mdelay(1);

	rtl8372n_sds_regbits_write(mdio_addr, 0, 0, 0, 0x200, 1);
	rtl8372n_sds_regbits_write(mdio_addr, 0, 6, 2, 0x2000, 1);
	rtl8372n_sds_regbits_write(mdio_addr, 1, 7, 16, 0xff, 3);
	mdelay(5);

	rtl8372n_fw_reset_flow_tgr(mdio_addr, 1);
	mdelay(5);

	rtl8372n_fw_reset_flow_tgr(mdio_addr, 0);

	rtl8372n_phy_write(mdio_addr, port_mask & ~BIT(cpu_port), 0x1f, 0xA610, 0x2858);

	rtl8372n_reg_set_bits(mdio_addr, 0x5FD4, 0x180000, 3);

	for (port = 0; port <= 8; port++) {
		if (!(port_mask & BIT(port)))
			continue;
		rtl8372n_reg_set_bits(mdio_addr, RTL8372N_MAC_L2_PORT_CTRL_ADDR(port), 0x110, 0x11);
	}

	rtl8372n_reg_set_bits(mdio_addr, 0x0B7C, 0x20, 1);

	reg_index = 0x7124;
	do {
		rtl8372n_reg_write(mdio_addr, reg_index, 0x1050);
		reg_index += 4;
	} while (reg_index != 0x714C);

	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_DW8051_CFG_ADDR, RTL8372N_DW8051_CFG_DW8051_READY_MASK, 1);

	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SMI_PORT0_5_ADDR_CTRL_ADDR, RTL8372N_SMI_PORT0_5_ADDR_CTRL_PORT4_ADDR_MASK, 4);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SMI_PORT0_5_ADDR_CTRL_ADDR, RTL8372N_SMI_PORT0_5_ADDR_CTRL_PORT5_ADDR_MASK, 5);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SMI_PORT6_9_ADDR_CTRL_ADDR, RTL8372N_SMI_PORT6_9_ADDR_CTRL_PORT6_ADDR_MASK, 6);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_SMI_PORT6_9_ADDR_CTRL_ADDR, RTL8372N_SMI_PORT6_9_ADDR_CTRL_PORT7_ADDR_MASK, 7);

	user_mask = port_mask & ~BIT(cpu_port);

	if (rtl8372n_patch_phy_v008(mdio_addr, user_mask, &patched_mask) < 0) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: PHY v008 patch failed (non-fatal)\n");
#endif
	}

	rtl8372n_patch_phy_v008_rls_lockmain(mdio_addr, patched_mask);
	rtl8372n_phy_write(mdio_addr, user_mask, 0x1f, 0xA610, 0x2058);

	rtl8372n_reg_set_bits(mdio_addr, 0x632C, 0x1FF000, port_mask);
	mdelay(5);

	rtl8372n_sds_mode_set(mdio_addr, 0, sds0_mode);
	rtl8372n_sds_mode_set(mdio_addr, 1, sds1_mode);

	if (sds1_mode != RTL8372N_SDS_MODE_OFF) {
		rtl8372n_sds_apply_patch(mdio_addr, rtl8372n_10g_an_patch, ARRAY_SIZE(rtl8372n_10g_an_patch));
		rtl8372n_sds_apply_patch(mdio_addr, rtl8372n_10g_mac_patch, ARRAY_SIZE(rtl8372n_10g_mac_patch));
		rtl8372n_sds_power_down(mdio_addr);
		rtl8372n_sds_mode_toggle(mdio_addr);
		rtl8372n_fw_reset_flow_tgr(mdio_addr, 1);
	}

	rtl8372n_fw_reset_flow_tgr(mdio_addr, 1);

	rtl8372n_reg_write(mdio_addr, RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(cpu_port), 1);
	mdelay(1);
	rtl8372n_reg_write(mdio_addr, RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(cpu_port), RTL8372N_STOCK_CPU_FORCE);

	rtl8372n_sds_power_down(mdio_addr);
	rtl8372n_reg_write(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, RTL8372N_STOCK_SDS_MODE);

	if (sds1_mode != RTL8372N_SDS_MODE_OFF) {
		rtl8372n_sds_apply_patch(mdio_addr, rtl8372n_10g_an_patch, ARRAY_SIZE(rtl8372n_10g_an_patch));
		rtl8372n_sds_apply_patch(mdio_addr, rtl8372n_10g_mac_patch, ARRAY_SIZE(rtl8372n_10g_mac_patch));
	}

	rtl8372n_sds_regbits_write(mdio_addr, 1, 7, 0x11, 0x000f, 0x000f);
	rtl8372n_sds_mode_toggle(mdio_addr);
	rtl8372n_fw_reset_flow_tgr(mdio_addr, 1);

	for (port = 0; port <= 8; port++) {
		if (port == cpu_port || !(port_mask & BIT(port)))
			continue;
		rtl8372n_reg_write(mdio_addr, RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(port), RTL8372N_STOCK_USER_FORCE);
	}

	rtl8372n_reg_write(mdio_addr, RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(cpu_port), RTL8372N_STOCK_CPU_FORCE);

	rtl8372n_port_isolation_setup(mdio_addr, cpu_port, port_mask);

	rtl8372n_reg_write(mdio_addr, RTL8372N_CPU_TAG_CTRL_ADDR, RTL8372N_STOCK_CPU_TAG);
	rtl8372n_reg_write(mdio_addr, RTL8372N_EXT_CPU_CTRL_ADDR, RTL8372N_STOCK_EXT_CPU);
	rtl8372n_reg_write(mdio_addr, RTL8372N_CPU_TAG_AWARE_ADDR, 0);

	rtl8372n_reg_write(mdio_addr, RTL8372N_VLAN_CTRL_ADDR, 0);
	rtl8372n_reg_write(mdio_addr, RTL8372N_VLAN_INGRESS_ADDR, 0);
	rtl8372n_reg_write(mdio_addr, RTL8372N_VLAN_EGRESS_ADDR, 0x000fffff);

	rtl8372n_reg_write(mdio_addr, RTL8372N_MSTP_STATE0_ADDR, 0x000fffff);

	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_MAC_L2_GLOBAL_CTRL0_ADDR, RTL8372N_MAC_L2_GLOBAL_CTRL0_FWD_INVLD_MAC_CTRL_MASK, 0);
	rtl8372n_reg_set_bits(mdio_addr, RTL8372N_MAC_L2_GLOBAL_CTRL0_ADDR, RTL8372N_MAC_L2_GLOBAL_CTRL0_FWD_UNKN_OPCODE_MASK, 0);

#ifdef CONFIG_RTL8372N_DEBUG
	{
		u32 chip, mode, force, tag;
		rtl8372n_reg_read(mdio_addr, 0x4, &chip);
		rtl8372n_reg_read(mdio_addr, RTL8372N_SDS_MODE_SEL_ADDR, &mode);
		rtl8372n_reg_read(mdio_addr, RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(cpu_port), &force);
		rtl8372n_reg_read(mdio_addr, RTL8372N_CPU_TAG_CTRL_ADDR, &tag);
		printf("RTL8372N: handoff chip=0x%08x sds=0x%08x force=0x%08x tag=0x%08x\n", chip, mode, force, tag);
	}

	printf("RTL8372N: switch init done (stock handoff)\n");
#endif
	return 0;
}

void ipq_rtl8372n_switch_reset(int gpio) {
	printf("RTL8372N: HW reset via GPIO %d\n", gpio);
	gpio_direction_output(gpio, 0);
	mdelay(50);
	gpio_direction_output(gpio, 1);
	mdelay(50);
}

static void ipq_rtl8372n_link_poll_register(ipq_rtl8372n_swt_cfg_t *swt_cfg);

int ipq_rtl8372n_switch_init(ipq_rtl8372n_swt_cfg_t *swt_cfg) {
	int retry;

	if (!swt_cfg)
		return -1;

	for (retry = 0; retry < RTL8372N_CHIP_PROBE_RETRY; retry++) {
		if (rtl8372n_chip_probe(swt_cfg->mdio_addr) == 0)
			break;
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: probe retry %d/%d\n", retry + 1, RTL8372N_CHIP_PROBE_RETRY);
#endif
		mdelay(100);
	}

	if (retry == RTL8372N_CHIP_PROBE_RETRY) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: chip probe failed after %d retries\n", RTL8372N_CHIP_PROBE_RETRY);
#endif
		swt_cfg->chip_detect = 0;
		return -1;
	}

	swt_cfg->chip_detect = 1;

#ifdef CONFIG_RTL8372N_DEBUG
	printf("RTL8372N: chip detected\n");
#endif

	if (_rtl8372n_switch_init(swt_cfg->mdio_addr, swt_cfg->sds0_mode, swt_cfg->sds1_mode, swt_cfg->cpu_port, swt_cfg->port_mask) < 0) {
#ifdef CONFIG_RTL8372N_DEBUG
		printf("RTL8372N: switch init failed\n");
#endif
		return -1;
	}

	swt_cfg->last_link = 0;
	ipq_rtl8372n_link_poll_register(swt_cfg);
	return 0;
}

static u32 rtl8372n_speed_mbps(u32 speed) {
	switch (speed) {
	case RTL8372N_PORT_SPEED_10M:	return 10;
	case RTL8372N_PORT_SPEED_100M:	return 100;
	case RTL8372N_PORT_SPEED_1000M:	return 1000;
	case RTL8372N_PORT_SPEED_2500M:	return 2500;
	case RTL8372N_PORT_SPEED_5G:	return 5000;
	case RTL8372N_PORT_SPEED_10G:	return 10000;
	default:						return 0;
	}
}

int ipq_rtl8372n_link_update(ipq_rtl8372n_swt_cfg_t *swt_cfg) {
	u32 mac_link_sts, spd_sts, spd_sts_hi, spd, dup_sts, port, speed_val, changed, phy_link;
	int status = 0;

	if (!swt_cfg || !swt_cfg->chip_detect ||
	    rtl8372n_reg_read(swt_cfg->mdio_addr, RTL8372N_MAC_LINK_STS_ADDR, &mac_link_sts) < 0)
		return 1;

	phy_link = (mac_link_sts & RTL8372N_MAC_LINK_STS_MAC_LINK_MASK) >> RTL8372N_MAC_LINK_STS_MAC_LINK_OFFSET;

	changed = phy_link ^ swt_cfg->last_link;
	if (!changed)
		return 0;

	if (rtl8372n_reg_read(swt_cfg->mdio_addr, RTL8372N_MAC_LINK_SPD_STS_ADDR(0), &spd_sts) < 0 ||
	    rtl8372n_reg_read(swt_cfg->mdio_addr, RTL8372N_MAC_LINK_DUP_STS_ADDR, &dup_sts) < 0)
		return 1;

#ifdef CONFIG_RTL8372N_DEBUG
	printf("RTL8372N: mac_sts=0x%x phy_link=0x%x last=0x%x changed=0x%x\n", mac_link_sts, phy_link, swt_cfg->last_link, changed);
#endif

	for (port = 0; port <= 9; port++) {
		if (!(changed & BIT(port)) || !(swt_cfg->port_mask & BIT(port)))
			continue;

		if (phy_link & BIT(port)) {
			spd = spd_sts;
			if (port >= 8) {
				if (rtl8372n_reg_read(swt_cfg->mdio_addr, RTL8372N_MAC_LINK_SPD_STS_ADDR(8), &spd_sts_hi) < 0)
					return 1;
				spd = spd_sts_hi;
			}
			speed_val = (spd >> RTL8372N_MAC_LINK_SPD_STS_OFFSET(port)) & (RTL8372N_MAC_LINK_SPD_STS_MASK(port) >>
				     RTL8372N_MAC_LINK_SPD_STS_OFFSET(port));
		}
		printf("RTL8372N: Port%d %s Speed :%d %s duplex\n", port,
			(phy_link & BIT(port)) ? "up" : "down",
			(phy_link & BIT(port)) ? rtl8372n_speed_mbps(speed_val) : 0,
			(phy_link & BIT(port)) ? ((dup_sts & BIT(port)) ? "Full" : "Half") : "Half");
	}

	swt_cfg->last_link = phy_link;
	return status;
}

#define RTL8372N_POLL_MAX_DEVS	2

static ipq_rtl8372n_swt_cfg_t *rtl8372n_poll_cfgs[RTL8372N_POLL_MAX_DEVS];
static int rtl8372n_poll_count;

static void ipq_rtl8372n_link_poll_register(ipq_rtl8372n_swt_cfg_t *swt_cfg) {
	if (rtl8372n_poll_count >= RTL8372N_POLL_MAX_DEVS)
		return;
	rtl8372n_poll_cfgs[rtl8372n_poll_count++] = swt_cfg;
}

void ipq_rtl8372n_link_poll(void) {
	int i;

	for (i = 0; i < rtl8372n_poll_count; i++) {
		if (rtl8372n_poll_cfgs[i] && rtl8372n_poll_cfgs[i]->chip_detect)
			ipq_rtl8372n_link_update(rtl8372n_poll_cfgs[i]);
	}
}