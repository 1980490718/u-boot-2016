#include <common.h>
#include <ipq_api.h>
#include <asm/gpio.h>
#include <fdtdec.h>
#include <net.h>

DECLARE_GLOBAL_DATA_PTR;

void led_toggle(const char *gpio_name)
{
	int node, value;
	unsigned int gpio;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node in fdt\n", gpio_name);
		return;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	if (gpio < 0) {
		printf("Could not find %s node's gpio in fdt\n", gpio_name);
		return;
	}

	value = gpio_get_value(gpio);
	value = !value;
	gpio_set_value(gpio, value);
}

void led_on(const char *gpio_name)
{
	int node;
	unsigned int gpio;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node in fdt\n", gpio_name);
		return;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	if (gpio < 0) {
		printf("Could not find %s node's gpio in fdt\n", gpio_name);
		return;
	}

	gpio_set_value(gpio, 1);
}

void led_off(const char *gpio_name)
{
	int node;
	unsigned int gpio;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node in fdt\n", gpio_name);
		return;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	if (gpio < 0) {
		printf("Could not find %s node's gpio in fdt\n", gpio_name);
		return;
	}

	gpio_set_value(gpio, 0);
}

bool button_is_press(const char *gpio_name, int value)
{
	int node;
	unsigned int gpio;
	node = fdt_path_offset(gd->fdt_blob, gpio_name);
	if (node < 0) {
		printf("Could not find %s node in fdt\n", gpio_name);
		return false;
	}
	gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", 0);
	if (gpio < 0) {
		printf("Could not find %s node's gpio in fdt\n", gpio_name);
		return false;
	}

	if(gpio_get_value(gpio) == value)
	{
		mdelay(10);
		if(gpio_get_value(gpio) == value)
			return true;
		else
			return false;
	}
	else
		return false;
}

void check_button_is_press(void)
{
	int counter = 0;

	while (button_is_press("reset_key", RESET_BUTTON_PRESSED))
	{

		if(counter == 0)
			printf("Reset button is pressed for: %2d second(s) ", counter);

		led_off("power_led");
		mdelay(500);
		led_on("power_led");
		mdelay(500);

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

int check_fw_type(void *address){
	u32 *sign_flas=(u32 *)(address+0x5c);
	u16 *sign_55aa=(u16 *)(address+0x1fe);
	u32 *sign_doodfeed=(u32 *)address;
	u32 *sign_ubi=(u32 *)address;
	u32 *sign_cdt=(u32 *)address;
	u32 *sign_elf=(u32 *)address;
	u32 *sign_mibib=(u32 *)address;

	if (*sign_flas==0x73616c46)
		return FW_TYPE_QSDK;
	else if (*sign_ubi==0x23494255)
		return FW_TYPE_UBI;
	else if (*sign_doodfeed==0xedfe0dd0)
		return FW_TYPE_NOR;
	else if (*sign_55aa==0xaa55)
		return FW_TYPE_GPT;
	else if (*sign_cdt==0x00544443)
		return FW_TYPE_CDT;
	else if (*sign_elf==0x464c457f)
		return FW_TYPE_ELF;
	else if (*sign_mibib==0xfe569fac)
		return FW_TYPE_MIBIB;
	else
		return -1;
	return 0;
}
