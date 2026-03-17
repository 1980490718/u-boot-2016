/*
 * drivers/net/yt921x.c - U-Boot compatible driver for Motorcomm YT921X switches
 *
 * Copyright (c) 2025 David Yang
 * Adapted for U-Boot-2016 by Willem Lee <1980490718@qq.com>
 *
 * Based on the TI Linux Kernel implementation:
 * https://github.com/RobertCNelson/ti-linux-kernel/commit/186623f4aa724c46cbb4dbd5235cf6942215f5b5
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <common.h>
#include <malloc.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/io.h>
#include "yt921x.h"

/* Define types if not already defined */
#ifndef u16
#define u16 unsigned short
#endif

#ifndef u32
#define u32 unsigned int
#endif

/* Global variable to maintain state during boot */
static struct yt921x_priv *global_priv = NULL;

/* Forward declarations for internal functions */
static int yt921x_reg_set_bits_internal(struct yt921x_priv *priv, unsigned int reg, unsigned int mask);

/* Register access functions using MII */
static int yt921x_reg_read_internal(struct yt921x_priv *priv, unsigned int reg, unsigned int *valp)
{
	unsigned short lo, hi;
	int ret;

	if (!priv || !valp)
		return -1;

	/* Write register address high */
	ret = miiphy_write(priv->mii_dev, priv->phy_addr,
					   YT921X_SMI_ADDR, (unsigned short)(reg >> 16));
	if (ret)
		return ret;

	/* Write register address low */
	ret = miiphy_write(priv->mii_dev, priv->phy_addr,
					   YT921X_SMI_ADDR + 1, (unsigned short)reg);
	if (ret)
		return ret;

	/* Read data high */
	ret = miiphy_read(priv->mii_dev, priv->phy_addr,
					  YT921X_SMI_DATA, &hi);
	if (ret < 0)
		return ret;

	/* Read data low */
	ret = miiphy_read(priv->mii_dev, priv->phy_addr,
					  YT921X_SMI_DATA + 1, &lo);
	if (ret < 0)
		return ret;

	*valp = ((unsigned int)hi << 16) | lo;
	return 0;
}

static int yt921x_reg_write_internal(struct yt921x_priv *priv, unsigned int reg, unsigned int val)
{
	int ret;

	if (!priv)
		return -1;

	/* Write register address high */
	ret = miiphy_write(priv->mii_dev, priv->phy_addr,
					   YT921X_SMI_ADDR, (unsigned short)(reg >> 16));
	if (ret)
		return ret;

	/* Write register address low */
	ret = miiphy_write(priv->mii_dev, priv->phy_addr,
					   YT921X_SMI_ADDR + 1, (unsigned short)reg);
	if (ret)
		return ret;

	/* Write data high */
	ret = miiphy_write(priv->mii_dev, priv->phy_addr,
					   YT921X_SMI_DATA, (unsigned short)(val >> 16));
	if (ret)
		return ret;

	/* Write data low */
	return miiphy_write(priv->mii_dev, priv->phy_addr,
						YT921X_SMI_DATA + 1, (unsigned short)val);
}

static int yt921x_reg_update_bits_internal(struct yt921x_priv *priv, unsigned int reg,
										   unsigned int mask, unsigned int val)
{
	unsigned int v;
	int ret;

	ret = yt921x_reg_read_internal(priv, reg, &v);
	if (ret)
		return ret;

	v &= ~mask;
	v |= val & mask;

	return yt921x_reg_write_internal(priv, reg, v);
}

/* Internal function to set bits */
static int yt921x_reg_set_bits_internal(struct yt921x_priv *priv, unsigned int reg, unsigned int mask)
{
	return yt921x_reg_update_bits_internal(priv, reg, 0, mask);
}

/* Public API functions that match the header file signature */
int yt921x_reg_read(struct yt921x_priv *priv, unsigned int reg, unsigned int *valp)
{
	if (!priv)
		return -1;
	return yt921x_reg_read_internal(priv, reg, valp);
}

int yt921x_reg_write(struct yt921x_priv *priv, unsigned int reg, unsigned int val)
{
	if (!priv)
		return -1;
	return yt921x_reg_write_internal(priv, reg, val);
}

int yt921x_reg_update_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask, unsigned int val)
{
	if (!priv)
		return -1;
	return yt921x_reg_update_bits_internal(priv, reg, mask, val);
}

int yt921x_reg_set_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask)
{
	if (!priv)
		return -1;
	return yt921x_reg_update_bits_internal(priv, reg, 0, mask);
}

int yt921x_reg_clear_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask)
{
	if (!priv)
		return -1;
	return yt921x_reg_update_bits_internal(priv, reg, mask, 0);
}

/* Port control functions */
int yt921x_port_set_speed(struct yt921x_priv *priv, int port, int speed)
{
	unsigned int reg_val;
	int ret;

	if (!priv || port >= YT921X_PORT_NUM)
		return -1;

	/* Read current port control register */
	ret = yt921x_reg_read_internal(priv, YT921X_PORTn_CTRL(port), &reg_val);
	if (ret)
		return ret;

	/* Clear speed bits and set new speed */
	reg_val &= ~YT921X_PORT_SPEED_M;
	switch (speed)
	{
	case 10:
		reg_val |= YT921X_PORT_SPEED_10;
		break;
	case 100:
		reg_val |= YT921X_PORT_SPEED_100;
		break;
	case 1000:
		reg_val |= YT921X_PORT_SPEED_1000;
		break;
	case 2500:
		reg_val |= YT921X_PORT_SPEED_2500;
		break;
	default:
		return -1; // Unsupported speed
	}

	return yt921x_reg_write_internal(priv, YT921X_PORTn_CTRL(port), reg_val);
}

int yt921x_port_set_duplex(struct yt921x_priv *priv, int port, int duplex)
{
	unsigned int reg_val;
	int ret;

	if (!priv || port >= YT921X_PORT_NUM)
		return -1;

	/* Read current port control register */
	ret = yt921x_reg_read_internal(priv, YT921X_PORTn_CTRL(port), &reg_val);
	if (ret)
		return ret;

	/* Update duplex setting */
	if (duplex)
	{
		/* Full duplex */
		reg_val |= YT921X_PORT_DUPLEX_FULL;
	}
	else
	{
		/* Half duplex */
		reg_val &= ~YT921X_PORT_DUPLEX_FULL;
	}

	return yt921x_reg_write_internal(priv, YT921X_PORTn_CTRL(port), reg_val);
}

int yt921x_port_get_status(struct yt921x_priv *priv, int port, unsigned int *status)
{
	if (!priv || !status || port >= YT921X_PORT_NUM)
		return -1;

	return yt921x_reg_read_internal(priv, YT921X_PORTn_STATUS(port), status);
}

int yt921x_port_enable_mac(struct yt921x_priv *priv, int port, int enable)
{
	unsigned int reg_val;
	int ret;

	if (!priv || port >= YT921X_PORT_NUM)
		return -1;

	/* Read current port control register */
	ret = yt921x_reg_read_internal(priv, YT921X_PORTn_CTRL(port), &reg_val);
	if (ret)
		return ret;

	if (enable)
	{
		/* Enable TX/RX MAC */
		reg_val |= (YT921X_PORT_TX_MAC_EN | YT921X_PORT_RX_MAC_EN);
	}
	else
	{
		/* Disable TX/RX MAC */
		reg_val &= ~(YT921X_PORT_TX_MAC_EN | YT921X_PORT_RX_MAC_EN);
	}

	return yt921x_reg_write_internal(priv, YT921X_PORTn_CTRL(port), reg_val);
}

/* SGMII control functions */
int yt921x_sgmii_set_mode(struct yt921x_priv *priv, int port, int mode)
{
	unsigned int reg_val;
	int ret;

	if (!priv || port >= YT921X_PORT_NUM)
		return -1;

	/* Read current SGMII control register */
	ret = yt921x_reg_read_internal(priv, YT921X_SGMII_CTRL(port), &reg_val);
	if (ret)
		return ret;

	/* Clear mode bits and set new mode */
	reg_val &= ~YT921X_SGMII_CTRL_MODE_M;
	switch (mode)
	{
	case YT921X_SGMII_MODE_MAC:
		reg_val |= YT921X_SGMII_CTRL_MODE_SGMII_MAC;
		break;
	case YT921X_SGMII_MODE_PHY:
		reg_val |= YT921X_SGMII_CTRL_MODE_SGMII_PHY;
		break;
	case YT921X_SGMII_MODE_1000BASEX:
		reg_val |= YT921X_SGMII_CTRL_MODE_1000BASEX;
		break;
	case YT921X_SGMII_MODE_100BASEX:
		reg_val |= YT921X_SGMII_CTRL_MODE_100BASEX;
		break;
	case YT921X_SGMII_MODE_2500BASEX:
		reg_val |= YT921X_SGMII_CTRL_MODE_2500BASEX;
		break;
	case YT921X_SGMII_MODE_DISABLE:
		reg_val |= YT921X_SGMII_CTRL_MODE_DISABLE;
		break;
	default:
		return -1; // Unsupported mode
	}

	return yt921x_reg_write_internal(priv, YT921X_SGMII_CTRL(port), reg_val);
}

/* Basic chip detection */
static int yt921x_chip_detect(struct yt921x_priv *priv)
{
	unsigned int chipid;
	int ret;

	if (!priv)
		return -1;

	ret = yt921x_reg_read_internal(priv, YT921X_CHIP_ID, &chipid);
	if (ret)
		return ret;

	if (((chipid >> 16) != YT9215_MAJOR) &&
		((chipid >> 16) != YT9218_MAJOR))
	{
		printf("YT921X: Unexpected chipid 0x%08x\n", chipid);
		return -1;
	}

	return 0;
}

/* Chip reset */
static int yt921x_chip_reset(struct yt921x_priv *priv)
{
	int ret;

	if (!priv)
		return -1;

	/* Perform hardware reset */
	ret = yt921x_reg_write_internal(priv, YT921X_RST, YT921X_RST_HW);
	if (ret)
		return ret;

	/* Wait for reset to complete */
	udelay(YT921X_RST_DELAY_US);

	/* Clear reset */
	return yt921x_reg_write_internal(priv, YT921X_RST, 0);
}

/* Initialize the switch */
int yt921x_switch_init(const char *mii_dev_name, int phy_addr)
{
	int ret;
	unsigned int chipid;

	if (!mii_dev_name)
		return -1;

	/* Free previous instance if exists */
	if (global_priv)
	{
		free(global_priv);
		global_priv = NULL;
	}

	global_priv = malloc(sizeof(struct yt921x_priv));
	if (!global_priv)
		return -1;

	memset(global_priv, 0, sizeof(struct yt921x_priv));

	strncpy(global_priv->mii_dev, mii_dev_name, sizeof(global_priv->mii_dev) - 1);
	global_priv->phy_addr = phy_addr;

	/* Initialize the switch */
	ret = yt921x_chip_reset(global_priv);
	if (ret)
	{
		free(global_priv);
		global_priv = NULL;
		return ret;
	}

	ret = yt921x_chip_detect(global_priv);
	if (ret)
	{
		free(global_priv);
		global_priv = NULL;
		return ret;
	}

	/* Get chip ID for reporting */
	yt921x_reg_read_internal(global_priv, YT921X_CHIP_ID, &chipid);

	/* Basic initialization - enable MIB */
	ret = yt921x_reg_set_bits_internal(global_priv, YT921X_FUNC, YT921X_FUNC_MIB);
	if (ret)
	{
		free(global_priv);
		global_priv = NULL;
		return ret;
	}

	/* Clear MIB counters */
	ret = yt921x_reg_write_internal(global_priv, YT921X_MIB_CTRL,
									YT921X_MIB_CTRL_CLEAN | YT921X_MIB_CTRL_ALL_PORT);
	if (ret)
	{
		free(global_priv);
		global_priv = NULL;
		return ret;
	}

	/* Set frame size to max for all ports */
	int port;
	for (port = 0; port < YT921X_PORT_NUM; port++)
	{
		yt921x_reg_update_bits_internal(global_priv, YT921X_MACn_FRAME(port),
										YT921X_MAC_FRAME_SIZE_M,
										YT921X_MAC_FRAME_SIZE(YT921X_FRAME_SIZE_MAX));
	}

	/* Configure default port settings */
	for (port = 0; port < YT921X_PORT_NUM; port++)
	{
		/* Enable RX/TX MAC for all ports */
		yt921x_port_enable_mac(global_priv, port, 1);

		/* Set default speed to 1000Mbps */
		yt921x_port_set_speed(global_priv, port, 1000);

		/* Set default to full duplex */
		yt921x_port_set_duplex(global_priv, port, 1);
	}

	global_priv->initialized = 1;

	printf("YT921X switch initialized: chipid=0x%08x on %s phy 0x%x\n",
		   chipid, mii_dev_name, phy_addr);

	return 0;
}

#ifdef CONFIG_CMD_YT921X
static int do_yt921x(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	const char *mii_dev_name;
	int phy_addr;
	unsigned int reg, val;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	mii_dev_name = miiphy_get_current_dev();
	if (!mii_dev_name)
	{
		printf("No MII device selected\n");
		return CMD_RET_FAILURE;
	}

	if (strcmp(argv[1], "init") == 0)
	{
		if (argc != 3)
			return CMD_RET_USAGE;

		phy_addr = simple_strtoul(argv[2], NULL, 16);
		ret = yt921x_switch_init(mii_dev_name, phy_addr);
		if (ret)
		{
			printf("YT921X init failed\n");
			return CMD_RET_FAILURE;
		}
		return CMD_RET_SUCCESS;
	}
	else if (strcmp(argv[1], "read") == 0)
	{
		if (argc != 3)
			return CMD_RET_USAGE;

		if (!global_priv)
		{
			printf("YT921X not initialized\n");
			return CMD_RET_FAILURE;
		}

		reg = simple_strtoul(argv[2], NULL, 16);
		ret = yt921x_reg_read(global_priv, reg, &val);
		if (ret)
		{
			printf("YT921X read failed\n");
			return CMD_RET_FAILURE;
		}
		printf("YT921X reg 0x%08x: 0x%08x\n", reg, val);
		return CMD_RET_SUCCESS;
	}
	else if (strcmp(argv[1], "write") == 0)
	{
		if (argc != 4)
			return CMD_RET_USAGE;

		if (!global_priv)
		{
			printf("YT921X not initialized\n");
			return CMD_RET_FAILURE;
		}

		reg = simple_strtoul(argv[2], NULL, 16);
		val = simple_strtoul(argv[3], NULL, 16);
		ret = yt921x_reg_write(global_priv, reg, val);
		if (ret)
		{
			printf("YT921X write failed\n");
			return CMD_RET_FAILURE;
		}
		return CMD_RET_SUCCESS;
	}
	else if (strcmp(argv[1], "detect") == 0)
	{
		// Simple detection based on known addresses
		for (phy_addr = 0; phy_addr < 32; phy_addr++)
		{
			unsigned int chipid;
			struct yt921x_priv temp_priv;
			strncpy(temp_priv.mii_dev, mii_dev_name, sizeof(temp_priv.mii_dev) - 1);
			temp_priv.phy_addr = phy_addr;

			if (yt921x_reg_read_internal(&temp_priv, YT921X_CHIP_ID, &chipid) == 0)
			{
				if (((chipid >> 16) == YT9215_MAJOR) ||
					((chipid >> 16) == YT9218_MAJOR))
				{
					printf("YT921X found at phy 0x%x: chipid=0x%08x\n",
						   phy_addr, chipid);
				}
			}
		}
		return CMD_RET_SUCCESS;
	}
	else if (strcmp(argv[1], "port-speed") == 0)
	{
		if (argc != 4)
			return CMD_RET_USAGE;

		if (!global_priv)
		{
			printf("YT921X not initialized\n");
			return CMD_RET_FAILURE;
		}

		int port = simple_strtoul(argv[2], NULL, 10);
		int speed = simple_strtoul(argv[3], NULL, 10);

		if (port < 0 || port >= YT921X_PORT_NUM)
		{
			printf("Invalid port number\n");
			return CMD_RET_FAILURE;
		}

		ret = yt921x_port_set_speed(global_priv, port, speed);
		if (ret)
		{
			printf("Failed to set port %d speed to %d\n", port, speed);
			return CMD_RET_FAILURE;
		}

		printf("Set port %d speed to %d Mbps\n", port, speed);
		return CMD_RET_SUCCESS;
	}
	else if (strcmp(argv[1], "port-status") == 0)
	{
		if (argc != 3)
			return CMD_RET_USAGE;

		if (!global_priv)
		{
			printf("YT921X not initialized\n");
			return CMD_RET_FAILURE;
		}

		int port = simple_strtoul(argv[2], NULL, 10);
		unsigned int status;

		if (port < 0 || port >= YT921X_PORT_NUM)
		{
			printf("Invalid port number\n");
			return CMD_RET_FAILURE;
		}

		ret = yt921x_port_get_status(global_priv, port, &status);
		if (ret)
		{
			printf("Failed to get port %d status\n", port);
			return CMD_RET_FAILURE;
		}

		printf("Port %d status: 0x%08x\n", port, status);
		return CMD_RET_SUCCESS;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	yt921x, 5, 1, do_yt921x,
	"YT921X switch control commands",
	"init <phy_addr> - initialize switch at given phy address\n"
	"yt921x detect - scan for YT921X switches\n"
	"yt921x read <reg> - read register\n"
	"yt921x write <reg> <val> - write register\n"
	"yt921x port-speed <port> <speed> - set port speed (10/100/1000/2500)\n"
	"yt921x port-status <port> - get port status");
#endif

/* Cleanup function */
void yt921x_cleanup(void)
{
	if (global_priv)
	{
		free(global_priv);
		global_priv = NULL;
	}
}