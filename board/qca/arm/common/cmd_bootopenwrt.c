/*
 * cmd_bootopenwrt.c - OpenWrt FIT boot command for SoftBank Air5
 *
 * Copyright (c) 2024, OpenWrt Community
 *
 * Boot flow for SoftBank Air5 with W25R512NWEIQ 64MB SPI-NOR:
 *
 *  Flash layout (64MB):
 *   0x00000000 - 0x003FFFFF : Boot chain (SBL/TZ/SMEM/U-Boot) [4MB]
 *   0x00400000 - end        : OpenWrt FIT image (kernel + dtb)
 *
 *  Boot sequence:
 *   1. Probe SPI-NOR flash
 *   2. Read FIT header from 0x00400000 to determine image size
 *   3. Load full FIT image to RAM
 *   4. Use bootm/FIT to boot kernel (rootfs loaded by kernel from flash)
 *
 *  Brick-proof (failsafe) recovery:
 *   - Press and hold reset button during boot
 *   - U-Boot enters TFTP recovery mode (192.168.1.1)
 *   - Upload new FIT image via TFTP -> written to 0x00400000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <common.h>
#include <command.h>
#include <bootm.h>
#include <image.h>
#include <errno.h>
#include <asm/arch-qca-common/qca_common.h>

/* SPI-NOR flash offset where OpenWrt FIT image starts */
#define OPENWRT_KERNEL_FLASH_OFFSET	0x00400000UL

/* Maximum FIT image size to read initially for header parsing (512KB) */
#define FIT_HEADER_READ_SIZE		(512 * 1024)

/* Maximum kernel+dtb FIT image size (32MB should be more than enough) */
#define OPENWRT_KERNEL_MAX_SIZE		(32 * 1024 * 1024)

/* RAM load address for FIT image */
#define OPENWRT_LOAD_ADDR		CONFIG_SYS_LOAD_ADDR

/* GPIO for reset button (failsafe detection) - IPQ807x typical reset GPIO */
#define RESET_BUTTON_GPIO		57

/* Failsafe web recovery IP */
#define FAILSAFE_IPADDR			"192.168.1.1"

DECLARE_GLOBAL_DATA_PTR;

extern bootm_headers_t images;

/*
 * Read GPIO input value
 * Returns 1 if button pressed (low = active), 0 otherwise
 */
static int is_reset_button_pressed(void)
{
	u32 val = *(volatile u32 *)GPIO_IN_OUT_ADDR(RESET_BUTTON_GPIO);
	/* Active low: bit 0 = current pin value, 0 = pressed */
	return ((val & 0x1) == 0) ? 1 : 0;
}

/*
 * Enter failsafe/recovery mode via built-in HTTP server
 *
 * Starts the uIP-based httpd on 192.168.1.1:80.
 * The web UI (vendors/air5) allows uploading:
 *   - OpenWrt FIT firmware  -> written to SPI-NOR 0x00400000
 *   - U-Boot (.mbn/.elf)    -> written to APPSBL  0x00200000
 *
 * Flash writes are handled by net/httpd.c do_firmware_upgrade() /
 * do_uboot_upgrade() which have Air5-specific branches guarded by
 * CONFIG_SOFTBANK_AIR5_BOOT.
 */
static void do_failsafe_recovery(void)
{
	printf("\n");
	printf("================================================\n");
	printf("  FAILSAFE RECOVERY MODE\n");
	printf("  Reset button held at boot\n");
	printf("================================================\n");
	printf("  Device IP  : %s\n", FAILSAFE_IPADDR);
	printf("  Open browser: http://%s/\n", FAILSAFE_IPADDR);
	printf("================================================\n\n");

	/* Configure network */
	setenv("ipaddr",   FAILSAFE_IPADDR);
	setenv("netmask",  "255.255.255.0");
	setenv("serverip", "192.168.1.2");

	/* Start built-in HTTP server - blocks until firmware uploaded */
	printf("Starting HTTP recovery server on %s:80 ...\n", FAILSAFE_IPADDR);
	run_command("httpd " FAILSAFE_IPADDR, 0);
}

/*
 * Main OpenWrt boot function
 *
 * Steps:
 *  1. Check reset button -> failsafe if pressed
 *  2. Probe SPI flash
 *  3. Read FIT header to get total image size
 *  4. Read full FIT image
 *  5. Boot via bootm with FIT config auto-selection
 */
static int do_bootopenwrt(cmd_tbl_t *cmdtp, int flag, int argc,
			  char *const argv[])
{
	char runcmd[256];
	char *const bootm_argv[2] = { runcmd, NULL };
	int ret;
	unsigned long load_addr = OPENWRT_LOAD_ADDR;
	unsigned long flash_offset = OPENWRT_KERNEL_FLASH_OFFSET;
	u32 fit_size;
	void *fit_hdr;

	printf("\nOpenWrt Boot - SoftBank Air5\n");
	printf("Flash offset : 0x%08lx\n", flash_offset);
	printf("Load address : 0x%08lx\n", load_addr);
	printf("\n");

	/* Step 1: Check for failsafe/recovery mode */
	printf("Checking reset button... ");
	if (is_reset_button_pressed()) {
		printf("PRESSED\n");
		/* Brief delay to confirm intentional hold */
		mdelay(500);
		if (is_reset_button_pressed()) {
			do_failsafe_recovery();
			/* Should not return, but if it does: */
			return CMD_RET_FAILURE;
		}
	} else {
		printf("not pressed\n");
	}

	/* Step 2: Probe SPI-NOR flash */
	printf("Probing SPI-NOR flash (W25R512NWEIQ 64MB)...\n");
	ret = run_command("sf probe", 0);
	if (ret != CMD_RET_SUCCESS) {
		printf("Error: SPI flash probe failed!\n");
		return CMD_RET_FAILURE;
	}

	/*
	 * Step 3: Read FIT header to determine total image size
	 * Read first 512KB which is enough to contain the FIT header
	 * and image data offsets.
	 */
	printf("Reading FIT header from 0x%08lx...\n", flash_offset);
	snprintf(runcmd, sizeof(runcmd),
		"sf read 0x%lx 0x%lx 0x%x",
		load_addr, flash_offset, FIT_HEADER_READ_SIZE);

	ret = run_command(runcmd, 0);
	if (ret != CMD_RET_SUCCESS) {
		printf("Error: Failed to read FIT header from flash!\n");
		return CMD_RET_FAILURE;
	}

	/* Validate FIT magic */
	fit_hdr = (void *)load_addr;
	if (genimg_get_format(fit_hdr) != IMAGE_FORMAT_FIT) {
		printf("Error: No valid FIT image found at flash offset 0x%08lx\n",
		       flash_offset);
		printf("  Magic: 0x%08x (expected 0x%08x)\n",
		       *(u32 *)fit_hdr, FDT_MAGIC);
		printf("\nIs firmware installed? Use failsafe to flash firmware.\n");
		return CMD_RET_FAILURE;
	}

	/*
	 * Step 4: Read full FIT image
	 * Get the total FIT size from the FDT header, then read it all.
	 * Add some padding for safety.
	 */
	fit_size = fdt_totalsize(fit_hdr);
	if (fit_size == 0 || fit_size > OPENWRT_KERNEL_MAX_SIZE) {
		printf("Warning: FIT size 0x%x seems invalid, using max 0x%x\n",
		       fit_size, OPENWRT_KERNEL_MAX_SIZE);
		fit_size = OPENWRT_KERNEL_MAX_SIZE;
	} else {
		/* Round up to 4KB boundary */
		fit_size = (fit_size + 0xFFF) & ~0xFFF;
		printf("FIT image size: 0x%x (%u KB)\n",
		       fit_size, fit_size / 1024);
	}

	/* Only re-read if we need more than the header read */
	if (fit_size > FIT_HEADER_READ_SIZE) {
		printf("Reading full FIT image (0x%x bytes)...\n", fit_size);
		snprintf(runcmd, sizeof(runcmd),
			"sf read 0x%lx 0x%lx 0x%x",
			load_addr, flash_offset, fit_size);

		ret = run_command(runcmd, 0);
		if (ret != CMD_RET_SUCCESS) {
			printf("Error: Failed to read full FIT image!\n");
			return CMD_RET_FAILURE;
		}
	}

	/* Enable dcache for faster decompression */
	dcache_enable();

	/*
	 * Step 5: Boot the FIT image
	 * Use bootm with the FIT image address.
	 * The FIT image contains kernel + dtb (and optionally initrd).
	 * rootfs is a separate MTD partition loaded by the kernel.
	 */
	printf("Booting OpenWrt FIT image...\n");
	snprintf(runcmd, sizeof(runcmd), "0x%lx", load_addr);

	ret = do_bootm_states(NULL, 0, 1, bootm_argv,
			      BOOTM_STATE_START      |
			      BOOTM_STATE_FINDOS     |
			      BOOTM_STATE_FINDOTHER  |
			      BOOTM_STATE_LOADOS     |
			      BOOTM_STATE_OS_PREP    |
			      BOOTM_STATE_OS_FAKE_GO |
			      BOOTM_STATE_OS_GO,
			      &images, 1);

	/* Should not reach here */
	dcache_disable();
	printf("Error: bootm failed (ret=%d)\n", ret);
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(bootopenwrt, 1, 0, do_bootopenwrt,
	"Boot OpenWrt FIT image from SPI-NOR flash",
	"\n"
	"  Boots OpenWrt FIT image from SPI-NOR flash at offset 0x00400000\n"
	"  Flash layout (W25R512NWEIQ 64MB):\n"
	"    0x00000000 - 0x003FFFFF : Boot chain (SBL/TZ/U-Boot)\n"
	"    0x00400000 - end        : OpenWrt FIT image\n"
	"\n"
	"  Failsafe recovery:\n"
	"    Hold reset button at power-on -> TFTP recovery mode\n"
	"    Device IP: 192.168.1.1, upload firmware via TFTP\n"
);
