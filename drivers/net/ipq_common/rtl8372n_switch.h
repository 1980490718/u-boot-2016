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

#ifndef _RTL8372N_SWITCH_H
#define _RTL8372N_SWITCH_H

#include <common.h>
#include <net.h>

/*
 * ==========================================================================
 * RTL8372N DTS Device Tree Binding Documentation
 * ==========================================================================
 *
 * Required parent node:
 *   /ess-switch {
 *       rtl8372n_switch_enable = <1>;        // 1=enable RTL8372N switch
 *       switch_mac_mode0 = <PORT_WRAPPER_USXGMII>;  // SoC UNIPHY0 mode
 *       switch_mac_mode1 = <PORT_WRAPPER_SGMII_PLUS>; // SoC UNIPHY1 mode
 *
 *       rtl8372n_swt_info {
 *           switch@0 {
 *               rtl8372n_rst_gpio = <25>;    // GPIO for hardware reset
 *               mdio_addr      = <29>;        // MDIO bus address of RTL8372N
 *               port_count     = <5>;         // Number of user ports (Port4-7 + CPU)
 *               sds0_mode      = <0x12>;      // SDS0 SerDes mode (HSGMII)
 *               sds1_mode      = <0xD>;       // SDS1 SerDes mode (10GQXG)
 *               cpu_port       = <8>;         // CPU connected port number
 *               port_mask      = <0x1F8>;     // Bitmask of active ports (bit8=CPU, bit7-4=users)
 *           };
 *       };
 *
 *       port_phyinfo {
 *           port@0 {
 *               phy_address = <0>;
 *               phy_type = <RTL8372N_SWITCH_TYPE>;
 *               forced-speed = <10000>;
 *               forced-duplex = <1>;
 *               uniphy_id = <0>;
 *               uniphy_mode = <PORT_WRAPPER_USXGMII>;
 *           };
 *       };
 *   };
 *
 * DTS Property Reference:
 * -------------------------------------------------------------------------
 * Property          | Type  | Required | Default | Description
 * -------------------------------------------------------------------------
 * rtl8372n_rst_gpio | u32   | Yes      | 0       | GPIO pin for HW reset, 0=skip reset
 * mdio_addr         | u32   | Yes      | 0       | RTL8372N MDIO address (typically 29)
 * port_count        | u32   | Yes      | 0       | Total user+CPU ports to configure
 * sds0_mode         | u32   | Yes      | 0       | SDS0 SerDes mode (see SDS_MODE below)
 * sds1_mode         | u32   | Yes      | 0       | SDS1 SerDes mode (see SDS_MODE below)
 * cpu_port          | u32   | Yes      | 8       | CPU connected port index
 * port_mask         | u32   | Yes      | 0       | Active port bitmask
 * -------------------------------------------------------------------------
 *
 * SDS Mode Values (sds0_mode / sds1_mode):
 *   0x02 = SGMII
 *   0x04 = 1000BASEX
 *   0x0D = 10GQXG (10G Quad XAUI)
 *   0x12 = HSGMII (2.5G)
 *   0x16 = 2500BASEX
 *   0x1A = 10GKR
 *   0x1F = OFF (powered down)
 *
 * port_mask Bit Layout:
 *   bit4 = Port4 (user port 0)
 *   bit5 = Port5 (user port 1)
 *   bit6 = Port6 (user port 2)
 *   bit7 = Port7 (user port 3)
 *   bit8 = Port8 (CPU port)
 *   Example: 0x1F8 = Port4+5+6+7+8 (4 user ports + 1 CPU port)
 *
 * PHY Address Mapping (auto-configured by driver):
 *   Port4 -> PHY addr 4 (via SMI_PORT0_5_ADDR_CTRL)
 *   Port5 -> PHY addr 5 (via SMI_PORT0_5_ADDR_CTRL)
 *   Port6 -> PHY addr 6 (via SMI_PORT6_9_ADDR_CTRL)
 *   Port7 -> PHY addr 7 (via SMI_PORT6_9_ADDR_CTRL)
 *
 * Example DTS for Xiaomi BE3600 Pro (5+8 ports):
 *   rtl8372n_swt_info {
 *       switch@0 {
 *           rtl8372n_rst_gpio = <25>;
 *           mdio_addr = <29>;
 *           port_count = <5>;
 *           sds0_mode = <0x12>;      // HSGMII for 2.5G link
 *           sds1_mode = <0xD>;       // 10GQXG for 10G link
 *           cpu_port = <8>;
 *           port_mask = <0x1F8>;     // Port4-8
 *       };
 *   };
 *
 * Driver Architecture:
 *   ipq5332_edma.c  --> SoC side (PPE/UNIPHY/XGMAC config + MDIO access)
 *   rtl8372n_switch.c --> Switch side (register config via MDIO)
 *   rtl8372n_switch.h --> Register definitions + DTS binding + data structures
 *
 * ==========================================================================
 */

#define RTL8372N_CHIP_ID			0x837270

#define RTL8372N_MDC_MDIO_CTRL_REG		21
#define RTL8372N_MDC_MDIO_ADDR_REG		22
#define RTL8372N_MDC_MDIO_DATA_LOW		23
#define RTL8372N_MDC_MDIO_DATA_HIGH		24
#define RTL8372N_MDC_MDIO_READ_CMD		0x1B
#define RTL8372N_MDC_MDIO_WRITE_CMD		0x19
#define RTL8372N_MDC_MDIO_BUSY_BIT		(1 << 2)

#define RTL8372N_SDS_INDACS_CMD_ADDR		0x3F8
#define RTL8372N_SDS_INDACS_RD_ADDR		0x3FC
#define RTL8372N_SDS_INDACS_WD_ADDR		0x400

#define RTL8372N_SDS_INDACS_CMD_SDS_CMD_OFFSET		15
#define RTL8372N_SDS_INDACS_CMD_SDS_RWOP_OFFSET		14
#define RTL8372N_SDS_INDACS_CMD_SDS_REGAD_OFFSET	7
#define RTL8372N_SDS_INDACS_CMD_SDS_REGAD_MASK		(0x1F << 7)
#define RTL8372N_SDS_INDACS_CMD_SDS_PAGE_OFFSET		1
#define RTL8372N_SDS_INDACS_CMD_SDS_PAGE_MASK		(0x3F << 1)
#define RTL8372N_SDS_INDACS_CMD_SDS_INDEX_OFFSET	0

#define RTL8372N_RST_GLB_CTRL_0_ADDR		0x24
#define RTL8372N_DW8051_CFG_ADDR		0x6040
#define RTL8372N_DW8051_CFG_DW8051_READY_MASK	(0x1 << 0)
#define RTL8372N_RS_LAYER_CONFIG_ADDR		0xB7C
#define RTL8372N_RS_LAYER_CONFIG_RS_LINK_FAULT_INDI_OFF_OFFSET	5
#define RTL8372N_RS_LAYER_CONFIG_RS_LINK_FAULT_INDI_OFF_MASK	(0x1 << 5)
#define RTL8372N_SDS_MODE_SEL_ADDR		0x7B20
#define RTL8372N_SDS_MODE_SEL_SDS0_MODE_SEL_MASK	(0x1F << 0)
#define RTL8372N_SDS_MODE_SEL_SDS1_MODE_SEL_MASK	(0x1F << 5)
#define RTL8372N_SDS_MODE_SEL_SDS0_USX_SUB_MODE_MASK	(0x1F << 10)
#define RTL8372N_SDS_MODE_SEL_SDS1_USX_SUB_MODE_MASK	(0x1F << 16)
#define RTL8372N_SMI_GLB_CTRL_ADDR		0x632C
#define RTL8372N_SMI_MAC_TYPE_CTRL_ADDR		0x6330
#define RTL8372N_SMI_MAC_TYPE_CTRL_MAC_PORT3_TYPE_MASK	(0x3 << 6)
#define RTL8372N_SMI_MAC_TYPE_CTRL_MAC_PORT8_TYPE_MASK	(0x3 << 16)
#define RTL8372N_SMI_PORT_POLLING_SEL_ADDR	0x6334
#define RTL8372N_SMI_CTRL_ADDR			0x6454
#define RTL8372N_MAC_L2_GLOBAL_CTRL0_ADDR	0x5FD4
#define RTL8372N_MAC_L2_PORT_CTRL_ADDR(port)	(0x1238 + ((port) << 8))
#define RTL8372N_MAC_FORCE_MODE_CTRL0_ADDR(port) (0x6344 + ((port) << 2))
#define RTL8372N_MAC_FORCE_MODE_CTRL0_MAC_FORCE_EN_OFFSET	0
#define RTL8372N_MAC_FORCE_MODE_CTRL0_FORCE_LINK_EN_OFFSET	1
#define RTL8372N_MAC_FORCE_MODE_CTRL0_DUPLEX_OFFSET		2
#define RTL8372N_MAC_FORCE_MODE_CTRL0_SPEED_OFFSET		3
#define RTL8372N_MAC_FORCE_MODE_CTRL0_SPEED_MASK		(0xF << 3)
#define RTL8372N_MAC_FORCE_MODE_CTRL0_TXPAUSE_OFFSET		7
#define RTL8372N_MAC_FORCE_MODE_CTRL0_RXPAUSE_OFFSET		8
#define RTL8372N_CPU_TAG_AWARE_ADDR		0x603C
#define RTL8372N_CPU_TAG_CTRL_ADDR		0x6720
#define RTL8372N_EXT_CPU_CTRL_ADDR		0x6724
#define RTL8372N_EXT_CPU_CTRL_PORT_MASK		(0xF << 0)

#define RTL8372N_STOCK_CPU_TAG			0x00000500
#define RTL8372N_STOCK_EXT_CPU			0x0000000F
#define RTL8372N_STOCK_USER_FORCE		0x00000194
#define RTL8372N_STOCK_CPU_FORCE		0x000003A7
#define RTL8372N_STOCK_SDS_MODE			0x000009BF
#define RTL8372N_HANDOFF_CPU_PORT		8
#define RTL8372N_HANDOFF_USER_MASK		0x000000F0
#define RTL8372N_HANDOFF_CPU_MASK		0x00000100

#define RTL8372N_VLAN_INGRESS_ADDR		0x4E18
#define RTL8372N_VLAN_EGRESS_ADDR		0x6738
#define RTL8372N_MSTP_STATE0_ADDR		0x5310
#define RTL8372N_PORT_ISO_PORT_PMSK_ADDR(port)	(0x50C0 + ((port) << 2))
#define RTL8372N_MAC_LINK_STS_ADDR		0x63E8
#define RTL8372N_MAC_LINK_STS_MAC_LINK_OFFSET	16
#define RTL8372N_MAC_LINK_STS_MAC_LINK_MASK	(0x3FF << 16)
#define RTL8372N_MAC_LINK_STS_LINK_OFFSET	0
#define RTL8372N_MAC_LINK_STS_LINK_MASK		(0x3FF << 0)
#define RTL8372N_MAC_LINK_SPD_STS_ADDR(port)	(0x63F0 + (((port) >> 3) << 2))
#define RTL8372N_MAC_LINK_SPD_STS_MASK(port)	(0xF << (((port) & 0x7) << 2))
#define RTL8372N_MAC_LINK_SPD_STS_OFFSET(port)	(((port) & 0x7) << 2)
#define RTL8372N_MAC_LINK_DUP_STS_ADDR		0x63F8
#define RTL8372N_FC_PORT_ACT_CTRL_ADDR(port)	(0x7124 + ((port) << 2))

#define RTL8372N_MAC_L2_GLOBAL_CTRL0_FWD_INVLD_MAC_CTRL_MASK	(0x1 << 19)
#define RTL8372N_MAC_L2_GLOBAL_CTRL0_FWD_UNKN_OPCODE_MASK	(0x1 << 20)
#define RTL8372N_MAC_L2_PORT_CTRL_RX_CHK_CRC_EN_MASK		(0x1 << 4)
#define RTL8372N_MAC_L2_PORT_CTRL_CLOCK_SWITCH_MASK		(0x1 << 8)
#define RTL8372N_FC_PORT_ACT_CTRL_DEFAULT_VAL			0x1050

#define RTL8372N_VLAN_CTRL_ADDR				0x4E14

#define RTL8372N_SMI_ACCESS_PHY_CTRL_0_ADDR	0x6438
#define RTL8372N_SMI_ACCESS_PHY_CTRL_0_PHY_MASK_MASK	(0x1FF << 0)
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_ADDR	0x643C
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_CMD_OFFSET	0
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_FAIL_OFFSET	24
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_FAIL_MASK	(0x7 << 24)
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_RWOP_OFFSET	2
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_TYPE_OFFSET	1
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_MMD_DEVAD_OFFSET	19
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_MMD_REG_OFFSET	3
#define RTL8372N_SMI_ACCESS_PHY_CTRL_1_MMD_REG_MASK	(0xFFFF << 3)
#define RTL8372N_SMI_ACCESS_PHY_CTRL_2_ADDR	0x6440
#define RTL8372N_SMI_ACCESS_PHY_CTRL_2_DATA_MASK	(0xFFFF << 0)
#define RTL8372N_SMI_ACCESS_PHY_CTRL_3_ADDR	0x6444
#define RTL8372N_SMI_ACCESS_PHY_CTRL_3_INDATA_MASK	(0xFFFF << 0)
#define RTL8372N_SMI_ACCESS_PHY_CTRL_MAX_POLL	1000

#define RTL8372N_SMI_PORT0_5_ADDR_CTRL_ADDR	0x644C
#define RTL8372N_SMI_PORT0_5_ADDR_CTRL_PORT4_ADDR_MASK	(0x1F << 20)
#define RTL8372N_SMI_PORT0_5_ADDR_CTRL_PORT5_ADDR_MASK	(0x1F << 25)
#define RTL8372N_SMI_PORT6_9_ADDR_CTRL_ADDR	0x6450
#define RTL8372N_SMI_PORT6_9_ADDR_CTRL_PORT6_ADDR_MASK	(0x1F << 0)
#define RTL8372N_SMI_PORT6_9_ADDR_CTRL_PORT7_ADDR_MASK	(0x1F << 5)

#define RTL8372N_PHY_MMD_PMAPMD		1
#define RTL8372N_PHY_MMD_AN			7
#define RTL8372N_PHY_MMD_VEND1			30
#define RTL8372N_PHY_MMD_VEND2			31
#define RTL8372N_PHY_BMSR_REG			1
#define RTL8372N_PHY_BMSR_LINK_STATUS		(1 << 2)
#define RTL8372N_PHY_PMA_CTRL1_REG		0

#define RTL8372N_PHY_PAGE31_REG			31
#define RTL8372N_PHY_PWR_REG			0xa610
#define RTL8372N_PHY_PWR_DOWN_VAL		0x2858
#define RTL8372N_PHY_PWR_UP_VAL		0x2058

#define RTL8372N_SDS_MODE_SGMII		2
#define RTL8372N_SDS_MODE_1000BASEX		4
#define RTL8372N_SDS_MODE_HSGMII		0x12
#define RTL8372N_SDS_MODE_2500BASEX		0x16
#define RTL8372N_SDS_MODE_10GQXG		0xD
#define RTL8372N_SDS_MODE_10GKR		0x1A
#define RTL8372N_SDS_MODE_OFF			0x1F

#define RTL8372N_PORT_SPEED_10M		0
#define RTL8372N_PORT_SPEED_100M	1
#define RTL8372N_PORT_SPEED_1000M	2
#define RTL8372N_PORT_SPEED_2500M	5
#define RTL8372N_PORT_SPEED_5G		6
#define RTL8372N_PORT_SPEED_10G		4

#define RTL8372N_MAX_PORT			6
#define RTL8372N_SDS_BUSY_POLL_CNT		1000
#define RTL8372N_CHIP_PROBE_RETRY		2

struct rtl8372n_sds_patch {
	u8 page;
	u8 reg;
	u16 val;
};

struct patch_entry16_4 {
	u16 reg_addr;
	u16 end_bit;
	u16 start_bit;
	u16 value;
};

typedef struct {
	u32 mdio_addr;
	int chip_detect;
	int chip_ver;
	int port_count;
	u32 sds0_mode;
	u32 sds1_mode;
	u32 cpu_port;
	u32 port_mask;
	u32 last_link;
} ipq_rtl8372n_swt_cfg_t;

extern int ipq_rtl8372n_switch_init(ipq_rtl8372n_swt_cfg_t *swt_cfg);
extern int ipq_rtl8372n_link_update(ipq_rtl8372n_swt_cfg_t *swt_cfg);
extern void ipq_rtl8372n_switch_reset(int gpio);
extern void ipq_rtl8372n_link_poll(void);

#endif