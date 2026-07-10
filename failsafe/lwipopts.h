#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

#define LWIP_NOASSERT                   1

#define LWIP_IPV4                        1
#define LWIP_IPV6                        0
#define LWIP_ARP                         1
#define ARP_TABLE_SIZE                   8
#define ARP_QUEUEING                     0

#define IP_REASSEMBLY                    0
#define IP_FRAG                          0
#define IP_REASS_MAX_PBUFS               0

#define LWIP_ICMP                        0
#define LWIP_IGMP                        0

#define LWIP_UDP                         0
#define LWIP_TCP                         1
#define LWIP_TCP_KEEPALIVE               0
#define LWIP_SINGLE_NETIF                1
#define TCP_MSS                           1446
#define TCP_SND_BUF                       (TCP_MSS * 45)
#define TCP_SND_QUEUELEN                  (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_SNDLOWAT                      LWIP_MAX(TCP_SND_BUF / 4, (2 * TCP_MSS) + 1)
#define TCP_WND                           (TCP_MSS * 45)
#define TCP_QUEUE_OOSEQ                   0
#define LWIP_TCP_SACK_OUT                 0
#define LWIP_TCP_MAX_SACK_NUM             4
#define LWIP_WND_SCALE                    0
#define TCP_RCV_SCALE                     0
#define LWIP_TCP_TIMESTAMPS               0
#define TCP_CALCULATE_EFF_SEND_MSS        1
#define TCP_MAXRTX                        12
#define TCP_SYNMAXRTX                     6
#define TCP_OOSEQ_MAX_PBUFS               0
#define TCP_OOSEQ_MAX_BYTES               0
#define MEMP_NUM_TCP_SEG                  512
#define MEMP_NUM_TCP_PCB_LISTEN           2
#define MEMP_NUM_TCP_PCB                  4
#define MEMP_NUM_REASSDATA                0
#define MEMP_NUM_ARP_QUEUE                8

#define LWIP_NETIF_TX_SINGLE_PBUF         1
#define TCP_OVERSIZE                      TCP_MSS

#define MEM_SIZE                          (512 * 1024)
#define MEM_ALIGNMENT                     4
#define MEMP_OVERFLOW_CHECK               0
#define MEMP_SANITY_CHECK                 0
#define MEM_USE_POOLS                     0

#define PBUF_POOL_SIZE                    64
#define PBUF_POOL_BUFSIZE                (TCP_MSS + 40 + 14)
#define PBUF_LINK_HLEN                    14

#define LWIP_NETIF_STATUS_CALLBACK        0
#define LWIP_NETIF_LINK_CALLBACK          0
#define LWIP_NETIF_HWADDRHINT             0

#define LWIP_STATS                        0
#define LWIP_STATS_DISPLAY                0

#define LWIP_CHECKSUM_CTRL_PER_NETIF      0
#define LWIP_CHECKSUM_ON_COPY             1

#define LWIP_PROVIDE_ERRNO                1

#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1

#define LWIP_HOOK_FILENAME                "lwip_hooks.h"

#define LWIP_NO_LIMITS_H                  1

#define LWIP_HTTPD                        0

#define PPP_SUPPORT                       0
#define PPPOE_SUPPORT                     0
#define PPP_NUM_TIMEOUTS                  0

#define LWIP_ND6_TCP_REACHABILITY_HINTS   0

#endif