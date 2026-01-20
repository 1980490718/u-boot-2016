/*
 * DHCP Server Implementation for U-Boot
 *
 * Copyright (C) 2026 Willem Lee <1980490718@qq.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <malloc.h>
#include <asm/errno.h>
#include <console.h>
#include "dhcpd.h"

/* Static variables for DHCP server internal state */
static struct dhcpd_lease dhcpd_leases[MAX_LEASES];  /* DHCP lease database */
struct dhcpd_svr_cfg dhcpd_svr_cfg;                  /* Server configuration */

static dhcpd_state_t dhcpd_state = DHCPD_STATE_STOPPED;  /* Current server state */
static rxhand_f *original_udp_handler = NULL;           /* Original UDP handler to restore */

/* DHCP magic cookie as defined in RFC 2131 */
static const uint8_t dhcp_magic_cookie[4] = { 99, 130, 83, 99 };

/**
 * dhcpd_mac_equal - Compare two MAC addresses
 * @a: First MAC address
 * @b: Second MAC address
 * Return: true if addresses are identical
 */
static bool dhcpd_mac_equal(const uint8_t *a, const uint8_t *b) {
	return memcmp(a, b, 6) == 0;
}

/**
 * dhcpd_find_lease - Find DHCP lease by MAC address
 * @mac: Client MAC address to search for
 * Return: Pointer to lease if found, NULL otherwise
 */
static struct dhcpd_lease *dhcpd_find_lease(const uint8_t *mac) {
	for (int i = 0; i < MAX_LEASES; i++) {
		if (dhcpd_leases[i].used && dhcpd_mac_equal(dhcpd_leases[i].mac_addr, mac))
			return &dhcpd_leases[i];
	}
	return NULL;
}

/**
 * dhcpd_ip_in_pool - Check if IP address is within configured DHCP pool
 * @ip_host: IP address in host byte order
 * Return: true if IP is within start_ip to end_ip range
 */
static bool dhcpd_ip_in_pool(uint32_t ip_host) {
	uint32_t start = ntohl(dhcpd_svr_cfg.start_ip.s_addr);
	uint32_t end = ntohl(dhcpd_svr_cfg.end_ip.s_addr);
	return ip_host >= start && ip_host <= end;
}

/**
 * dhcpd_ip_is_allocated - Check if IP address is already leased to any client
 * @ip_host: IP address in host byte order
 * Return: true if IP is already allocated
 */
static bool dhcpd_ip_is_allocated(uint32_t ip_host) {
	for (int i = 0; i < MAX_LEASES; i++) {
		if (dhcpd_leases[i].used && dhcpd_leases[i].ip_addr.s_addr == htonl(ip_host)) {
			return true;
		}
	}
	return false;
}

/**
 * dhcpd_ip_allocated_to_mac - Check if IP is allocated to specific MAC address
 * @ip_host: IP address in host byte order
 * @mac: MAC address to check
 * Return: true if IP is allocated to this specific MAC
 */
static bool dhcpd_ip_allocated_to_mac(uint32_t ip_host, const uint8_t *mac) {
	for (int i = 0; i < MAX_LEASES; i++) {
		if (dhcpd_leases[i].used && dhcpd_leases[i].ip_addr.s_addr == htonl(ip_host) && dhcpd_mac_equal(dhcpd_leases[i].mac_addr, mac)) {
			return true;
		}
	}
	return false;
}

/**
 * dhcpd_print_ip_with_mac - Helper function to print IP and MAC address
 * @ip: IP address to print
 * @mac: MAC address to print
 * @action: Action description (e.g., "offer", "ACK")
 */
static void dhcpd_print_ip_with_mac(struct in_addr ip, const uint8_t *mac, const char *action) {
	char ip_str[16];
	ip_to_string(ip, ip_str);
	printf("DHCP %-6s:%s to %pM\n", action, ip_str, mac);
}

/**
 * dhcpd_validate_config - Validate DHCP server configuration
 * @cfg: Server configuration to validate
 * Return: SUCCESS if valid, error code otherwise
 */
static int dhcpd_validate_config(const struct dhcpd_svr_cfg *cfg) {
	if (!cfg) {
		return ERR_INVALID_PARAM;
	}

	if (cfg->server_ip.s_addr == 0 || cfg->end_ip.s_addr == 0) {
		return ERR_CONFIG;
	}

	return SUCCESS;
}

/**
 * dhcpd_create_lease - Create a new DHCP lease
 * @mac: Client MAC address
 * @ip: IP address to assign
 * @index: Index in dhcpd_leases array to use
 * Return: SUCCESS on success, error code on failure
 */
static int dhcpd_create_lease(const uint8_t *mac, struct in_addr ip, int index) {
	if (!mac || index < 0 || index >= MAX_LEASES) {
		return ERR_INVALID_PARAM;
	}

	dhcpd_leases[index].used = true;
	memcpy(dhcpd_leases[index].mac_addr, mac, 6);
	dhcpd_leases[index].ip_addr = ip;
	dhcpd_leases[index].lease_start = get_timer(0);
	dhcpd_leases[index].lease_time = 3600 * CONFIG_SYS_HZ; /* 1 hour */

	return SUCCESS;
}

/**
 * dhcpd_get_available_lease_slot - Find an available slot in the leases array
 * Return: Index of available slot, or -1 if no slots available
 */
static int dhcpd_get_available_lease_slot(void) {
	for (int i = 0; i < MAX_LEASES; i++) {
		if (!dhcpd_leases[i].used) {
			return i;
		}
	}
	return -1;
}

/**
 * Helper function to allocate IP and create lease
 * @param mac: Client MAC address
 * @param try_ip: IP address to allocate
 * @param allocated_ip: Output parameter for allocated IP
 * @return SUCCESS on success, error code on failure
 */
static int dhcpd_try_allocate_ip(const uint8_t *mac, uint32_t try_ip, struct in_addr *allocated_ip) {
	int lease_idx = dhcpd_get_available_lease_slot();
	if (lease_idx >= 0) {
		struct in_addr ip_addr;
		ip_addr.s_addr = htonl(try_ip);
		dhcpd_create_lease(mac, ip_addr, lease_idx);
		*allocated_ip = dhcpd_leases[lease_idx].ip_addr;
		return SUCCESS;
	}
	/* No free lease slots */
	allocated_ip->s_addr = 0;
	return ERR_SERVER_FULL;
}

static int dhcpd_alloc_ip(const uint8_t *mac, struct in_addr *allocated_ip) {
    uint32_t start = ntohl(dhcpd_svr_cfg.start_ip.s_addr);
    uint32_t end = ntohl(dhcpd_svr_cfg.end_ip.s_addr);

    /* First check for existing lease for this MAC */
    for (int i = 0; i < MAX_LEASES; i++) {
        if (dhcpd_leases[i].used && dhcpd_mac_equal(dhcpd_leases[i].mac_addr, mac)) {
            /* Update lease timestamp and return the IP */
            dhcpd_leases[i].lease_start = get_timer(0);
            *allocated_ip = dhcpd_leases[i].ip_addr;
            return SUCCESS;
        }
    }

    /* Total available IPs in the pool (server IP is not in range) */
    uint32_t total_ips = end - start + 1;

    /* Try random allocation */
    int max_attempts = total_ips < 100 ? total_ips : 100;  /* Limit attempts to avoid infinite loop */
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        /* Generate a random IP within the range */
        uint32_t mac_hash = 0;
        for (int i = 0; i < 6; i++) {
            mac_hash = mac_hash * 31 + mac[i];
        }
        /* Add timer value to make it more random */
        uint32_t seed = mac_hash + get_timer(0) + attempt;
        uint32_t random_offset = seed % (end - start + 1);
        uint32_t random_ip = start + random_offset;
        /* Check if this IP is available */
        if (!dhcpd_ip_is_allocated(random_ip)) {
            /* Found available IP, create lease */
            return dhcpd_try_allocate_ip(mac, random_ip, allocated_ip);
        }

        /* Check if this IP belongs to this client (inconsistency case) */
        if (dhcpd_ip_allocated_to_mac(random_ip, mac)) {
            /* This IP is already assigned to this client */
            allocated_ip->s_addr = htonl(random_ip);
            return SUCCESS;
        }
    }

    /* If random allocation fails, fall back to linear search for available IP */
    uint32_t current_ip = start;
    int total_ips_in_pool = end - start + 1;
    int count = 0;

    while (count < total_ips_in_pool) {
        if (!dhcpd_ip_is_allocated(current_ip)) {
            /* Found available IP, create lease */
            return dhcpd_try_allocate_ip(mac, current_ip, allocated_ip);
        }

        current_ip = (current_ip < end) ? current_ip + 1 : start;
        count++;
    }

    /* All IPs are taken, return error */
    allocated_ip->s_addr = 0;
    return ERR_SERVER_FULL;
}

/**
 * dhcpd_parse_option - Parse DHCP option from packet
 * @bp: DHCP packet pointer
 * @len: Packet length
 * @option_code: Option code to find
 * @expected_len: Expected option length (0 for any length)
 * @result: Buffer to store option value
 * @result_len: Pointer to store actual option length
 * Return: SUCCESS if found, error code otherwise
 */
static int dhcpd_parse_option(const struct dhcpd_pkt *bp, unsigned int len, uint8_t option_code, uint8_t expected_len, void *result, uint8_t *result_len) {
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const uint8_t *opt;
	unsigned int optlen;

	if (!bp || len < fixed + 4)
		return ERR_INVALID_PARAM;

	opt = (const uint8_t *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return ERR_INVALID_PACKET;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		uint8_t code, olen;
		code = *opt++;
		optlen--;

		if (code == OPTION_PAD)
			continue;
		if (code == OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == option_code) {
			if (expected_len == 0 || olen == expected_len) {
				if (result) {
					memcpy(result, opt, olen);
				}
				if (result_len) {
					*result_len = olen;
				}
				return SUCCESS;
			}
		}

		opt += olen;
		optlen -= olen;
	}

	return ERR_NOT_FOUND;
}

/**
 * dhcpd_parse_msg_type - Parse DHCP message type from options
 * @bp: DHCP packet pointer
 * @len: Packet length
 * @msg_type: Output parameter for message type
 * Return: SUCCESS if found, error code otherwise
 */
static int dhcpd_parse_msg_type(const struct dhcpd_pkt *bp, unsigned int len, uint8_t *msg_type) {
	if (!msg_type) {
		return ERR_INVALID_PARAM;
	}

	if (dhcpd_parse_option(bp, len, OPTION_MESSAGE_TYPE, 1, msg_type, NULL) == SUCCESS) {
		return SUCCESS;
	}

	return ERR_NOT_FOUND;
}

/**
 * dhcpd_parse_req_ip - Parse requested IP address from DHCP options
 * @bp: DHCP packet pointer
 * @len: Packet length
 * @req_ip: Output parameter for requested IP
 * Return: SUCCESS if found, error code otherwise
 */
static int dhcpd_parse_req_ip(const struct dhcpd_pkt *bp, unsigned int len, struct in_addr *req_ip) {
	if (!req_ip) {
		return ERR_INVALID_PARAM;
	}

	return dhcpd_parse_option(bp, len, OPTION_REQUESTED_IP, 4, &req_ip->s_addr, NULL);
}

/**
 * dhcpd_parse_server_id - Parse server identifier from DHCP options
 * @bp: DHCP packet pointer
 * @len: Packet length
 * @server_id: Output parameter for server identifier
 * Return: SUCCESS if found, error code otherwise
 */
static int dhcpd_parse_server_id(const struct dhcpd_pkt *bp, unsigned int len, struct in_addr *server_id) {
	if (!server_id) {
		return ERR_INVALID_PARAM;
	}

	return dhcpd_parse_option(bp, len, OPTION_SERVER_ID, 4, &server_id->s_addr, NULL);
}

/**
 * dhcpd_validate_request - Validate DHCP request parameters
 * @client_mac: Client MAC address
 * @req_ip: Requested IP address
 * @nak_msg: Output parameter for NAK message if validation fails
 * Return: SUCCESS if valid, error code with NAK message otherwise
 */
static int dhcpd_validate_request(const uint8_t *client_mac, struct in_addr req_ip, const char **nak_msg) {
	uint32_t ip_host = ntohl(req_ip.s_addr);

	if (!client_mac || !nak_msg) {
		return ERR_INVALID_PARAM;
	}

	/* Check if requested IP is in our subnet */
	uint32_t network = dhcpd_svr_cfg.server_ip.s_addr & dhcpd_svr_cfg.netmask.s_addr;
	uint32_t req_network = req_ip.s_addr & dhcpd_svr_cfg.netmask.s_addr;

	if (req_network != network) {
		*nak_msg = "requested address not on local network";
		return ERR_INVALID_PACKET;
	}

	/* Check if requested IP is in our IP pool */
	if (!dhcpd_ip_in_pool(ip_host)) {
		*nak_msg = "requested address not available";
		return ERR_OUT_OF_RANGE;
	}

	/* Since server IP is not in the pool range, no need to check if requested IP equals server IP */

	return SUCCESS;  /* Request is valid */
}

/**
 * dhcpd_process_lease - Create or update DHCP lease for client
 * @client_mac: Client MAC address
 * @req_ip: Requested IP address
 * @processed_ip: Output parameter for processed IP address
 * Return: SUCCESS on success, error code on failure
 */
static int dhcpd_process_lease(const uint8_t *client_mac, struct in_addr req_ip, struct in_addr *processed_ip) {
	struct dhcpd_lease *lease;
	uint32_t ip_host = ntohl(req_ip.s_addr);

	if (!client_mac || !processed_ip) {
		return ERR_INVALID_PARAM;
	}

	lease = dhcpd_find_lease(client_mac);

	/* First check if requested IP is already taken by another client */
	if (dhcpd_ip_is_allocated(ip_host) && !dhcpd_ip_allocated_to_mac(ip_host, client_mac)) {
		/* IP is taken by another client, can't assign it */
		return dhcpd_alloc_ip(client_mac, processed_ip);
	}

	/* Find existing lease */
	if (lease) {
		/* Update existing lease's IP */
		lease->ip_addr = req_ip;
		lease->lease_start = get_timer(0);
		lease->lease_time = 3600 * CONFIG_SYS_HZ; /* 1 hour */
		*processed_ip = req_ip;
		return SUCCESS;
	} else {
		/* Create new lease */
		int lease_idx = dhcpd_get_available_lease_slot();
		if (lease_idx >= 0) {
			dhcpd_create_lease(client_mac, req_ip, lease_idx);
			*processed_ip = req_ip;
			return SUCCESS;
		}
		/* No free lease slots */
		*processed_ip = req_ip;
		return ERR_SERVER_FULL;
	}
}

/* DHCP option helper functions */

/**
 * dhcpd_opt_add_u8 - Add 8-bit option to DHCP packet
 * @p: Current position in options buffer
 * @code: Option code
 * @val: Option value
 * Return: Updated buffer position
 */
static uint8_t *dhcpd_opt_add_u8(uint8_t *p, uint8_t code, uint8_t val) {
	*p++ = code;
	*p++ = 1;
	*p++ = val;
	return p;
}

/**
 * dhcpd_opt_add_u32 - Add 32-bit option to DHCP packet
 * @p: Current position in options buffer
 * @code: Option code
 * @val: Option value
 * Return: Updated buffer position
 */
static uint8_t *dhcpd_opt_add_u32(uint8_t *p, uint8_t code, uint32_t val) {
	*p++ = code;
	*p++ = 4;
	memcpy(p, &val, 4);
	return p + 4;
}

/**
 * dhcpd_opt_add_inaddr - Add IP address option to DHCP packet
 * @p: Current position in options buffer
 * @code: Option code
 * @addr: IP address
 * Return: Updated buffer position
 */
static uint8_t *dhcpd_opt_add_inaddr(uint8_t *p, uint8_t code, struct in_addr addr) {
	return dhcpd_opt_add_u32(p, code, addr.s_addr);
}

/**
 * dhcpd_send_reply - Construct and send DHCP reply packet
 * @req: Original DHCP request packet
 * @req_len: Request packet length
 * @dhcp_msg_type: DHCP message type (OFFER/ACK/NAK)
 * @yiaddr: IP address to assign (0 for NAK)
 * @nak_message: NAK message string (for NAK only)
 * Return: SUCCESS on success, error code on failure
 */
static int dhcpd_send_reply(const struct dhcpd_pkt *req, unsigned int req_len, uint8_t dhcp_msg_type, struct in_addr yiaddr, const char *nak_message) {
	struct dhcpd_pkt *bp;
	struct in_addr server_ip, netmask, gateway, dns;
	struct in_addr dest_addr;
	uchar *pkt, *payload;
	int eth_hdr_size, payload_len;
	uint8_t *opt, *opt_end;
	uint32_t lease;
	(void)req_len;

	server_ip = dhcpd_svr_cfg.server_ip;
	netmask = dhcpd_svr_cfg.netmask;
	gateway = dhcpd_svr_cfg.gateway;
	dns = server_ip; /* Use server IP as DNS for simplicity */

	pkt = net_tx_packet;
	if (!pkt) {
		return ERR_NETWORK;
	}

	eth_hdr_size = net_set_ether(pkt, net_bcast_ethaddr, PROT_IP);

	/* For DHCP responses, we need to send to the client */
	/* If ciaddr is set, send to that IP, otherwise broadcast to 255.255.255.255 */
	if (req->ciaddr != 0) {
		dest_addr.s_addr = req->ciaddr;
	} else {
		dest_addr.s_addr = htonl(0xFFFFFFFF); /* Broadcast in network byte order */
	}

	payload = pkt + eth_hdr_size + IP_HDR_SIZE + UDP_HDR_SIZE;
	bp = (struct dhcpd_pkt *)payload;
	memset(bp, 0, sizeof(*bp));

	bp->op = BOOTREPLY;
	bp->htype = HTYPE_ETHER;
	bp->hlen = HLEN_ETHER;
	bp->hops = 0;
	bp->xid = req->xid;
	bp->secs = req->secs;
	bp->flags = htons(DHCP_FLAG_BROADCAST);
	bp->ciaddr = req->ciaddr;

	/* For NAK, yiaddr is set to 0 */
	if (dhcp_msg_type == DHCPNAK) {
		bp->yiaddr = 0;
		bp->siaddr = 0;
	} else {
		bp->yiaddr = yiaddr.s_addr;
		bp->siaddr = server_ip.s_addr;
	}

	bp->giaddr = 0;
	memcpy(bp->chaddr, req->chaddr, sizeof(bp->chaddr));

	opt = (uint8_t *)bp->vend;
	memcpy(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie));
	opt += 4;

	opt_end = (uint8_t *)bp->vend + sizeof(bp->vend);

	/* Add message type option */
	if (opt + 3 <= opt_end) {  /* At least 1 byte code + 1 byte len + 1 byte data */
		opt = dhcpd_opt_add_u8(opt, OPTION_MESSAGE_TYPE, dhcp_msg_type);
	} else {
		return ERR_BUFFER_OVERFLOW;
	}

	/* For NAK, only include Server Identifier and Message options */
	if (dhcp_msg_type == DHCPNAK) {
		/* Add server identifier */
		if (opt + 6 <= opt_end) {  /* At least 1 byte code + 1 byte len + 4 bytes data */
			opt = dhcpd_opt_add_inaddr(opt, OPTION_SERVER_ID, server_ip);
		} else {
			return ERR_BUFFER_OVERFLOW;
		}

		/* Add NAK message if provided */
		if (nak_message && *nak_message) {
			int msg_len = strlen(nak_message);
			/* Limit message length to prevent buffer overflow */
			if (msg_len > 64) msg_len = 64;  // Limit to 64 bytes to be safe

			/* Ensure we have enough space: 1 byte code + 1 byte len + msg_len bytes */
			if (opt + 2 + msg_len <= opt_end) {
				*opt++ = OPTION_MESSAGE;
				*opt++ = msg_len;
				memcpy(opt, nak_message, msg_len);
				opt += msg_len;
			} else {
				/* Not enough space, skip adding message */
			}
		}
	} else {
		/* For OFFER/ACK, include complete network configuration */
		if (opt + 6 <= opt_end) {  /* Server ID option */
			opt = dhcpd_opt_add_inaddr(opt, OPTION_SERVER_ID, server_ip);
		} else {
			return ERR_BUFFER_OVERFLOW;
		}

		if (opt + 6 <= opt_end) {  /* Subnet mask option */
			opt = dhcpd_opt_add_inaddr(opt, OPTION_SUBNET_MASK, netmask);
		} else {
			return ERR_BUFFER_OVERFLOW;
		}

		if (gateway.s_addr != 0) {
			if (opt + 6 <= opt_end) {  /* Router option */
				opt = dhcpd_opt_add_inaddr(opt, OPTION_ROUTER, gateway);
			} else {
				return ERR_BUFFER_OVERFLOW;
			}
		}

		if (opt + 6 <= opt_end) {  /* DNS server option */
			opt = dhcpd_opt_add_inaddr(opt, OPTION_DNS_SERVER, dns);
		} else {
			return ERR_BUFFER_OVERFLOW;
		}

		lease = htonl(3600); /* 1 hour lease time */
		if (opt + 6 <= opt_end) {  /* Lease time option */
			opt = dhcpd_opt_add_u32(opt, OPTION_LEASE_TIME, lease);
		} else {
			return ERR_BUFFER_OVERFLOW;
		}
	}

	if (opt < opt_end) {
		*opt++ = OPTION_END;
	}

	payload_len = (int)((uintptr_t)opt - (uintptr_t)payload);
	/* Ensure minimum BOOTP packet size */
	if (payload_len < 300) {
		int padding_needed = 300 - payload_len;
		if (opt + padding_needed <= opt_end) {
			/* Use OPTION_PAD (0) to fill the remaining space */
			memset(opt, OPTION_PAD, padding_needed);
			opt += padding_needed;
			payload_len = 300;
		} else {
			/* Buffer too small for minimum packet size, but we'll still send what we have */
			printf("DHCP: Warning - cannot pad to 300 bytes (buffer full)\n");
		}
	}

	net_set_udp_header(pkt + eth_hdr_size, dest_addr, DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, payload_len);
	net_send_packet(pkt, eth_hdr_size + IP_HDR_SIZE + UDP_HDR_SIZE + payload_len);

	return SUCCESS;
}

/**
 * dhcpd_handle_request - Process DHCP REQUEST message
 * @bp: DHCP packet pointer
 * @len: Packet length
 * Return: SUCCESS on successful processing, error code otherwise
 */
static int dhcpd_handle_request(const struct dhcpd_pkt *bp, unsigned int len) {
	struct in_addr req_ip, yiaddr, server_id;
	int ret;
	const char *nak_msg = NULL;

	/* Check if request is for our server */
	ret = dhcpd_parse_server_id(bp, len, &server_id);
	if (ret == SUCCESS) {
		if (server_id.s_addr != dhcpd_svr_cfg.server_ip.s_addr) {
			return SUCCESS;  /* Not for our server, ignore */
		}
	} else if (ret != ERR_NOT_FOUND) {
		return ret;  /* Other error occurred */
	}

	/* Parse requested IP address */
	if (dhcpd_parse_req_ip(bp, len, &req_ip) != SUCCESS) {
		/* No requested IP specified, allocate new IP */
		ret = dhcpd_alloc_ip(bp->chaddr, &yiaddr);
		if (ret != SUCCESS) {
			return ret;
		}
		goto send_ack;
	}

	/* Validate requested IP address */
	ret = dhcpd_validate_request(bp->chaddr, req_ip, &nak_msg);
	if (ret != SUCCESS) {
		goto send_nak;
	}

	/* Request is valid, process lease */
	ret = dhcpd_process_lease(bp->chaddr, req_ip, &yiaddr);
	if (ret != SUCCESS) {
		return ret;
	}

send_ack:
	dhcpd_print_ip_with_mac(yiaddr, bp->chaddr, "ACK");
	return dhcpd_send_reply(bp, len, DHCPACK, yiaddr, NULL);

send_nak:
	printf("DHCP: NAK to %pM (%s)\n", bp->chaddr, nak_msg ? nak_msg : "");
	return dhcpd_send_reply(bp, len, DHCPNAK, (struct in_addr){0}, nak_msg);
}

/**
 * dhcpd_handle_discover - Process DHCP DISCOVER message
 * @client_mac: Client MAC address
 * @yiaddr: Output parameter for offered IP address
 * Return: SUCCESS on success, error code on failure
 */
static int dhcpd_handle_discover(const uint8_t *client_mac, struct in_addr *yiaddr) {
	/* Find existing lease */
	struct dhcpd_lease *lease;
	uint32_t ip_host;
	if (!client_mac || !yiaddr) {
		return ERR_INVALID_PARAM;
	}

	lease = dhcpd_find_lease(client_mac);

	if (lease) {
		/* Check if IP in lease is still available */
		ip_host = ntohl(lease->ip_addr.s_addr);

		if (!dhcpd_ip_is_allocated(ip_host) || dhcpd_ip_allocated_to_mac(ip_host, client_mac)) {
			/* IP is available or still belongs to this client */
			*yiaddr = lease->ip_addr;
			return SUCCESS;
		} else {
			/* IP has been taken by another client, reallocate */
			/* Remove old lease */
			for (int i = 0; i < MAX_LEASES; i++) {
				if (dhcpd_leases[i].used && dhcpd_mac_equal(dhcpd_leases[i].mac_addr, client_mac)) {
					dhcpd_leases[i].used = false;
					memset(dhcpd_leases[i].mac_addr, 0, 6);
					break;
				}
			}
		}
	}

	/* New client or needs reallocation, use MAC-based allocation */
	return dhcpd_alloc_ip(client_mac, yiaddr);
}

/**
 * dhcpd_handler_with_fallback - Main UDP packet handler with DHCP detection
 * @pkt: Received packet data
 * @dport: Destination port
 * @sip: Source IP address
 * @sport: Source port
 * @len: Packet length
 *
 * This function handles DHCP packets and forwards non-DHCP traffic
 * to the original UDP handler. It identifies DHCP packets by checking:
 * 1. BOOTREQUEST opcode
 * 2. Ethernet hardware type
 * 3. DHCP magic cookie in options
 */
static void dhcpd_handler_with_fallback(uchar *pkt, unsigned dport, struct in_addr sip, unsigned sport, unsigned len) {
	const struct dhcpd_pkt *bp = (const struct dhcpd_pkt *)pkt;
	uint8_t msg_type;
	struct in_addr yiaddr;
	int ret;

	/* Check if this is a DHCP packet by validating its structure */
	if (len >= offsetof(struct dhcpd_pkt, vend) && bp->op == BOOTREQUEST && bp->htype == HTYPE_ETHER && bp->hlen == HLEN_ETHER) {
		/* Check if this is a DHCP packet by looking for the magic cookie */
		unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
		if (len >= fixed + 4 && memcmp((const uint8_t *)bp->vend, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)) == 0) {
			/* This is a DHCP packet, check message type */
			if (dhcpd_parse_msg_type(bp, len, &msg_type) == SUCCESS) {
				/* Process as DHCP packet */
				switch (msg_type) {
					case DHCPDISCOVER:
						ret = dhcpd_handle_discover(bp->chaddr, &yiaddr);
						if (ret == SUCCESS) {
							dhcpd_print_ip_with_mac(yiaddr, bp->chaddr, "Offer");
						} else {
							printf("Failed to handle DHCP DISCOVER: error %d\n", ret);
						}
						dhcpd_send_reply(bp, len, DHCPOFFER, yiaddr, NULL);
						return;
					case DHCPREQUEST:
						dhcpd_handle_request(bp, len);
						return;
					default:
						break;
				}
			}
		}
	}

	/* This is not a DHCP packet, forward to original handler if available */
	if (original_udp_handler) {
		original_udp_handler(pkt, dport, sip, sport, len);
	}
}

/**
 * dhcpd_set_config - Set DHCP server configuration
 * @cfg: Server configuration structure
 * Return: SUCCESS on success, error code on failure
 */
int dhcpd_set_config(struct dhcpd_svr_cfg *cfg) {
	int ret = dhcpd_validate_config(cfg);
	if (ret != SUCCESS) {
		return ret;
	}

	/* Copy configuration */
	memcpy(&dhcpd_svr_cfg, cfg, sizeof(dhcpd_svr_cfg));
	return SUCCESS;
}

/**
 * dhcpd_init_server - Initialize DHCP server
 * Return: SUCCESS on success, error code on failure
 *
 * Initializes network, sets up UDP handler, and prints server info.
 */
int dhcpd_init_server(void) {
	/* Configure IP settings first */
	dhcpd_ip_settings();
	/* Initialize leases if not already done */
	memset(dhcpd_leases, 0, sizeof(dhcpd_leases));

	/* Save the original UDP handler to restore later */
	original_udp_handler = net_get_udp_handler();

	/* Set up DHCP handler with fallback to original handler for non-DHCP traffic */
	net_set_udp_handler(dhcpd_handler_with_fallback);

	/* Print server info */
	char start_str[16], end_str[16], server_str[16], gateway_str[16], netmask_str[16];
	ip_to_string(dhcpd_svr_cfg.start_ip, start_str);
	ip_to_string(dhcpd_svr_cfg.end_ip, end_str);
	ip_to_string(dhcpd_svr_cfg.server_ip, server_str);
	ip_to_string(dhcpd_svr_cfg.gateway, gateway_str);
	ip_to_string(dhcpd_svr_cfg.netmask, netmask_str);

	printf("DHCPServer :%s\n", server_str);
	printf("Netmask    :%s\n", netmask_str);
	printf("Gateway    :%s\n", gateway_str);
	printf("PoolStart  :%s\n", start_str);
	printf("PoolEnd    :%s\n", end_str);

	return SUCCESS;
}

/**
 * dhcpd_deinit_server - Deinitialize DHCP server
 *
 * Restores original UDP handler and resets network state.
 */
void dhcpd_deinit_server(void) {
	/* Clean up - restore original UDP handler */
	if (original_udp_handler) {
		net_set_udp_handler(original_udp_handler);
		original_udp_handler = NULL;
	}

	/* Reset network state */
	net_set_state(NETLOOP_SUCCESS);

	printf("DHCP server stopped\n");
}

/**
 * dhcpd_poll_server - Non-blocking server polling function
 * Return: SUCCESS if running, error code if stopped or interrupted
 *
 * This function should be called periodically in non-blocking mode
 * to process incoming DHCP packets.
 */
int dhcpd_poll_server(void) {
	if (dhcpd_state != DHCPD_STATE_RUNNING) {
		return ERR_NETWORK;  /* Server not running */
	}

	eth_rx();

	// Check for Ctrl+C interruption
	if (ctrlc()) {
		dhcpd_stop_server();
		return ERR_NETWORK;  /* Indicate interruption */
	}

	return SUCCESS;
}

/**
 * dhcpd_stop_server - Stop DHCP server
 *
 * Gracefully stops the server and cleans up resources.
 */
void dhcpd_stop_server(void) {
	if (dhcpd_state != DHCPD_STATE_STOPPED) {
		dhcpd_deinit_server();
		dhcpd_state = DHCPD_STATE_STOPPED;
	}
}

/**
 * dhcpd_ip_settings - Configure IP settings from environment variables
 *
 * Configures server IP from environment variable ipaddr, calculates start IP as ipaddr+1,
 * end IP as ipaddr+100, with boundary checks against network and broadcast addresses.
 * Netmask is obtained from environment variable netmask.
 * Gateway is obtained from environment variable gatewayip if set, otherwise defaults to server IP.
 */
void dhcpd_ip_settings(void) {
	char *env_ip = getenv("ipaddr");
	char *env_netmask = getenv("netmask");
	char *env_gateway = getenv("gatewayip");
	/* If ipaddr or netmask not set, use default values */
	if (env_ip == NULL || env_netmask == NULL) {
		run_command("env default ipaddr netmask", 0);
		env_ip = getenv("ipaddr");
		env_netmask = getenv("netmask");
	}

	/* Convert strings to IP addresses */
	struct in_addr ip_addr = string_to_ip(env_ip);
	struct in_addr server_addr = ip_addr;  // Use ipaddr as server IP
	struct in_addr netmask_addr = string_to_ip(env_netmask);

	dhcpd_svr_cfg.server_ip = server_addr;

	/* Calculate network and broadcast addresses */
	uint32_t ip_addr_int = ntohl(ip_addr.s_addr);
	uint32_t netmask_int = ntohl(netmask_addr.s_addr);
	uint32_t network_addr = ip_addr_int & netmask_int;
	uint32_t broadcast_addr = network_addr | (~netmask_int);
	/* Calculate DHCP pool start and end IPs as ipaddr+1 and ipaddr+100 respectively */
	uint32_t start_ip = ip_addr_int + 1;
	uint32_t end_ip = ip_addr_int + 100;

	/* Boundary check - ensure IPs are within valid range */
	if (start_ip <= network_addr) start_ip = network_addr + 1;
	if (end_ip >= broadcast_addr) end_ip = broadcast_addr - 1;
	if (start_ip > end_ip) {
		/* If start IP is greater than end IP (e.g., ipaddr set to 254), provide at least 10 IPs */
		start_ip = network_addr + 1;
		end_ip = (start_ip + 10 < broadcast_addr) ? start_ip + 10 : broadcast_addr - 1;
		if (end_ip > broadcast_addr - 1) end_ip = broadcast_addr - 1;
	}

	dhcpd_svr_cfg.start_ip.s_addr = htonl(start_ip);
	dhcpd_svr_cfg.end_ip.s_addr = htonl(end_ip);
	dhcpd_svr_cfg.netmask = netmask_addr;

	/* Set gateway to environment value or server IP if not set */
	if (env_gateway != NULL) {
		dhcpd_svr_cfg.gateway = string_to_ip(env_gateway);
	} else {
		/* Default to server IP (ipaddr) */
		dhcpd_svr_cfg.gateway = server_addr;
	}
}

/**
 * dhcpd_request - Start DHCP server in blocking mode (for console)
 * Return: SUCCESS on normal exit, error code on failure
 *
 * This function runs a blocking main loop that processes DHCP requests
 * until interrupted by Ctrl+C. It's intended for standalone console use.
 */
int dhcpd_request(void) {
	eth_init();
	mdelay(1500);
	/* Initialize the server */
	if (dhcpd_init_server() != SUCCESS) {
		return ERR_NETWORK;
	}

	dhcpd_state = DHCPD_STATE_RUNNING;

	/* Main server loop */
	while (1) {
		eth_rx();
		if (ctrlc()) {
			break;
		}
		udelay(1000);
	}

	/* Deinitialize the server */
	dhcpd_stop_server();
	return SUCCESS;
}

/**
 * dhcpd_request_nonblocking - Start DHCP server in non-blocking mode
 * Return: SUCCESS on success, error code on failure
 *
 * This function initializes the server and processes packets for a short
 * duration (5 seconds) before returning. It's intended for integration
 * with other services like HTTP server.
 */
int dhcpd_request_nonblocking(void) {
#ifndef CONFIG_HTTPD
	eth_init();
	udelay(10000);
#endif
	/* Initialize the server */
	if (dhcpd_init_server() != SUCCESS) {
		return ERR_NETWORK;
	}

	dhcpd_state = DHCPD_STATE_RUNNING;

	/* Process packets for a short duration to handle initial DHCP requests */
	unsigned long start = get_timer(0);
	unsigned long timeout = 2 * CONFIG_SYS_HZ; /* 2 seconds timeout */

	while (get_timer(start) < timeout) {
		eth_rx();
		udelay(1000);  /* Small delay to prevent excessive CPU usage */
	}

	return SUCCESS;
}

/**
 * do_dhcpd - U-Boot command implementation for DHCP server
 * @cmdtp: Command table pointer
 * @flag: Command flags
 * @argc: Argument count
 * @argv: Argument vector
 * Return: SUCCESS on success, error code on failure
 *
 * Command usage:
 *   dhcpd      - Start DHCP server in blocking mode
 *   dhcpd -nb  - Start DHCP server in non-blocking mode
 */
int do_dhcpd(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[]) {
	/* Configure IP settings from environment or defaults */
	dhcpd_ip_settings();

	/* Validate IP configuration */
	int ret = dhcpd_validate_config(&dhcpd_svr_cfg);
	if (ret != SUCCESS) {
		printf("Error: Invalid IP configuration\n");
		return ret;
	}

	net_init();

	net_ip = dhcpd_svr_cfg.server_ip;
	if (!net_netmask.s_addr)
		net_netmask = dhcpd_svr_cfg.netmask;
	if (!net_gateway.s_addr)
		net_gateway = dhcpd_svr_cfg.gateway;

	/* Check for non-blocking flag */
	if (argc > 1 && strcmp(argv[1], "-nb") == 0) {
		return dhcpd_request_nonblocking();
	}

	return dhcpd_request();
}

U_BOOT_CMD(
	dhcpd, 2, 1, do_dhcpd,
	"Start DHCP server using environment variables",
	"[-nb]\n"
	"  Start DHCP server for IP address assignment\n"
	"  -nb: Non-blocking mode (returns immediately)\n"
	"Environment variables used:\n"
	"  ipaddr, netmask, gatewayip (optional)"
);