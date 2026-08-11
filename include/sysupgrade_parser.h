#ifndef __SYSUPGRADE_PARSER_H
#define __SYSUPGRADE_PARSER_H

typedef struct {
	const void *kernel_data;
	size_t kernel_size;
	const void *rootfs_data;
	size_t rootfs_size;
} sysupgrade_fw_parts;

#define sysupgrade_parts_valid(p) ((p)->kernel_data || (p)->rootfs_data)

sysupgrade_fw_parts parse_sysupgrade_firmware(void *address);
#ifdef CONFIG_CMD_UBI
int sysupgrade_ubi_init(void);
int sysupgrade_write_ubi_volumes(sysupgrade_fw_parts *parts, int backup_enabled);
#endif

#endif