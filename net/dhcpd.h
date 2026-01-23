/*
 * DHCP Server Implementation for U-Boot
 *
 * Copyright (C) 2026 Willem Lee <1980490718@qq.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __DHCPD_H__
#define __DHCPD_H__

#include <net.h>

/* DHCP Magic Cookie */
#define DHCPD_MAGIC 0x63825363

/* DHCP ports */
#define DHCPD_CLIENT_PORT 68
#define DHCPD_SERVER_PORT 67

/* DHCP Message Types */
#define DHCPDISCOVER	1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPDECLINE		4
#define DHCPACK			5
#define DHCPNAK			6
#define DHCPRELEASE		7
#define DHCPINFORM		8

/* DHCP Options */
#define OPTION_PAD						0
#define OPTION_SUBNET_MASK				1
#define OPTION_ROUTER					3
#define OPTION_DNS_SERVER				6
#define OPTION_HOST_NAME				12
#define OPTION_REQUESTED_IP				50
#define OPTION_LEASE_TIME				51
#define OPTION_MESSAGE_TYPE				53
#define OPTION_SERVER_ID				54
#define OPTION_PARAMETER_REQUEST_LIST	55
#define OPTION_CLIENT_ID				61
#define OPTION_MESSAGE					56
#define OPTION_END						255

/* BOOTP/DHCP constants */
#define BOOTREQUEST		1
#define BOOTREPLY		2
#define HTYPE_ETHER		1
#define HLEN_ETHER		6
#define DHCP_FLAG_BROADCAST	0x8000

/* Maximum number of leases */
#define MAX_LEASES 32

/* DHCP Error Codes */
#define SUCCESS				0
#define ERR_INVALID_PARAM	-1
#define ERR_NO_MEMORY		-2
#define ERR_NETWORK			-3
#define ERR_NOT_FOUND		-4
#define ERR_OUT_OF_RANGE	-5
#define ERR_INVALID_PACKET	-6
#define ERR_SERVER_FULL		-7
#define ERR_CONFIG			-8
#define ERR_BUFFER_OVERFLOW	-9
#define RETRY_REQUEST		-10
#define ERR_DHCP_FAILURE	-11

/* DHCP Server States */
typedef enum {
	DHCPD_STATE_STOPPED = 0,
	DHCPD_STATE_RUNNING,
} dhcpd_state_t;

/* BOOTP/DHCP message header (RFC 2131) */
struct dhcpd_pkt {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint32_t ciaddr;
	uint32_t yiaddr;
	uint32_t siaddr;
	uint32_t giaddr;
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint8_t vend[312];
} __packed;

/* Lease information */
struct dhcpd_lease {
	bool used;
	uint8_t mac_addr[6];
	struct in_addr ip_addr;
	unsigned long lease_start;
	unsigned long lease_time;
};

/* Server configuration */
struct dhcpd_svr_cfg {
	struct in_addr server_ip;
	struct in_addr start_ip;
	struct in_addr end_ip;
	struct in_addr netmask;
	struct in_addr gateway;
};

/* Exported functions */
int dhcpd_request(void);
int dhcpd_request_nonblocking(void);
int dhcpd_poll_server(void);
void dhcpd_stop_server(void);
int do_dhcpd(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[]);
void dhcpd_ip_settings(void);

/* Exported variables */
extern struct dhcpd_svr_cfg dhcpd_svr_cfg;

#endif /* __DHCPD_H__ */