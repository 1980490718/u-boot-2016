#include <common.h>
#include <ipq_api.h>
#include <asm/gpio.h>
#include <fdtdec.h>
#include <net.h>
#include <stdbool.h>
#include <asm-generic/global_data.h>
#include <asm/arch-qca-common/smem.h>

DECLARE_GLOBAL_DATA_PTR;

static int fdt_get_gpio_number(const char *gpio_name) {
	int node;
	unsigned int gpio;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node in fdt\n", gpio_name);
		return -1;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	if (gpio < 0) {
		printf("Could not find %s node's gpio in fdt\n", gpio_name);
		return -1;
	}
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

void check_button_is_press(void) {
	int counter = 0;
	while (button_is_press("reset_key", RESET_BUTTON_PRESSED)) {
		if(counter == 0)
			printf("Reset button is pressed for: %2d second(s) ", counter);
		led_blink_then_on("power_led", 1000);
		counter++;
		printf("\b\b\b\b\b\b\b\b\b\b\b\b\b%2d second(s) ", counter);
		if(counter >= 3){
			printf("\n");
#if defined(CONFIG_IPQ807X_AP8220)
			led_on("power_led");
			led_off("wlan2g_led");
			led_off("wlan5g_led");
			led_off("bluetooth_led");
#elif defined(CONFIG_IPQ807X_AX6)
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
			eth_initialize();
			run_command("httpd 192.168.1.1", 0);
			run_command("res", 0);
			break;
		}
	}
	if (counter != 0)
		printf("\n");
	return;
}

int check_fw_type(void *address) {
	typedef unsigned int u32;
	typedef unsigned short u16;
	u32 *sign_flas=(u32 *)(address+0x5c);
	u16 *sign_55aa=(u16 *)(address+0x1fe);
	u32 *sign_doodfeed=(u32 *)address;
	u32 *sign_ubi=(u32 *)address;
	u32 *sign_cdt=(u32 *)address;
	u32 *sign_elf=(u32 *)address;
	u32 *sign_mibib=(u32 *)address;

	if (*sign_flas==0x73616c46) return FW_TYPE_QSDK;
	else if (*sign_ubi==0x23494255) return FW_TYPE_UBI;
	else if (*sign_doodfeed==0xedfe0dd0) return FW_TYPE_NOR;
	else if (*sign_55aa==0xaa55) return FW_TYPE_GPT;
	else if (*sign_cdt==0x00544443) return FW_TYPE_CDT;
	else if (*sign_elf==0x464c457f) return FW_TYPE_ELF;
	else if (*sign_mibib==0xfe569fac) return FW_TYPE_MIBIB;
	else return -1;
	return 0;
}

/* Get the size information from the smem table (in bytes) */
unsigned long get_smem_table_size_bytes(const char *name) {
	uint32_t offset, byte_size;
	if (getpart_offset_size((char *)name, &offset, &byte_size) == 0) {
		return (unsigned long)byte_size;
	}
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

void led_init_by_name(const char *gpio_name)
{
	int node;
	struct qca_gpio_config gpio_config;

	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node\n", gpio_name);
		return;
	}

	gpio_config.gpio	= fdtdec_get_uint(gd->fdt_blob,
						  node, "gpio", 0);
	gpio_config.func	= fdtdec_get_uint(gd->fdt_blob,
						  node, "func", 0);
	gpio_config.out		= fdtdec_get_uint(gd->fdt_blob,
						  node, "out", 0);
	gpio_config.pull	= fdtdec_get_uint(gd->fdt_blob,
						  node, "pull", 0);
	gpio_config.drvstr	= fdtdec_get_uint(gd->fdt_blob,
						  node, "drvstr", 0);
	gpio_config.oe		= fdtdec_get_uint(gd->fdt_blob,
						  node, "oe", 0);
	gpio_config.vm		= fdtdec_get_uint(gd->fdt_blob,
						  node, "vm", 0);
	gpio_config.od_en	= fdtdec_get_uint(gd->fdt_blob,
						  node, "od_en", 0);
	gpio_config.pu_res	= fdtdec_get_uint(gd->fdt_blob,
						  node, "pu_res", 0);

	gpio_tlmm_config(&gpio_config);
}

void led_init(void)
{
	led_init_by_name("power_led");
	led_init_by_name("blink_led");
	led_init_by_name("system_led");
#if defined(CONFIG_IPQ807X_AP8220)
	led_init_by_name("wlan2g_led");
	led_init_by_name("wlan5g_led");
	led_init_by_name("bluetooth_led");
#endif
#if defined(CONFIG_IPQ807X_AX6)
	led_init_by_name("network_blue_led");
	led_init_by_name("aiot_led");
#endif
#if defined(CONFIG_IPQ807X_XGLINK_5GCPE)
	led_init_by_name("led_system_power2");
#endif

	led_on("power_led");
	mdelay(500);
}

void btn_init(void)
{
	led_init_by_name("reset_key");
}