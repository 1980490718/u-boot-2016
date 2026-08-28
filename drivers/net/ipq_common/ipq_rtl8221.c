/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/
#include <common.h>
#include <net.h>
#include <asm-generic/errno.h>
#include <asm/io.h>
#include <malloc.h>
#include <phy.h>
#include "ipq_phy.h"

#define QCA808X_MII_ADDR_C45			(1<<30)
#define QCA808X_REG_C45_ADDRESS(dev_type, reg_num) (QCA808X_MII_ADDR_C45 | ((dev_type & 0x1f) << 16) | (reg_num & 0xffff))

extern int ipq_mdio_read(int mii_id, int regnum, ushort *data);
extern int ipq_mdio_write(int mii_id, int regnum, u16 data);

int rtl8221_phy_reg_read(u32 dev_id, u32 phy_id, u32 reg_id) {
	return ipq_mdio_read(phy_id, reg_id, NULL);
}

int rtl8221_phy_reg_write(u32 dev_id, u32 phy_id, u32 reg_id, u16 value) {
	return ipq_mdio_write(phy_id, reg_id, value);
}

u8 rtl8221_phy_get_link_status(u32 dev_id, u32 phy_id) {
	u16 phy_data;
	phy_data = rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(31, 0xA434));
	if (phy_data & BIT(2))
		return 0;

	return 1;
}

u32 rtl8221_phy_get_duplex(u32 dev_id, u32 phy_id, fal_port_duplex_t *duplex) {
	u16 phy_data;

	phy_data = rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(31, 0xA434));

	if (phy_data & BIT(3))
		*duplex = FAL_FULL_DUPLEX;
	else
		*duplex = FAL_HALF_DUPLEX;

	return 0;
}

u32 rtl8221_phy_get_speed(u32 dev_id, u32 phy_id, fal_port_speed_t *speed) {
	u16 phy_data;
	uint32_t spd_grp, spd;

	phy_data = rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(31, 0xA434));

	spd_grp = (phy_data & (0x0600)) >> 9;
	spd = (phy_data & (0x0030)) >> 4;

	if (spd_grp) {
		if (spd == 1) {
			*speed = FAL_SPEED_2500;
		} else if (spd == 3) {
			*speed = FAL_SPEED_1000;
		} else {
			return -EINVAL;
		}
	}
	else {
		if (spd == 0) {
			*speed = FAL_SPEED_10;
		} else if (spd == 1) {
			*speed = FAL_SPEED_100;
		} else if (spd == 2) {
			*speed = FAL_SPEED_1000;
		}
	}

	return 0;
}

static int rtl8221_led_init(u32 phy_id) {
	static const struct {u16 reg; u16 mask; u16 val;} steps[] = {
		{ 0xd032, 0x0000, 0x0025 },
		{ 0xd034, 0x0000, 0x0024 },
		{ 0xd036, 0x0000, 0x0023 },
		{ 0xd040, 0x0007, 0x0007 },
		{ 0xd044, 0x0007, 0x0000 },
	};
	u16 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(steps); i++) {
		val = steps[i].mask ?
			(rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(31, steps[i].reg))
				& ~steps[i].mask) | steps[i].val : steps[i].val;
		if (rtl8221_phy_reg_write(0x0, phy_id, QCA808X_REG_C45_ADDRESS(31, steps[i].reg), val) < 0)
			return -1;
	}
	return 0;
}

int ipq_rtl8221_phy_init(struct phy_ops **ops, u32 phy_id) {
	u16 phy_data;
	struct phy_ops *rtl8221_ops;

	rtl8221_ops = (struct phy_ops *)malloc(sizeof(struct phy_ops));
	if (!rtl8221_ops)
		return -ENOMEM;
	rtl8221_ops->phy_get_link_status = rtl8221_phy_get_link_status;
	rtl8221_ops->phy_get_speed = rtl8221_phy_get_speed;
	rtl8221_ops->phy_get_duplex = rtl8221_phy_get_duplex;
	*ops = rtl8221_ops;

	phy_data = rtl8221_phy_reg_read(0x0, phy_id, 2);
	printf("PHY ID1: 0x%x\n", phy_data);
	phy_data = rtl8221_phy_reg_read(0x0, phy_id, 3);
	printf("PHY ID2: 0x%x\n", phy_data);

	phy_data = rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(30, 0x75F3));
	phy_data &= 0xfffe;
	phy_data = rtl8221_phy_reg_write(0x0, phy_id, QCA808X_REG_C45_ADDRESS(30, 0x75F3), phy_data);

	phy_data = rtl8221_phy_reg_read(0x0, phy_id, QCA808X_REG_C45_ADDRESS(30, 0x697A));
	phy_data = (phy_data & 0xffc0) | 0;
	phy_data = rtl8221_phy_reg_write(0x0, phy_id, QCA808X_REG_C45_ADDRESS(30, 0x697A), phy_data);

	rtl8221_led_init(phy_id);

	return phy_data;
}