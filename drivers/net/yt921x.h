/* drivers/net/yt921x.h */
/*
 * Header for Motorcomm YT921X switch driver for U-Boot
 * Copyright (c) 2025 David Yang
 * Adapted for U-Boot-2016 by Willem Lee <1980490718@qq.com>
 *
 * Based on the TI Linux Kernel implementation:
 * https://github.com/RobertCNelson/ti-linux-kernel/commit/186623f4aa724c46cbb4dbd5235cf6942215f5b5
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef _YT921X_H
#define _YT921X_H

#include <linux/types.h>
#include <asm/types.h>

/* Register definitions - simplified from original */
#define YT921X_SMI_SWITCHID_M 0x0000000C /* GENMASK(3, 2) */
#define YT921X_SMI_AD 0x00000002		 /* BIT(1) */
#define YT921X_SMI_ADDR 0x00000000		 /* 0 */
#define YT921X_SMI_DATA 0x00000002		 /* YT921X_SMI_AD */
#define YT921X_SMI_RW 0x00000001		 /* BIT(0) */
#define YT921X_SMI_WRITE 0x00000000		 /* 0 */
#define YT921X_SMI_READ 0x00000001		 /* YT921X_SMI_RW */

#define YT921X_RST 0x80000
#define YT921X_RST_HW 0x80000000 /* BIT(31) */
#define YT921X_RST_SW 0x00000002 /* BIT(1) */
#define YT921X_FUNC 0x80004
#define YT921X_FUNC_MIB 0x00000002 /* BIT(1) */
#define YT921X_CHIP_ID 0x80008
#define YT921X_CHIP_ID_MAJOR 0xFFFF0000 /* GENMASK(31, 16) */
#define YT921X_EXT_CPU_PORT 0x8000c
#define YT921X_CPU_TAG_TPID 0x80010
#define YT921X_PVID_SEL 0x80014
#define YT921X_SERDES_CTRL 0x80028
#define YT921X_IO_LEVEL 0x80030

#define YT921X_MACn_FRAME(port) (0x81008 + 0x1000 * (port))
#define YT921X_MAC_FRAME_SIZE_M 0x003FFF00 /* GENMASK(21, 8) */
#define YT921X_MAC_FRAME_SIZE(x) (((x) & 0x3FFF) << 8)

#define YT921X_MIB_CTRL 0xc0004
#define YT921X_MIB_CTRL_CLEAN 0x40000000	/* BIT(30) */
#define YT921X_MIB_CTRL_PORT_M 0x00000078	/* GENMASK(6, 3) */
#define YT921X_MIB_CTRL_ONE_PORT 0x00000002 /* BIT(1) */
#define YT921X_MIB_CTRL_ALL_PORT 0x00000001 /* BIT(0) */
#define YT921X_MIBn_DATA0(port) (0xc0100 + 0x100 * (port))
#define YT921X_MIBn_DATAm(port, x) (YT921X_MIBn_DATA0(port) + 4 * (x))

#define YT921X_EDATA_CTRL 0xe0000
#define YT921X_EDATA_DATA 0xe0004

#define YT921X_EXT_MBUS_OP 0x6a000
#define YT921X_INT_MBUS_OP 0xf0000
#define YT921X_MBUS_OP_START 0x00000001 /* BIT(0) */
#define YT921X_EXT_MBUS_CTRL 0x6a004
#define YT921X_INT_MBUS_CTRL 0xf0004
#define YT921X_MBUS_CTRL_PORT_M 0x03E00000 /* GENMASK(25, 21) */
#define YT921X_MBUS_CTRL_REG_M 0x001F0000  /* GENMASK(20, 16) */
#define YT921X_MBUS_CTRL_TYPE_M 0x00000F00 /* GENMASK(11, 8) */
#define YT921X_MBUS_CTRL_OP_M 0x0000000C   /* GENMASK(3, 2) */
#define YT921X_MBUS_CTRL_WRITE 0x00000004  /* YT921X_MBUS_CTRL_OP(1) */
#define YT921X_MBUS_CTRL_READ 0x00000008   /* YT921X_MBUS_CTRL_OP(2) */
#define YT921X_EXT_MBUS_DOUT 0x6a008
#define YT921X_INT_MBUS_DOUT 0xf0008
#define YT921X_EXT_MBUS_DIN 0x6a00c
#define YT921X_INT_MBUS_DIN 0xf000c

/* Port control registers and bit definitions */
#define YT921X_PORTn_CTRL(port) (0x80100 + 4 * (port))
#define YT921X_PORTn_STATUS(port) (0x80200 + 4 * (port))

/* Port control bits */
#define YT921X_PORT_FLOWCONTROL_AN 0x00000400 /* BIT(10) */
#define YT921X_PORT_LINK_AN 0x00000200        /* BIT(9) */
#define YT921X_PORT_DUPLEX_M 0x00000080       /* GENMASK(7, 7) */
#define YT921X_PORT_DUPLEX_HALF 0x00000000    /* 0 */
#define YT921X_PORT_DUPLEX_FULL 0x00000080    /* BIT(7) */
#define YT921X_PORT_RX_FLOWCONTROL 0x00000040 /* BIT(6) */
#define YT921X_PORT_TX_FLOWCONTROL 0x00000020 /* BIT(5) */
#define YT921X_PORT_RX_MAC_EN 0x00000010      /* BIT(4) */
#define YT921X_PORT_TX_MAC_EN 0x00000008      /* BIT(3) */
#define YT921X_PORT_SPEED_M 0x00000007        /* GENMASK(2, 0) */
#define YT921X_PORT_SPEED_10 0x00000000       /* 0 */
#define YT921X_PORT_SPEED_100 0x00000001      /* 1 */
#define YT921X_PORT_SPEED_1000 0x00000002     /* 2 */
#define YT921X_PORT_SPEED_2500 0x00000004     /* 4 */

/* SGMII control registers and bit definitions */
#define YT921X_SGMII_CTRL(port) (0x8008c + 4 * (port))
#define YT921X_SGMII_CTRL_MODE_M 0x00000380   /* GENMASK(9, 7) */
#define YT921X_SGMII_CTRL_MODE_SGMII_MAC 0x00000000    /* 0 */
#define YT921X_SGMII_CTRL_MODE_SGMII_PHY 0x00000080    /* 1<<7 */
#define YT921X_SGMII_CTRL_MODE_1000BASEX 0x00000100    /* 2<<7 */
#define YT921X_SGMII_CTRL_MODE_100BASEX 0x00000180     /* 3<<7 */
#define YT921X_SGMII_CTRL_MODE_2500BASEX 0x00000200    /* 4<<7 */
#define YT921X_SGMII_CTRL_MODE_BASEX 0x00000280        /* 5<<7 */
#define YT921X_SGMII_CTRL_MODE_DISABLE 0x00000300      /* 6<<7 */
#define YT921X_SGMII_CTRL_LINK 0x00000010              /* BIT(4) */
#define YT921X_SGMII_CTRL_DUPLEX_M 0x00000008          /* GENMASK(3, 3) */
#define YT921X_SGMII_CTRL_DUPLEX_HALF 0x00000000       /* 0 */
#define YT921X_SGMII_CTRL_DUPLEX_FULL 0x00000008       /* BIT(3) */
#define YT921X_SGMII_CTRL_SPEED_M 0x00000007           /* GENMASK(2, 0) */
#define YT921X_SGMII_CTRL_SPEED_10 0x00000000          /* 0 */
#define YT921X_SGMII_CTRL_SPEED_100 0x00000001         /* 1 */
#define YT921X_SGMII_CTRL_SPEED_1000 0x00000002        /* 2 */
#define YT921X_SGMII_CTRL_SPEED_2500 0x00000004        /* 4 */

/* Port status bits */
#define YT921X_PORT_LINK 0x00000100		   /* BIT(8) */
#define YT921X_PORT_HALF_PAUSE 0x00000080  /* BIT(7) */
#define YT921X_PORT_DUPLEX_FULL 0x00000080 /* BIT(7) */
#define YT921X_PORT_RX_PAUSE 0x00000040	   /* BIT(6) */
#define YT921X_PORT_TX_PAUSE 0x00000020	   /* BIT(5) */
#define YT921X_PORT_RX_MAC_EN 0x00000010   /* BIT(4) */
#define YT921X_PORT_TX_MAC_EN 0x00000008   /* BIT(3) */
#define YT921X_PORT_SPEED_M 0x00000007	   /* GENMASK(2, 0) */
#define YT921X_PORT_SPEED_10 0x00000000	   /* YT921X_PORT_SPEED(0) */
#define YT921X_PORT_SPEED_100 0x00000001   /* YT921X_PORT_SPEED(1) */
#define YT921X_PORT_SPEED_1000 0x00000002  /* YT921X_PORT_SPEED(2) */
#define YT921X_PORT_SPEED_10000 0x00000003 /* YT921X_PORT_SPEED(3) */
#define YT921X_PORT_SPEED_2500 0x00000004  /* YT921X_PORT_SPEED(4) */

/* Port numbers */
#define YT921X_PORT_NUM 11 /* 0-10 */

/* Major versions */
#define YT9215_MAJOR 0x9002
#define YT9218_MAJOR 0x9001

/* Reset delay */
#define YT921X_RST_DELAY_US 100000  /* 100ms */

/* Frame size */
#define YT921X_FRAME_SIZE_MAX 0x2400 /* 9216 */

/* SGMII Modes */
#define YT921X_SGMII_MODE_MAC 0
#define YT921X_SGMII_MODE_PHY 1
#define YT921X_SGMII_MODE_1000BASEX 2
#define YT921X_SGMII_MODE_100BASEX 3
#define YT921X_SGMII_MODE_2500BASEX 4
#define YT921X_SGMII_MODE_DISABLE 6

/* Structure definition */
struct yt921x_priv {
	char mii_dev[16];
	int phy_addr;
	int initialized;
};

/* Function prototypes */
int yt921x_switch_init(const char *mii_dev_name, int phy_addr);
int yt921x_reg_read(struct yt921x_priv *priv, unsigned int reg, unsigned int *valp);
int yt921x_reg_write(struct yt921x_priv *priv, unsigned int reg, unsigned int val);
int yt921x_reg_update_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask, unsigned int val);
int yt921x_reg_set_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask);
int yt921x_reg_clear_bits(struct yt921x_priv *priv, unsigned int reg, unsigned int mask);

/* Port control functions */
int yt921x_port_set_speed(struct yt921x_priv *priv, int port, int speed);
int yt921x_port_set_duplex(struct yt921x_priv *priv, int port, int duplex);
int yt921x_port_get_status(struct yt921x_priv *priv, int port, unsigned int *status);
int yt921x_port_enable_mac(struct yt921x_priv *priv, int port, int enable);

/* SGMII control functions */
int yt921x_sgmii_set_mode(struct yt921x_priv *priv, int port, int mode);

#endif /* _YT921X_H */