ifdef CONFIG_LWIP_HTTPD

LWIPDIR = $(srctree)/failsafe/lwip

LWIP_INC = -I$(srctree)/failsafe \
	-isystem $(srctree)/failsafe/arch \
	-I$(srctree)/failsafe/lwip/include \
	-fno-strict-aliasing

ccflags-y += $(LWIP_INC)

obj-y += failsafe_httpd.o
obj-y += ethernetif.o
obj-y += fs.o
obj-y += lwip/core/init.o
obj-y += lwip/core/def.o
obj-y += lwip/core/mem.o
obj-y += lwip/core/memp.o
obj-y += lwip/core/pbuf.o
obj-y += lwip/core/netif.o
obj-y += lwip/core/ip.o
obj-y += lwip/core/ipv4/ip4.o
obj-y += lwip/core/ipv4/ip4_addr.o
obj-y += lwip/core/ipv4/etharp.o
obj-y += lwip/core/inet_chksum.o
obj-y += lwip/core/tcp.o
obj-y += lwip/core/tcp_in.o
obj-y += lwip/core/tcp_out.o
obj-y += lwip/core/timeouts.o
obj-y += lwip/netif/ethernet.o

endif