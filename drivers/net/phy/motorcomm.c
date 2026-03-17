// SPDX-License-Identifier: GPL-2.0+
/*
 * Motorcomm PHY driver for u-boot
 *
 * Based on Linux kernel driver from:
 * https://github.com/AsahiLinux/linux/blob/asahi/drivers/net/phy/motorcomm.c
 *
 * Original Authors:
 * Copyright (c) 2021 Peter Geis <pgwipeout@gmail.com>
 * Copyright (c) 2021 Frank <Frank.Sae@motor-comm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Adapted for u-boot by: Assistant
 * Modifications for integration into Qualcomm's custom u-boot-2016 by: Willem Lee <1980490718@qq.com>
 * Copyright (c) 2026 Willem Lee <1980490718@qq.com>. All rights reserved.
 * NOTE: This driver has been adapted from the Linux kernel version but has NOT BEEN TESTED!
 */

#include <config.h>
#include <common.h>
#include <phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/types.h>
#include <asm/types.h>

#ifndef u16
#define u16 unsigned short
#endif

#ifndef MDIO_DEVAD_NONE
#define MDIO_DEVAD_NONE (-1)
#endif

#ifndef PHY_INTERFACE_MODE_2500BASEX
#define PHY_INTERFACE_MODE_2500BASEX 13 /* 2500base-X */
#endif

#define PHY_ID_YT8511		0x0000010a
#define PHY_ID_YT8521		0x0000011a
#define PHY_ID_YT8531		0x4f51e91b
#define PHY_ID_YT8531S		0x4f51e91a
#define PHY_ID_YT8821		0x4f51ea19

/* Specific Function Control Register */
#define YTPHY_SPECIFIC_FUNCTION_CONTROL_REG	0x10

/* Specific Status Register */
#define YTPHY_SPECIFIC_STATUS_REG			0x11
#define YTPHY_SSR_SPEED_MASK			((0x3 << 14) | BIT(9))
#define YTPHY_SSR_SPEED_10M			((0x0 << 14))
#define YTPHY_SSR_SPEED_100M			((0x1 << 14))
#define YTPHY_SSR_SPEED_1000M			((0x2 << 14))
#define YTPHY_SSR_SPEED_2500M			((0x0 << 14) | BIT(9))
#define YTPHY_SSR_DUPLEX_OFFSET			13
#define YTPHY_SSR_DUPLEX			BIT(13)
#define YTPHY_SSR_SPEED_DUPLEX_RESOLVED		BIT(11)
#define YTPHY_SSR_LINK				BIT(10)

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT				0x1E
/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA				0x1F

#define YT8511_PAGE_SELECT	0x1e
#define YT8511_PAGE		0x1f
#define YT8511_EXT_CLK_GATE	0x0c
#define YT8511_EXT_DELAY_DRIVE	0x0d
#define YT8511_EXT_SLEEP_CTRL	0x27

/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */
#define YT8511_CLK_125M		(BIT(2) | BIT(1))
#define YT8511_PLLON_SLP	BIT(14)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX		BIT(0)

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN	(0xf << 4)
#define YT8511_DELAY_GE_TX_DIS	(0x2 << 4)
#define YT8511_DELAY_FE_TX_EN	(0xf << 12)
#define YT8511_DELAY_FE_TX_DIS	(0x2 << 12)

/* Phy gmii clock gating Register */
#define YT8521_CLOCK_GATING_REG			0xC
#define YT8521_CGR_RX_CLK_EN			BIT(12)

#define YT8521_EXTREG_SLEEP_CONTROL1_REG	0x27
#define YT8521_ESC1R_SLEEP_SW			BIT(15)
#define YT8521_ESC1R_PLLON_SLP			BIT(14)

#define YT8521_REG_SPACE_SELECT_REG		0xA000
#define YT8521_RSSR_SPACE_MASK			BIT(1)
#define YT8521_RSSR_FIBER_SPACE			(0x1 << 1)
#define YT8521_RSSR_UTP_SPACE			(0x0 << 1)

#define YT8521_CHIP_CONFIG_REG			0xA001
#define YT8521_CCR_SW_RST			BIT(15)

/* 1b0 disable 1.9ns rxc clock delay  *default*
 * 1b1 enable 1.9ns rxc clock delay
 */
#define YT8521_CCR_RXC_DLY_EN			BIT(8)
#define YT8521_CCR_RXC_DLY_1_900_NS		1900

#define YT8521_CCR_MODE_SEL_MASK		(BIT(2) | BIT(1) | BIT(0))
#define YT8521_CCR_MODE_UTP_TO_RGMII		0
#define YT8521_CCR_MODE_FIBER_TO_RGMII		1
#define YT8521_CCR_MODE_UTP_FIBER_TO_RGMII	2
#define YT8521_CCR_MODE_UTP_TO_SGMII		3
#define YT8521_CCR_MODE_SGPHY_TO_RGMAC		4
#define YT8521_CCR_MODE_SGMAC_TO_RGPHY		5
#define YT8521_CCR_MODE_UTP_TO_FIBER_AUTO	6
#define YT8521_CCR_MODE_UTP_TO_FIBER_FORCE	7

/* 3 phy polling modes,poll mode combines utp and fiber mode*/
#define YT8521_MODE_FIBER			0x1
#define YT8521_MODE_UTP				0x2
#define YT8521_MODE_POLL			0x3

#define YT8521_RGMII_CONFIG1_REG		0xA003
/* 1b0 use original tx_clk_rgmii  *default*
 * 1b1 use inverted tx_clk_rgmii.
 */
#define YT8521_RC1R_TX_CLK_SEL_INVERTED		BIT(14)
#define YT8521_RC1R_RX_DELAY_MASK		GENMASK(13, 10)
#define YT8521_RC1R_FE_TX_DELAY_MASK		GENMASK(7, 4)
#define YT8521_RC1R_GE_TX_DELAY_MASK		GENMASK(3, 0)
#define YT8521_RC1R_RGMII_0_000_NS		0
#define YT8521_RC1R_RGMII_0_150_NS		1
#define YT8521_RC1R_RGMII_0_300_NS		2
#define YT8521_RC1R_RGMII_0_450_NS		3
#define YT8521_RC1R_RGMII_0_600_NS		4
#define YT8521_RC1R_RGMII_0_750_NS		5
#define YT8521_RC1R_RGMII_0_900_NS		6
#define YT8521_RC1R_RGMII_1_050_NS		7
#define YT8521_RC1R_RGMII_1_200_NS		8
#define YT8521_RC1R_RGMII_1_350_NS		9
#define YT8521_RC1R_RGMII_1_500_NS		10
#define YT8521_RC1R_RGMII_1_650_NS		11
#define YT8521_RC1R_RGMII_1_800_NS		12
#define YT8521_RC1R_RGMII_1_950_NS		13
#define YT8521_RC1R_RGMII_2_100_NS		14
#define YT8521_RC1R_RGMII_2_250_NS		15

#define YTPHY_SYNCE_CFG_REG			0xA012
#define YT8521_SCR_SYNCE_ENABLE			BIT(5)
/* 1b0 output 25m clock
 * 1b1 output 125m clock  *default*
 */
#define YT8521_SCR_CLK_FRE_SEL_125M		BIT(3)
#define YT8521_SCR_CLK_SRC_MASK			GENMASK(2, 1)
#define YT8521_SCR_CLK_SRC_PLL_125M		0
#define YT8521_SCR_CLK_SRC_UTP_RX		1
#define YT8521_SCR_CLK_SRC_SDS_RX		2
#define YT8521_SCR_CLK_SRC_REF_25M		3
#define YT8531_SCR_SYNCE_ENABLE			BIT(6)
/* 1b0 output 25m clock   *default*
 * 1b1 output 125m clock
 */
#define YT8531_SCR_CLK_FRE_SEL_125M		BIT(4)
#define YT8531_SCR_CLK_SRC_MASK			GENMASK(3, 1)
#define YT8531_SCR_CLK_SRC_PLL_125M		0
#define YT8531_SCR_CLK_SRC_UTP_RX		1
#define YT8531_SCR_CLK_SRC_SDS_RX		2
#define YT8531_SCR_CLK_SRC_CLOCK_FROM_DIGITAL	3
#define YT8531_SCR_CLK_SRC_REF_25M		4
#define YT8531_SCR_CLK_SRC_SSC_25M		5

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#ifndef GENMASK
#define GENMASK(h, l) (((1U << ((h) - (l) + 1)) - 1) << (l))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

    /**
     * ytphy_read_ext() - read a PHY's extended register
     * @phydev: a pointer to a &struct phy_device
     * @regnum: register number to read
     *
     * returns the value of regnum reg or negative error code
     */
    static int ytphy_read_ext(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA);
}

/**
 * ytphy_write_ext() - write a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * returns 0 or negative error code
 */
static int ytphy_write_ext(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA, val);
}

/**
 * ytphy_modify_ext() - bits modify a PHY's extended register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * NOTE: Convenience function which allows a PHY's extended register to be
 * modified as new register value = (old register value & ~mask) | set.
 *
 * returns 0 or negative error code
 */
static int ytphy_modify_ext(struct phy_device *phydev, u16 regnum, u16 mask,
			    u16 set)
{
	int ret;
	u16 val;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	val = phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA);
	if (val < 0)
		return val;

	val = (val & ~mask) | set;

	return phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA, val);
}

static int yt8511_config_init(struct phy_device *phydev)
{
	int oldpage, ret = 0;
	unsigned int ge, fe;

	/* Save current page */
	oldpage = phy_read(phydev, MDIO_DEVAD_NONE, YT8511_PAGE_SELECT);
	if (oldpage < 0)
		return oldpage;

	/* set rgmii delay mode */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		ge = YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	default: /* do not support other modes */
		/* For unsupported modes, use default */
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	}

	ret = ytphy_modify_ext(phydev, YT8511_PAGE, (YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN), ge);
	if (ret < 0)
		goto restore_page;

	/* set clock mode to 125mhz */
	ret = ytphy_modify_ext(phydev, YT8511_PAGE, 0, YT8511_CLK_125M);
	if (ret < 0)
		goto restore_page;

	/* fast ethernet delay is in a separate page */
	ret = phy_write(phydev, MDIO_DEVAD_NONE, YT8511_PAGE_SELECT, YT8511_EXT_DELAY_DRIVE);
	if (ret < 0)
		goto restore_page;

	ret = ytphy_modify_ext(phydev, YT8511_PAGE, YT8511_DELAY_FE_TX_EN, fe);
	if (ret < 0)
		goto restore_page;

	/* leave pll enabled in sleep */
	ret = phy_write(phydev, MDIO_DEVAD_NONE, YT8511_PAGE_SELECT, YT8511_EXT_SLEEP_CTRL);
	if (ret < 0)
		goto restore_page;

	ret = ytphy_modify_ext(phydev, YT8511_PAGE, 0, YT8511_PLLON_SLP);
	if (ret < 0)
		goto restore_page;

restore_page:
	/* Restore original page */
	phy_write(phydev, MDIO_DEVAD_NONE, YT8511_PAGE_SELECT, oldpage);
	return ret;
}

static int yt8531_config_init(struct phy_device *phydev)
{
	/* Simple initialization for YT8531 */
	/* For u-boot, we focus on basic functionality */

	/* Configure RGMII delays */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* Configure basic RGMII delays */
		break;
	default:
		break;
	}

	return 0;
}

static int yt8521_config_init(struct phy_device *phydev)
{
	int ret = 0;
	int oldpage;

	/* Save current page */
	oldpage = ytphy_read_ext(phydev, YT8521_REG_SPACE_SELECT_REG);
	if (oldpage < 0)
		return oldpage;

	/* Select UTP space */
	ret = ytphy_write_ext(phydev, YT8521_REG_SPACE_SELECT_REG, YT8521_RSSR_UTP_SPACE);
	if (ret < 0)
		return ret;

	/* Configure RGMII delays based on interface */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* Configure RGMII timing */
		break;
	default:
		break;
	}

	/* Disable auto sleep */
	ret = ytphy_modify_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG,
			 YT8521_ESC1R_SLEEP_SW, 0);

	/* Restore original page */
	ytphy_write_ext(phydev, YT8521_REG_SPACE_SELECT_REG, oldpage);

	return ret;
}

static int yt8821_config_init(struct phy_device *phydev)
{
	int ret = 0;
	u16 set;

	/* Configure YT8821 for SGMII or 2500BASEX modes */
	if (phydev->interface == PHY_INTERFACE_MODE_2500BASEX) {
		set = 1; /* Force 2500Base-X mode */
	} else {
		set = 0; /* Auto mode */
	}

	/* Modify chip config register */
	ret = ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
		YT8521_CCR_MODE_SEL_MASK, set);

	/* Disable auto sleep */
	ytphy_modify_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1_REG,
			 YT8521_ESC1R_SLEEP_SW, 0);

	/* Soft reset */
	ytphy_modify_ext(phydev, YT8521_CHIP_CONFIG_REG,
			 YT8521_CCR_SW_RST, 0);

	return ret;
}

static int ytphy_config(struct phy_device *phydev)
{
	int ret;

	/* Initialize the PHY based on its ID */
	switch (phydev->phy_id & 0xffffffff) {
	case PHY_ID_YT8511:
		ret = yt8511_config_init(phydev);
		break;
	case PHY_ID_YT8521:
		ret = yt8521_config_init(phydev);
		break;
	case PHY_ID_YT8531:
	case PHY_ID_YT8531S:
		ret = yt8531_config_init(phydev);
		break;
	case PHY_ID_YT8821:
		ret = yt8821_config_init(phydev);
		break;
	default:
		ret = 0;
		break;
	}

	if (ret < 0)
		return ret;

	return genphy_config_aneg(phydev);
}

static int ytphy_startup(struct phy_device *phydev)
{
	int ret;
	int status;

	/* Update link */
	ret = genphy_update_link(phydev);
	if (ret)
		return ret;

	/* Get the current status */
	status = phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_SPECIFIC_STATUS_REG);
	if (status < 0)
		return status;

	/* Parse link settings */
	if (status & YTPHY_SSR_LINK) {
		int speed_mode = status & YTPHY_SSR_SPEED_MASK;

		switch (speed_mode) {
		case YTPHY_SSR_SPEED_10M:
			phydev->speed = SPEED_10;
			break;
		case YTPHY_SSR_SPEED_100M:
			phydev->speed = SPEED_100;
			break;
		case YTPHY_SSR_SPEED_1000M:
			phydev->speed = SPEED_1000;
			break;
		case YTPHY_SSR_SPEED_2500M:
			phydev->speed = SPEED_2500;
			break;
		default:
			phydev->speed = SPEED_1000; /* Default to 1000 */
			break;
		}

		phydev->duplex = (status & YTPHY_SSR_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
	} else {
		phydev->link = 0;
	}

	return 0;
}

static struct phy_driver motorcomm_phy_drvs[] = {
	{
		.name = "YT8511 Gigabit Ethernet",
		.uid = PHY_ID_YT8511,
		.mask = 0xffffffff,
		.features = PHY_GBIT_FEATURES,
		.config = ytphy_config,
		.startup = ytphy_startup,
		.shutdown = genphy_shutdown,
	},
	{
		.name = "YT8521 Gigabit Ethernet",
		.uid = PHY_ID_YT8521,
		.mask = 0xffffffff,
		.features = PHY_GBIT_FEATURES,
		.config = ytphy_config,
		.startup = ytphy_startup,
		.shutdown = genphy_shutdown,
	},
	{
		.name = "YT8531 Gigabit Ethernet",
		.uid = PHY_ID_YT8531,
		.mask = 0xffffffff,
		.features = PHY_GBIT_FEATURES,
		.config = ytphy_config,
		.startup = ytphy_startup,
		.shutdown = genphy_shutdown,
	},
	{
		.name = "YT8531S Gigabit Ethernet",
		.uid = PHY_ID_YT8531S,
		.mask = 0xffffffff,
		.features = PHY_GBIT_FEATURES,
		.config = ytphy_config,
		.startup = ytphy_startup,
		.shutdown = genphy_shutdown,
	},
	{
		.name = "YT8821 2.5Gbps PHY",
		.uid = PHY_ID_YT8821,
		.mask = 0xffffffff,
		.features = PHY_GBIT_FEATURES | SUPPORTED_10000baseT_Full,
		.config = ytphy_config,
		.startup = ytphy_startup,
		.shutdown = genphy_shutdown,
	},
};

int phy_motorcomm_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(motorcomm_phy_drvs); i++)
		phy_register(&motorcomm_phy_drvs[i]);

	return 0;
}