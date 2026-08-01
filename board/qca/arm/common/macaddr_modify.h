#ifndef __MACADDR_MODIFY_H
#define __MACADDR_MODIFY_H

#include <common.h>

int macaddr_read_base(u8 *mac, int mac_index);
int macaddr_read_wifi(u8 *mac, int wifi_index);
int macaddr_modify_base(const u8 *new_mac, int mac_index);
int macaddr_modify_wifi(const u8 *new_mac, int wifi_index);
u32 macaddr_wifi_offset(int wifi_index);
u32 macaddr_wifi_cs_offset(int wifi_index);
u16 macaddr_wifi_checksum(int wifi_index);
int macaddr_wifi_cs_valid(int wifi_index);

#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ806X)
#define MACADDR_WIFI_MAX	3
#else
#define MACADDR_WIFI_MAX	6
#endif

#endif