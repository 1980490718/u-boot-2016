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
#include <asm/io.h>
#include <console.h>

DECLARE_GLOBAL_DATA_PTR;

/* -----------------------------------------------------------------------
 * GPIO helper - resolve GPIO number from FDT path or environment variable
 * ----------------------------------------------------------------------- */

static int fdt_find_gpio_node(const char *gpio_name) {
	int node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node >= 0)
		return node;
	char path[128];
	snprintf(path, sizeof(path), "/tlmm-gpio/key_gpio/%s", gpio_name);
	node = fdt_path_offset(gd->fdt_blob, path);
	if (node >= 0)
		return node;
	snprintf(path, sizeof(path), "/tlmm-gpio/led_gpio/%s", gpio_name);
	return fdt_path_offset(gd->fdt_blob, path);
}

static int fdt_get_gpio_number(const char *gpio_name) {
	char *endp;
	ulong num = simple_strtoul(gpio_name, &endp, 10);
	if (*endp == '\0' && endp != gpio_name)
		return (int)num;
	char *env_val = getenv(gpio_name);
	if (env_val)
		return simple_strtoul(env_val, NULL, 10);
	int node = fdt_find_gpio_node(gpio_name);
	if (node < 0)
		return -1;
	return fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
}

/* -----------------------------------------------------------------------
 * LED control - toggle/on/off/blink with FDT or env GPIO override
 * ----------------------------------------------------------------------- */

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

static void led_init_from_env(int gpio, const char *name) {
	struct qca_gpio_config cfg = {
		.gpio = gpio, .func = 0, .out = 0,
		.pull = GPIO_NO_PULL, .drvstr = GPIO_8MA,
		.oe = GPIO_OE_ENABLE, .vm = 0, .od_en = 0, .pu_res = 0, .sr_en = 0
	};
	gpio_tlmm_config(&cfg);
	printf("GPIO%d: %s (env)\n", gpio, name);
}

void led_init_by_name(const char *gpio_name) {
	char *env_val = getenv(gpio_name);
	if (env_val) {
		led_init_from_env(simple_strtoul(env_val, NULL, 10), gpio_name);
		return;
	}
	int node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		char path[128];
		snprintf(path, sizeof(path), "/tlmm-gpio/led_gpio/%s", gpio_name);
		node = fdt_path_offset(gd->fdt_blob, path);
	}
	if (node < 0)
		return;
	struct qca_gpio_config gpio_config;
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
	printf("GPIO%d: %s\n", gpio_config.gpio, gpio_name);
}

void led_init(void) {
	int node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/led_gpio");
	if (node >= 0) {
		int subnode;
		fdt_for_each_subnode(gd->fdt_blob, subnode, node) {
			const char *name = fdt_get_name(gd->fdt_blob, subnode, NULL);
			if (name)
				led_init_by_name(name);
		}
	}
	led_on("power_led");
	mdelay(500);
}

/* -----------------------------------------------------------------------
 * Button detection - debounce, init, env override, 3s hold to start httpd
 * Priority: env reset_key > DTS key_gpio subnodes > DTS reset_key fallback
 * ----------------------------------------------------------------------- */

static bool gpio_debounce(int gpio, int value) {
	if (gpio_get_value(gpio) != value)
		return false;
	mdelay(10);
	return gpio_get_value(gpio) == value;
}

bool btn_is_pressed(const char *gpio_name, int value) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0)
		return false;
	return gpio_debounce(gpio, value);
}

/**
 * Configure a GPIO as input with pull-up
 */
static void gpio_input_pullup(int gpio) {
	struct qca_gpio_config cfg = {
		.gpio = gpio, .func = 0, .out = 0,
		.pull = GPIO_PULL_UP, .drvstr = GPIO_8MA,
		.oe = GPIO_OE_DISABLE, .vm = 0, .od_en = 0, .pu_res = 0, .sr_en = 0
	};
	gpio_tlmm_config(&cfg);
}

static void btn_init_gpio(int gpio, const char *name, const char *source) {
	gpio_input_pullup(gpio);
	mdelay(50);
	int value = gpio_get_value(gpio);
	printf("GPIO%d: %s%s\n", gpio, name, value == RESET_BUTTON_PRESSED ? " pressed" : strcmp(source, "env") == 0 ? " (env)" : "");
}

void btn_init_by_name(const char *gpio_name) {
	int gpio = fdt_get_gpio_number(gpio_name);
	if (gpio < 0) {
		return;
	}
	btn_init_gpio(gpio, gpio_name, "fdt");
}

static int env_reset_gpio(void) {
	static int gpio = -2;
	if (gpio == -2) {
		char *val = getenv("reset_key");
		if (val) {
			gpio = simple_strtoul(val, NULL, 10);
			btn_init_gpio(gpio, "reset_key", "env");
		} else {
			gpio = -1;
		}
	}
	return gpio;
}

static bool btn_pressed(char *name, int name_len) {
	int eg = env_reset_gpio();
	if (eg >= 0) {
		if (gpio_debounce(eg, RESET_BUTTON_PRESSED)) {
			strncpy(name, "reset_key", name_len - 1);
			name[name_len - 1] = '\0';
			return true;
		}
		return false;
	}

	int node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/key_gpio");
	if (node < 0) {
		if (btn_is_pressed("reset_key", RESET_BUTTON_PRESSED)) {
			strncpy(name, "reset_key", name_len - 1);
			name[name_len - 1] = '\0';
			return true;
		}
		return false;
	}

	int subnode;
	fdt_for_each_subnode(gd->fdt_blob, subnode, node) {
		const char *subname = fdt_get_name(gd->fdt_blob, subnode, NULL);
		if (!subname)
			continue;
		if (btn_is_pressed(subname, RESET_BUTTON_PRESSED)) {
			strncpy(name, subname, name_len - 1);
			name[name_len - 1] = '\0';
			return true;
		}
	}

	return false;
}

void btn_init(void) {
	if (env_reset_gpio() >= 0)
		return;

	int key_gpio_node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/key_gpio");
	if (key_gpio_node < 0) {
		btn_init_by_name("reset_key");
		return;
	}
	int subnode;
	fdt_for_each_subnode(gd->fdt_blob, subnode, key_gpio_node) {
		const char *subnode_name = fdt_get_name(gd->fdt_blob, subnode, NULL);
		if (subnode_name)
			btn_init_by_name(subnode_name);
	}
}

void btn_check_press(void) {
	char name[64] = {0};
	int counter = 0;
	while (btn_pressed(name, sizeof(name))) {
		if (counter == 0)
			printf("%s button is pressed for:%2d second(s) ", name, counter);
		led_blink_then_on("power_led", 1000);
		counter++;
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s) ", counter);
		if(counter >= 3){
			printf("\n");
			int led_node = fdt_path_offset(gd->fdt_blob, "/tlmm-gpio/led_gpio");
			if (led_node >= 0) {
				int subnode;
				fdt_for_each_subnode(gd->fdt_blob, subnode, led_node)
					led_off(fdt_get_name(gd->fdt_blob, subnode, NULL));
			}
			led_on("power_led");
			led_on("blink_led");
#ifndef CONFIG_IPQ40XX
			eth_initialize();
#endif
			run_command("httpd", 0);
			break;
		}
	}
	if (counter != 0)
		printf("\n");
	return;
}

/* -----------------------------------------------------------------------
 * U-Boot command - gpio: control LED/GPIO via DTS name or env
 * ----------------------------------------------------------------------- */

static void fdt_list_gpio(int node, const char *parent) {
	int subnode;
	fdt_for_each_subnode(gd->fdt_blob, subnode, node) {
		int gpio = fdtdec_get_uint(gd->fdt_blob, subnode, "gpio", -1);
		if (gpio >= 0) {
			int value = gpio_get_value(gpio);
			unsigned int cfg = readl(GPIO_CONFIG_ADDR(gpio));
			printf("GPIO%d %s value=%d  %s/%s\n", gpio,
				(cfg & (1 << 9)) ? "out" : "in", value,
				parent, fdt_get_name(gd->fdt_blob, subnode, NULL));
		}
		fdt_list_gpio(subnode, fdt_get_name(gd->fdt_blob, subnode, NULL));
	}
}

#ifdef CONFIG_IPQ6018
#define GPIO_MAX 80
#elif defined(CONFIG_IPQ40XX) || defined(CONFIG_IPQ807X) || defined(CONFIG_IPQ806X)
#define GPIO_MAX 68
#elif defined(CONFIG_IPQ5332)
#define GPIO_MAX 52
#elif defined(CONFIG_IPQ5018)
#define GPIO_MAX 47
#elif defined(CONFIG_IPQ9574)
#define GPIO_MAX 62
#endif

static void gpio_detect(void) {
	int i, values[GPIO_MAX];
	printf("Detecting GPIO 0-%d input pins...\n", GPIO_MAX - 1);
	for (i = 0; i < GPIO_MAX; i++) {
		unsigned int cfg = readl(GPIO_CONFIG_ADDR(i));
		if ((cfg & 0x1C) || (cfg & (1 << 9)))
			values[i] = -1;
		else
			values[i] = gpio_get_value(i);
	}
	printf("Press and release button(Ctrl+C or 10s exit)\n");
	ulong deadline = get_timer(0) + 10000;
	while (!ctrlc() && get_timer(0) < deadline) {
		for (i = 0; i < GPIO_MAX; i++) {
			if (values[i] < 0)
				continue;
			int v = gpio_get_value(i);
			if (v != values[i]) {
				printf("GPIO%d changed: %d -> %d\n", i, values[i], v);
				values[i] = v;
			}
		}
		udelay(10000);
	}
}

static int do_gpio_cmd(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]) {
	if (argc < 2) {
		int root = fdt_path_offset(gd->fdt_blob, "/");
		if (root >= 0)
			fdt_list_gpio(root, "");
		return CMD_RET_USAGE;
	}
	if (strcmp(argv[1], "d") == 0) {
		gpio_detect();
		return 0;
	}
	int gpio = fdt_get_gpio_number(argv[1]);
	if (gpio < 0) {
		printf("GPIO '%s' not found\n", argv[1]);
		return 1;
	}
	if (argc == 2) {
		int value = gpio_get_value(gpio);
		unsigned int cfg = readl(GPIO_CONFIG_ADDR(gpio));
		printf("GPIO%d %s value=%d\n", gpio, (cfg & (1 << 9)) ? "out" : "in", value);
		return 0;
	}
	const char *op = argv[2];
	struct qca_gpio_config cfg = {
		.gpio = gpio, .func = 0, .out = 1,
		.pull = GPIO_NO_PULL, .drvstr = GPIO_8MA,
		.oe = GPIO_OE_ENABLE, .vm = 0, .od_en = 0, .pu_res = 0, .sr_en = 0
	};
	gpio_tlmm_config(&cfg);
	if (strcmp(op, "on") == 0)
		gpio_set_value(gpio, 1);
	else if (strcmp(op, "off") == 0)
		gpio_set_value(gpio, 0);
	else if (strcmp(op, "t") == 0)
		gpio_set_value(gpio, !gpio_get_value(gpio));
	else if (strcmp(op, "b") == 0) {
		if (argc < 4)
			return CMD_RET_USAGE;
		led_blink(argv[1], simple_strtoul(argv[3], NULL, 10) * 1000);
	} else
		return CMD_RET_USAGE;
	return 0;
}

U_BOOT_CMD(gpio, 4, 0, do_gpio_cmd,
	"control LED/GPIO via FDT name or env",
	"<name|num> - show GPIO status\n"
	"gpio <name> on|off|t(oggle)|b(link) <sec>\n"
	"gpio d - detect button\n"
);

/* -----------------------------------------------------------------------
 * Firmware detection - identify firmware type and HLOS size by magic number
 * ----------------------------------------------------------------------- */

struct fw_info check_fw_type_ex(void *address) {
	typedef uint32_t u32;
	typedef uint16_t u16;

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

/* -----------------------------------------------------------------------
 * Partition - SMEM/GPT partition size and offset queries for webfailsafe
 * ----------------------------------------------------------------------- */

u64 get_smem_table_size_bytes(const char *name) {
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
	if (ret == 0 && (flash_type == SMEM_BOOT_MMC_FLASH ||
			qca_smem_flash_info.rootfs.offset == 0xBAD0FF5E)) {
		block_dev_desc_t *mmc_dev;
		disk_partition_t disk_info;
		/* Get the MMC device */
		mmc_dev = mmc_get_dev(0); // Use first MMC device
		if (mmc_dev != NULL && mmc_dev->type != DEV_TYPE_UNKNOWN) {
			/* Try to get partition info from GPT by name */
			ret = get_partition_info_efi_by_name(mmc_dev, name, &disk_info);
			if (ret == 0) {
				return (u64)disk_info.size * (u64)mmc_dev->blksz;
			}
		}
	}
#endif
	/* If all methods failed, return 0 */
	return 0;
}

/* define macro for get size function */
#define DEFINE_GET_SIZE_FUNC(func_name, partition_name) \
	u64 func_name(void) { \
		return get_smem_table_size_bytes(partition_name); \
	}

/* api for webfailsafe upgrade size limit */
DEFINE_GET_SIZE_FUNC(get_uboot_size, "0:APPSBL")
DEFINE_GET_SIZE_FUNC(get_art_size, "0:ART")
DEFINE_GET_SIZE_FUNC(get_cdt_size, "0:CDT")
DEFINE_GET_SIZE_FUNC(get_mibib_size, "0:MIBIB")
DEFINE_GET_SIZE_FUNC(get_bootconfig_size, "0:BOOTCONFIG")

u64 get_firmware_upgrade_max_size(void) {
	uint32_t flash_type;
	if (get_current_flash_type(&flash_type) != 0)
		return 0;
	switch (flash_type) {
#if defined(CONFIG_EFI_PARTITION) && defined(CONFIG_PARTITIONS) && defined(CONFIG_CMD_MMC)
		case SMEM_BOOT_MMC_FLASH:
		case SMEM_BOOT_NORPLUSEMMC:
			return get_hlos_size() + get_rootfs_size();
#endif
		case SMEM_BOOT_NOR_FLASH:
			return get_nor_firmware_combined_size();
		case SMEM_BOOT_SPI_FLASH:
			if (get_which_flash_param("rootfs") > 0)
				return get_firmware_size();
			else
				return get_nor_firmware_combined_size();
		case SMEM_BOOT_NAND_FLASH:
		case SMEM_BOOT_QSPI_NAND_FLASH:
		case SMEM_BOOT_NORPLUSNAND:
		default:
			return get_firmware_size();
	}
}

/* Function to get firmware size supporting multiple rootfs partition names */
u64 get_firmware_size(void) {
	u64 size;
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
u64 get_smem_table_offset(const char *name) {
	uint32_t offset, byte_size;
	if (getpart_offset_size((char *)name, &offset, &byte_size) == 0) {
		return (unsigned long)offset;
	}
	return 0;
}

/* Get the combined size of the NOR firmware (kernel + rootfs) */
u64 get_nor_firmware_combined_size(void) {
	u64 kernel_size = get_smem_table_size_bytes("0:HLOS");
	u64 rootfs_size = get_smem_table_size_bytes("rootfs");
	return kernel_size + rootfs_size;
}

/* define macro for get offset function */
#define DEFINE_GET_OFFSET_FUNC(func_name, partition_name) \
	u64 func_name(void) { \
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

u64 get_bootconfig_offset_blocks(void) { return get_bootconfig_offset() / 512; }
u64 get_bootconfig_size_blocks(void)   { return get_bootconfig_size() / 512; }

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

/* -----------------------------------------------------------------------
 * Network - PHY link status monitoring and auto re-initialization
 * ----------------------------------------------------------------------- */

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