#include <common.h>
#include <ipq_api.h>
#include <asm/gpio.h>
#include <fdtdec.h>
#include <net.h>
#include <stdbool.h>
#include <asm-generic/global_data.h>
#include <asm/arch-qca-common/smem.h>
#include <part.h>
#include <mmc.h>
#include <miiphy.h>
#include <linux/mii.h>
#if defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ6018) || defined(CONFIG_IPQ9574)
#include "../drivers/net/ipq_common/ipq_phy.h"
#endif
#ifdef CONFIG_IPQ40XX
#include <dt-bindings/qcom/gpio-ipq40xx.h>
#endif
#include <asm/arch-qca-common/gpio.h>

DECLARE_GLOBAL_DATA_PTR;

static int fdt_get_gpio_number(const char *gpio_name) {
	int node;
	unsigned int gpio;
	char *env_val;
	/* First check if gpio_name exists as a direct path */
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node >= 0) {
		gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
		return gpio;
	}
	/* If not found as direct path, also check under /tlmm-gpio/key_gpio path */
	char full_path[128];
	snprintf(full_path, sizeof(full_path), "/tlmm-gpio/key_gpio/%s", gpio_name);
	node = fdt_path_offset(gd->fdt_blob, full_path);
	if (node < 0) {
		/* Check environment variable for GPIO number */
		env_val = getenv(gpio_name);
		if (env_val) {
			gpio = simple_strtoul(env_val, NULL, 10);
			return gpio;
		}
		printf("Could not find %s node in fdt and no env variable set\n", gpio_name);
		return -1;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	return gpio;
}

void led_toggle(const char *gpio_name) {
	int value;
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return;
	}
	value = gpio_get_value(gpio);
	value = !value;
	gpio_set_value(gpio, value);
}

void led_on(const char *gpio_name) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return;
	}
	gpio_set_value(gpio, 1);
}

void led_off(const char *gpio_name) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return;
	}
	gpio_set_value(gpio, 0);
}

void led_blink(const char *gpio_name, int duration) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return;
	}
	/* Number of cycles = duration(ms) / (on time(500ms) + off time(500ms)) */
	int cycles = duration / 1000;
	int i;
	for (i = 0; i < cycles; i++) {
		/* LED on */
		gpio_set_value(gpio, 1);
		mdelay(500);

		/* LED off */
		gpio_set_value(gpio, 0);
		mdelay(500);
	}
}

void led_blink_then_on(const char *gpio_name, int duration) {
	led_blink(gpio_name, duration);
	led_on(gpio_name);
}

void led_blink_then_off(const char *gpio_name, int duration) {
	led_blink(gpio_name, duration);
	led_off(gpio_name);
}

bool button_is_press(const char *gpio_name, int value) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return false;
	}
	if(gpio_get_value(gpio) == value) {
		mdelay(10);
		if(gpio_get_value(gpio) == value)
			return true;
		else
			return false;
	}
	else
		return false;
}

/**
 * Configure a GPIO as input with pull-up
 */
static void config_gpio_as_input_with_pullup(int gpio) {
	struct qca_gpio_config gpio_config;
	gpio_config.gpio = gpio;
	gpio_config.func = 0;
	gpio_config.out = 0;
	gpio_config.pull = GPIO_PULL_UP;
	gpio_config.drvstr = GPIO_8MA;
	gpio_config.oe = GPIO_OE_DISABLE;
	gpio_config.vm = 0;
	gpio_config.od_en = 0;
	gpio_config.pu_res = 0;
	gpio_config.sr_en = 0;
	gpio_tlmm_config(&gpio_config);
}

/**
 * Check reset button status using either DTS node or environment variable
 * If button is pressed for 3 seconds, start httpd server
 */
static void check_reset_button_status(void) {
	int gpio = -1;
	int counter = 0;
	char *env_val;
	/* First check environment variable, override DTS settings */
	env_val = getenv("reset_key");
	if (env_val) {
		gpio = simple_strtoul(env_val, NULL, 10);
		/* Environment variable takes precedence, use it directly */
	} else {
		/* If not in environment, check if reset_key is defined in DTS */
		int node = fdt_path_offset(gd->fdt_blob, "reset_key");
		if (node >= 0) {
			/* Reset key is defined in DTS, use existing function */
			return;
		} else {
			/* No reset key defined anywhere, return */
			return;
		}
	}
	/* Configure GPIO as input with pull-up */
	config_gpio_as_input_with_pullup(gpio);
	/* Check if reset button is pressed */
	while (gpio_get_value(gpio) == 0) { /* 0 means pressed (active low) */
		if (counter == 0) {
			printf("Reset button is pressed for: %2d second(s) ", counter);
		}
		/* Blink power LED if available */
		led_blink_then_on("power_led", 1000);
		counter++;
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s) ", counter);
		if (counter >= 3) {
			printf("\n");
			/* Start httpd server */
			led_off("power_led");
			led_on("blink_led");
			run_command("httpd", 0);
			break;
		}
	}
	if (counter != 0) {
		printf("\n");
	}
	return;
}

static bool is_any_button_pressed(char *pressed_button_name, int max_name_len) {
	int key_gpio_node;
	int subnode;

	/* Find the key_gpio node */
	key_gpio_node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/key_gpio");
	if (key_gpio_node < 0) {
		/* Fallback to checking reset_key directly if key_gpio path doesn't exist */
		if (button_is_press("reset_key", RESET_BUTTON_PRESSED)) {
			strncpy(pressed_button_name, "reset_key", max_name_len-1);
			pressed_button_name[max_name_len-1] = '\0';
			return true;
		}
		return false;
	}

	/* Iterate through all subnodes under key_gpio */
	fdt_for_each_subnode(gd->fdt_blob, subnode, key_gpio_node) {
		const char *subnode_name = fdt_get_name(gd->fdt_blob, subnode, NULL);
		if (subnode_name && button_is_press(subnode_name, RESET_BUTTON_PRESSED)) {
			strncpy(pressed_button_name, subnode_name, max_name_len-1);
			pressed_button_name[max_name_len-1] = '\0';
			return true;
		}
	}

	return false;
}

void check_button_is_press(void) {
	int counter = 0;
	char pressed_button_name[64] = {0};
	while (is_any_button_pressed(pressed_button_name, sizeof(pressed_button_name))) {
		if(counter == 0)
			printf("%s button is pressed for:%2d second(s) ", pressed_button_name, counter);
		led_blink_then_on("power_led", 1000);
		counter++;
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s) ", counter);
		if(counter >= 3){
			printf("\n");
#if defined(CONFIG_IPQ807X_ALIYUN_AP8220)
			led_on("power_led");
			led_off("wlan2g_led");
			led_off("wlan5g_led");
			led_off("bluetooth_led");
#elif defined(CONFIG_IPQ807X_REDMI_AX6)
			led_off("system_led");
			led_on("power_led");
			led_on("blink_led");
			led_off("network_blue_led");
#elif defined(CONFIG_IPQ6018_M2)
			led_on("system_led");
			led_on("wlan2g_led");
			led_on("wlan5g_led");
			led_on("mesh_led");
			led_on("bluetooth_led");
#elif defined(CONFIG_IPQ807X_XGLINK_5GCPE)
			led_on("led_system_power2");
#else
			led_off("power_led");
			led_on("blink_led");
#endif
#ifndef CONFIG_IPQ40XX
			eth_initialize();
#endif
			run_command("httpd", 0);
			break;
		}
	}
	if (counter != 0)
		printf("\n");
	/* Check reset button status using environment variable if not defined in fdt */
	check_reset_button_status();
	return;
}

struct fw_info check_fw_type_ex(void *address) {
	typedef unsigned int u32;
	typedef unsigned short u16;

	struct fw_info info = {
		.type = -1,
		.hlos_size = 12 * 1024 * 1024  // Default 12MB
	};

	u32 *ptr_flas		= (u32 *)((uintptr_t)address + 0x5c);
	u16 *ptr_55aa		= (u16 *)((uintptr_t)address + 0x1fe);
	u32 *ptr_doodfeed	= (u32 *)address;
	u32 *ptr_ubi		= (u32 *)address;
	u32 *ptr_cdt		= (u32 *)address;
	u32 *ptr_elf		= (u32 *)address;
	u32 *ptr_mibib		= (u32 *)address;

	// Detect HLOS size magic number positions
	u32 *ptr_hlos_4m	= (u32 *)((uintptr_t)address + 0x400000);
	u32 *ptr_hlos_6m	= (u32 *)((uintptr_t)address + 0x600000);
	u32 *ptr_hlos_8m	= (u32 *)((uintptr_t)address + 0x800000);
	u32 *ptr_hlos_12m	= (u32 *)((uintptr_t)address + 0xC00000);
	u32 *ptr_hlos_14m	= (u32 *)((uintptr_t)address + 0xE00000);
	u32 *ptr_hlos_16m	= (u32 *)((uintptr_t)address + 0x1000000);

	const u32 MAGIC_HSQS = 0x73717368;  // "hsqs"

	// Detect actual HLOS size in ascending order
	if (*ptr_hlos_4m == MAGIC_HSQS) {
		info.hlos_size = 4 * 1024 * 1024;
	}
	else if (*ptr_hlos_6m == MAGIC_HSQS) {
		info.hlos_size = 6 * 1024 * 1024;
	}
	else if (*ptr_hlos_8m == MAGIC_HSQS) {
		info.hlos_size = 8 * 1024 * 1024;
	}
	else if (*ptr_hlos_12m == MAGIC_HSQS) {
		info.hlos_size = 12 * 1024 * 1024;
	}
	else if (*ptr_hlos_14m == MAGIC_HSQS) {
		info.hlos_size = 14 * 1024 * 1024;
	}
	else if (*ptr_hlos_16m == MAGIC_HSQS) {
		info.hlos_size = 16 * 1024 * 1024;
	}

	// Detect firmware type
	if (*ptr_flas == 0x73616c46) info.type			= FW_TYPE_QSDK;
	else if (*ptr_ubi == 0x23494255) info.type		= FW_TYPE_UBI;
	else if (*ptr_doodfeed == 0xedfe0dd0) info.type	= FW_TYPE_FIT;
	else if (*ptr_55aa == 0xaa55) info.type			= FW_TYPE_GPT;
	else if (*ptr_cdt == 0x00544443) info.type		= FW_TYPE_CDT;
	else if (*ptr_elf == 0x464c457f) info.type		= FW_TYPE_ELF;
	else if (*ptr_mibib == 0xfe569fac) info.type	= FW_TYPE_MIBIB;
	else info.type = -1;

	return info;
}

int check_fw_type(void *address) {
	struct fw_info info = check_fw_type_ex(address);
	return info.type;
}

/* Get the size information from the smem table (in bytes) */
unsigned long get_smem_table_size_bytes(const char *name) {
	uint32_t offset, byte_size;
	int ret;
	/* First, try the traditional SMEM methods */
	ret = getpart_offset_size((char *)name, &offset, &byte_size);
	if (ret == 0 && byte_size != 0) {
		return (unsigned long)byte_size;
	}
#if defined(CONFIG_EFI_PARTITION) && defined(CONFIG_PARTITIONS) && defined(CONFIG_CMD_MMC)
	/* If SMEM methods fail, try getting flash type to determine if it's EMMC */
	uint32_t flash_type, flash_index, flash_chip_select, flash_block_size, flash_density;
	ret = smem_get_boot_flash(&flash_type, &flash_index, &flash_chip_select, &flash_block_size, &flash_density);
	/* If it's an EMMC device, try getting partition info from GPT */
	if (ret == 0 && flash_type == SMEM_BOOT_MMC_FLASH) {
		block_dev_desc_t *mmc_dev;
		disk_partition_t disk_info;
		/* Get the MMC device */
		mmc_dev = mmc_get_dev(0); // Use first MMC device
		if (mmc_dev != NULL && mmc_dev->type != DEV_TYPE_UNKNOWN) {
			/* Try to get partition info from GPT by name */
			ret = get_partition_info_efi_by_name(mmc_dev, name, &disk_info);
			if (ret == 0) {
				/* Successfully got partition info from GPT, return size in bytes */
				return (unsigned long)disk_info.size * (unsigned long)mmc_dev->blksz;
			}
		}
	}
#endif
	/* If all methods failed, return 0 */
	return 0;
}

/* define macro for get size function */
#define DEFINE_GET_SIZE_FUNC(func_name, partition_name) \
	unsigned long func_name(void) { \
		return get_smem_table_size_bytes(partition_name); \
	}

/* api for webfailsafe upgrade size limit */
DEFINE_GET_SIZE_FUNC(get_uboot_size, "0:APPSBL")
DEFINE_GET_SIZE_FUNC(get_art_size, "0:ART")
DEFINE_GET_SIZE_FUNC(get_cdt_size, "0:CDT")
DEFINE_GET_SIZE_FUNC(get_mibib_size, "0:MIBIB")
DEFINE_GET_SIZE_FUNC(get_bootconfig_size, "0:BOOTCONFIG")

unsigned long get_firmware_upgrade_max_size(void) {
	switch (qca_smem_flash_info.flash_type) {
#if defined(CONFIG_EFI_PARTITION) && defined(CONFIG_PARTITIONS) && defined(CONFIG_CMD_MMC)
		case FLASH_TYPE_MMC:
		case FLASH_TYPE_NOR_PLUS_EMMC:
			return get_hlos_size() + get_rootfs_size();
#endif
		case FLASH_TYPE_NOR:
			return get_nor_firmware_combined_size();
		case FLASH_TYPE_SPI:
			if (get_which_flash_param("rootfs"))
				return get_firmware_size();
			else
				return get_nor_firmware_combined_size();
		case FLASH_TYPE_NAND:
		case FLASH_TYPE_QSPI_NAND:
		case FLASH_TYPE_NOR_PLUS_NAND:
		default:
			return get_firmware_size();
	}
}

/* Function to get firmware size supporting multiple rootfs partition names */
unsigned long get_firmware_size(void) {
	unsigned long size;
	/* Try different possible rootfs partition names in priority order */
	/* Priority order: rootfs -> rootfs1 -> rootfs2 -> rootfs_1 */
	size = get_smem_table_size_bytes("rootfs");
	if (size > 0)
		return size;
	size = get_smem_table_size_bytes("rootfs1");
	if (size > 0)
		return size;
	size = get_smem_table_size_bytes("rootfs2");
	if (size > 0)
		return size;
	size = get_smem_table_size_bytes("rootfs_1");
	if (size > 0)
		return size;
	/* If none found, return size of default rootfs */
	return get_smem_table_size_bytes("rootfs");
}

/* Get the offset start information from the smem table */
unsigned long get_smem_table_offset(const char *name) {
	uint32_t offset, byte_size;
	if (getpart_offset_size((char *)name, &offset, &byte_size) == 0) {
		return (unsigned long)offset;
	}
	return 0;
}

/* Get the combined size of the NOR firmware (kernel + rootfs) */
unsigned long get_nor_firmware_combined_size(void) {
	unsigned long kernel_size = get_smem_table_size_bytes("0:HLOS");
	unsigned long rootfs_size = get_smem_table_size_bytes("rootfs");
	return kernel_size + rootfs_size;
}

/* define macro for get offset function */
#define DEFINE_GET_OFFSET_FUNC(func_name, partition_name) \
	unsigned long func_name(void) { \
		return get_smem_table_offset(partition_name); \
	}

/* api for partition offset start */
DEFINE_GET_OFFSET_FUNC(get_hlos_offset, "0:HLOS")
DEFINE_GET_OFFSET_FUNC(get_hlos_1_offset, "0:HLOS_1")
DEFINE_GET_OFFSET_FUNC(get_rootfs_offset, "rootfs")
DEFINE_GET_OFFSET_FUNC(get_rootfs_1_offset, "rootfs_1")
DEFINE_GET_SIZE_FUNC(get_hlos_size, "0:HLOS")
DEFINE_GET_SIZE_FUNC(get_hlos_1_size, "0:HLOS_1")
DEFINE_GET_SIZE_FUNC(get_rootfs_size, "rootfs")
DEFINE_GET_SIZE_FUNC(get_rootfs_1_size, "rootfs_1")
DEFINE_GET_OFFSET_FUNC(get_bootconfig_offset, "0:BOOTCONFIG")
DEFINE_GET_OFFSET_FUNC(get_bootconfig1_offset, "0:BOOTCONFIG1")
#if defined(CONFIG_EFI_PARTITION) && defined(CONFIG_PARTITIONS) && defined(CONFIG_CMD_MMC)
/* Get the firmware type from the smem table */
#define PART_SIZE_BYTES(part)     get_smem_table_size_bytes(part)
#define PART_START_BLOCK(part)    get_smem_table_offset(part)
#define PART_END_BLOCK(part)      (PART_START_BLOCK(part) + (PART_SIZE_BYTES(part) / 512) - 1)

/* Calculate how to distribute firmware between HLOS and rootfs partitions */
int emmc_calculate_firmware_distribution(unsigned long firmware_size,
		unsigned long hlos_max_size, unsigned long rootfs_max_size,
		unsigned long *hlos_part_size, unsigned long *rootfs_part_size) {
	*hlos_part_size = (firmware_size < hlos_max_size) ? firmware_size : hlos_max_size;
	unsigned long remaining = (firmware_size > *hlos_part_size) ? (firmware_size - *hlos_part_size) : 0;
	*rootfs_part_size = (remaining < rootfs_max_size) ? remaining : rootfs_max_size;
	return 0;
}

unsigned long get_bootconfig_offset_blocks(void) { return get_bootconfig_offset() / 512; }
unsigned long get_bootconfig_size_blocks(void)   { return get_bootconfig_size() / 512; }

unsigned long get_hlos_size_bytes(void)     { return PART_SIZE_BYTES("0:HLOS"); }
unsigned long get_hlos_start_block(void)    { return PART_START_BLOCK("0:HLOS"); }
unsigned long get_hlos_end_block(void)      { return PART_END_BLOCK("0:HLOS"); }

unsigned long get_hlos_1_size_bytes(void)   { return PART_SIZE_BYTES("0:HLOS_1"); }
unsigned long get_hlos_1_start_block(void)  { return PART_START_BLOCK("0:HLOS_1"); }
unsigned long get_hlos_1_end_block(void)    { return PART_END_BLOCK("0:HLOS_1"); }

unsigned long get_rootfs_size_bytes(void)   { return PART_SIZE_BYTES("rootfs"); }
unsigned long get_rootfs_start_block(void)  { return PART_START_BLOCK("rootfs"); }
unsigned long get_rootfs_end_block(void)    { return PART_END_BLOCK("rootfs"); }

unsigned long get_rootfs_1_size_bytes(void) { return PART_SIZE_BYTES("rootfs_1"); }
unsigned long get_rootfs_1_start_block(void){ return PART_START_BLOCK("rootfs_1"); }
unsigned long get_rootfs_1_end_block(void)  { return PART_END_BLOCK("rootfs_1"); }
#endif /* CONFIG_EFI_PARTITION && CONFIG_PARTITIONS && CONFIG_CMD_MMC */

void led_init_by_name(const char *gpio_name) {
	int node;
	struct qca_gpio_config gpio_config;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node\n", gpio_name);
		return;
	}
	gpio_config.gpio	= fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	gpio_config.func	= fdtdec_get_uint(gd->fdt_blob, node, "func", 0);
	gpio_config.out		= fdtdec_get_uint(gd->fdt_blob, node, "out", 0);
	gpio_config.pull	= fdtdec_get_uint(gd->fdt_blob, node, "pull", 0);
	gpio_config.drvstr	= fdtdec_get_uint(gd->fdt_blob, node, "drvstr", 0);
	gpio_config.oe		= fdtdec_get_uint(gd->fdt_blob, node, "oe", 0);
	gpio_config.vm		= fdtdec_get_uint(gd->fdt_blob, node, "vm", 0);
	gpio_config.od_en	= fdtdec_get_uint(gd->fdt_blob, node, "od_en", 0);
	gpio_config.pu_res	= fdtdec_get_uint(gd->fdt_blob, node, "pu_res", 0);
	gpio_tlmm_config(&gpio_config);
}

void led_init(void) {
	led_init_by_name("power_led");
	led_init_by_name("blink_led");
	led_init_by_name("system_led");
#if defined(CONFIG_IPQ807X_ALIYUN_AP8220)
	led_init_by_name("wlan2g_led");
	led_init_by_name("wlan5g_led");
	led_init_by_name("bluetooth_led");
#endif
#if defined(CONFIG_IPQ807X_REDMI_AX6)
	led_init_by_name("network_blue_led");
	led_init_by_name("aiot_led");
#endif
#if defined(CONFIG_IPQ807X_XGLINK_5GCPE)
	led_init_by_name("led_system_power2");
#endif
	led_on("power_led");
	mdelay(500);
}

void btn_init_by_name(const char *gpio_name) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		printf("Could not find %s in FDT or environment\n", gpio_name);
		return;
	}
	config_gpio_as_input_with_pullup(gpio);
	mdelay(50);
	int value = gpio_get_value(gpio);
	printf("GPIO %d:%s initial value: %d\n", gpio, gpio_name, value);
}

void btn_init(void) {
	int key_gpio_node;
	int subnode;
	key_gpio_node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/key_gpio");
	if (key_gpio_node < 0) {
		btn_init_by_name("reset_key");
		return;
	}
	fdt_for_each_subnode(gd->fdt_blob, subnode, key_gpio_node) {
		const char *subnode_name = fdt_get_name(gd->fdt_blob, subnode, NULL);
		if (subnode_name) {
			btn_init_by_name(subnode_name);
		}
	}
}

#define LINK_CHECK_INTERVAL	100
#define PHY_SPEC_STATUS		17
#define PHY_LINK_PASS		0x0400

static ulong link_last_check;
static int link_first_check;
static int link_port_prev;

#if defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ6018) || defined(CONFIG_IPQ9574)
extern phy_info_t *phy_info[];

#if defined(CONFIG_IPQ807X)
#define PHY_PORT_MAX	PHY_MAX
#elif defined(CONFIG_IPQ6018)
#define PHY_PORT_MAX	IPQ6018_PHY_MAX
#elif defined(CONFIG_IPQ9574)
#define PHY_PORT_MAX	IPQ9574_PHY_MAX
#endif
#endif

static int read_phy_link(const char *devname, int phy_addr) {
	ushort val;

	if (miiphy_read(devname, phy_addr, PHY_SPEC_STATUS, &val) == 0) {
		if (val != 0xFFFF && val != 0x0000)
			return (val & PHY_LINK_PASS) ? 1 : 0;
	}

	/* BMSR LSTATUS is latched-low: 1st read clears, 2nd reads real-time */
	if (miiphy_read(devname, phy_addr, MII_BMSR, &val) != 0)
		return 0;
	if (miiphy_read(devname, phy_addr, MII_BMSR, &val) != 0)
		return 0;
	if (val == 0xFFFF || val == 0x0000)
		return 0;
	return (val & BMSR_LSTATUS) ? 1 : 0;
}

int eth_check_link_change(void) {
	ulong now = get_timer(0);
	const char *devname;
	int link_port = -1;
	int i;

	if ((now - link_last_check) < LINK_CHECK_INTERVAL)
		return 0;
	link_last_check = now;

	devname = miiphy_get_current_dev();
	if (!devname)
		return 0;

#if defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ6018) || defined(CONFIG_IPQ9574)
	for (i = 0; i < PHY_PORT_MAX; i++) {
		if (!phy_info[i] || phy_info[i]->phy_type == UNUSED_PHY_TYPE || phy_info[i]->phy_type == SFP_PHY_TYPE)
			continue;
		if (read_phy_link(devname, phy_info[i]->phy_address)) {
			link_port = i;
			break;
		}
	}
#elif defined(CONFIG_IPQ5018)
	static const char * const mdio_bus[] = {"IPQ MDIO0", "IPQ MDIO1"};
	int b;

	for (b = 0; b < (int)ARRAY_SIZE(mdio_bus) && link_port < 0; b++) {
		for (i = 0; i < PHY_MAX_ADDR; i++) {
			if (read_phy_link(mdio_bus[b], i)) {
				link_port = b * PHY_MAX_ADDR + i;
				break;
			}
		}
	}
#else
	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if (read_phy_link(devname, i)) {
			link_port = i;
			break;
		}
	}
#endif

	if (!link_first_check) {
		link_port_prev = link_port;
		link_first_check = 1;
		return 0;
	}

	if (link_port == link_port_prev)
		return 0;

	link_port_prev = link_port;

	if (link_port < 0)
		return 0;

	eth_init();
	return 1;
}