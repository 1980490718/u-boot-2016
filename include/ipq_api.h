#define RESET_BUTTON_PRESSED        0

#define LED_ON 1
#define LED_OFF 0

#define WEBFAILSAFE_PROGRESS_START 				0
#define WEBFAILSAFE_PROGRESS_TIMEOUT			1
#define WEBFAILSAFE_PROGRESS_UPLOAD_READY		2
#define WEBFAILSAFE_PROGRESS_UPGRADE_READY		3
#define WEBFAILSAFE_PROGRESS_UPGRADE_FAILED		4

#define WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE		0
#define WEBFAILSAFE_UPGRADE_TYPE_UBOOT			1
#define WEBFAILSAFE_UPGRADE_TYPE_ART 			2
#define WEBFAILSAFE_UPGRADE_TYPE_IMG			3
#define WEBFAILSAFE_UPGRADE_TYPE_CDT			4
#define WEBFAILSAFE_UPGRADE_TYPE_MIBIB			5

#define CONFIG_LOADADDR								(unsigned long) 0x44000000 /* console default address */
#define WEBFAILSAFE_UPLOAD_RAM_ADDRESS				(unsigned long) 0x50000000

/*#define WEBFAILSAFE_UPLOAD_UBOOT_ADDRESS			(unsigned long) 0x520000
#define WEBFAILSAFE_UPLOAD_ART_ADDRESS				(unsigned long) 0x660000

#define CONFIG_ART_START							(unsigned long) 0x660000
*/

#define UBOOT_START_ADDR_NAND						(unsigned long) 0x800000 /* offset 0x800000 */
#define UBOOT_SIZE_NAND								(unsigned long) (1536 * 1024) /* 1.5MiB hex length is 0x180000 */
#define ART_START_ADDR_NAND							(unsigned long) 0x1100000 /* offset 0x1100000 */
#define ART_SIZE_NAND								WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES
#define FIRMWARE_START_ADDR_NAND					(unsigned long) 0xa00000 /* offset 0xa00000 */
#define FIRMWARE_SIZE_NAND							(unsigned long) (115 * 1024 * 1024) /* 115MiB hex length is 0x7300000 */
#define CDT_START_ADDR_NAND							(unsigned long) 0xc80000 /* offset 0xc80000 */
#define CDT_1_START_ADDR_NAND						(unsigned long) 0xd00000 /* offset 0xc90000 */
#define CDT_SIZE_NAND								(unsigned long) (256 * 1024) /* 256KiB hex length is 0x40000 */
#define MIBIB_START_ADDR_NAND						(unsigned long) 0x180000 /* offset 0x180000 */
#define MIBIB_SIZE_NAND								(unsigned long) (1024 * 1024) /* 1MiB hex length is 0x100000 */

#define UBOOT_START_ADDR_NOR						(unsigned long) 0x520000 /* offset 0x520000 */
#define UBOOT_1_START_ADDR_NOR						(unsigned long) 0x5C0000 /* offset 0x5C0000 */
#define UBOOT_SIZE_NOR								WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES
#define FIRMWARE_START_ADDR_NOR						(unsigned long) 0x6a0000 /* offset 0x6a0000 */
#define ART_START_ADDR_NOR							(unsigned long) 0x660000 /* offset 0x660000 */
#define ART_SIZE_NOR								WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES
#define MIBIB_START_ADDR_NOR						(unsigned long) 0xc0000 /* offset 0xc0000 */
#define MIBIB_SIZE_NOR								WEBFAILSAFE_UPLOAD_MIBIB_SIZE_IN_BYTES
#define CDT_START_ADDR_NOR							(unsigned long) 0x4f0000 /* offset 0xc80000 */
#define CDT_1_START_ADDR_NOR						(unsigned long) 0x500000 /* offset 0xc80000 */
#define CDT_SIZE_NOR								WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES

#define HLOS_NAME									"hlos-0cc33b23252699d495d79a843032498bfa593aba"
#define ROOTFS_NAME									"rootfs-f3c50b484767661151cfb641e2622703e45020fe"
#define WIFIFW_NAME									"wififw-45b62ade000c18bfeeb23ae30e5a6811eac05e2f"

#define GPT_NAME									"0:GPT" /* eMMC only,is equal to MIBIB. */
#define MIBIB_NAME									"0:MIBIB"
#define CDT_NAME									"0:CDT"
#define CDT_NAME_1									"0:CDT_1"
#define UBOOT_NAME									"0:APPSBL"
#define UBOOT_NAME_1								"0:APPSBL_1"
#define ART_NAME									"0:ART"

#define WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES		(unsigned long) (640 * 1024) /* 640KiB hex length is 0x100000 */
#define WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES		(unsigned long) (256 * 1024) /* 256KiB hex length is 0x40000 */
#define WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES		(unsigned long) (256 * 1024) /* 256KiB hex length is 0x40000 */
#define WEBFAILSAFE_UPLOAD_ART_BIG_SIZE_IN_BYTES	(unsigned long) (512 * 1024) /* 512KiB hex length is 0x80000 */
#define WEBFAILSAFE_UPLOAD_MIBIB_SIZE_IN_BYTES		(unsigned long) (64 * 1024) /* 64KiB hex length is 0x10000 */

#define FW_TYPE_NOR 0
#define FW_TYPE_EMMC 1
#define FW_TYPE_QSDK 2
#define FW_TYPE_UBI 3
#define FW_TYPE_CDT 4
#define FW_TYPE_ELF 5

int check_test(void);
int check_config(void);
int auto_update_by_tftp(void);
int check_fw_type(void *address);
void led_toggle(const char *gpio_name);
void led_on(const char *gpio_name);
void led_off(const char *gpio_name);
void check_button_is_press(void);