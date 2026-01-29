#define RESET_BUTTON_PRESSED        0

#define LED_ON 1
#define LED_OFF 0

/* WebFailsafe progress status */
#define WEBFAILSAFE_PROGRESS_START 				0
#define WEBFAILSAFE_PROGRESS_TIMEOUT			1
#define WEBFAILSAFE_PROGRESS_UPLOAD_READY		2
#define WEBFAILSAFE_PROGRESS_UPGRADING			3
#define WEBFAILSAFE_PROGRESS_UPGRADE_READY		4
#define WEBFAILSAFE_PROGRESS_UPGRADE_FAILED		5

/* WebFailsafe upgrade type */
#define WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE		0
#define WEBFAILSAFE_UPGRADE_TYPE_UBOOT			1
#define WEBFAILSAFE_UPGRADE_TYPE_ART 			2
#define WEBFAILSAFE_UPGRADE_TYPE_IMG			3
#define WEBFAILSAFE_UPGRADE_TYPE_CDT			4
#define WEBFAILSAFE_UPGRADE_TYPE_MIBIB			5

/* firmware type */
enum firmware_type_enum {
	FW_TYPE_NOR		= 0,
	FW_TYPE_GPT		= 1,
	FW_TYPE_QSDK	= 2,
	FW_TYPE_UBI		= 3,
	FW_TYPE_CDT		= 4,
	FW_TYPE_ELF		= 5,
	FW_TYPE_MIBIB	= 6,
};

/* flash type from "arch/arm/include/asm/arch-qca-common/smem.h"
enum flash_type_enum {
	SMEM_BOOT_NO_FLASH			= 0,
	SMEM_BOOT_NOR_FLASH			= 1,
	SMEM_BOOT_NAND_FLASH		= 2,
	SMEM_BOOT_ONENAND_FLASH		= 3,
	SMEM_BOOT_SDC_FLASH			= 4,
	SMEM_BOOT_MMC_FLASH			= 5,
	SMEM_BOOT_SPI_FLASH			= 6,
	SMEM_BOOT_NORPLUSNAND		= 7,
	SMEM_BOOT_NORPLUSEMMC		= 8,
	SMEM_BOOT_QSPI_NAND_FLASH	= 11,
};
*/
/* flash type */
#define FLASH_TYPE_NO_FLASH			0
#define FLASH_TYPE_NOR				1
#define FLASH_TYPE_NAND				2
#define FLASH_TYPE_ONENAND			3
#define FLASH_TYPE_SDC				4
#define FLASH_TYPE_MMC				5
#define FLASH_TYPE_SPI				6
#define FLASH_TYPE_NOR_PLUS_NAND	7
#define FLASH_TYPE_NOR_PLUS_EMMC	8
#define FLASH_TYPE_QSPI_NAND		11

/* load address */
#define CONFIG_LOADADDR								(unsigned long) 0x44000000 /* console default address */
#if defined(CONFIG_256MB_RAM)
#define WEBFAILSAFE_UPLOAD_RAM_ADDRESS				CONFIG_LOADADDR /* For 256MB RAM, use the console default address */
#else
#define WEBFAILSAFE_UPLOAD_RAM_ADDRESS				(unsigned long) 0x50000000
#endif
/* simplify the WEBFAILSAFE_UPLOAD_RAM_ADDRESS as UPLOAD_ADDR */
#define UPLOAD_ADDR									WEBFAILSAFE_UPLOAD_RAM_ADDRESS

/* nand flash offset start and size */
#define UBOOT_START_ADDR_NAND						(unsigned long) 0x800000 /* offset 0x800000 */
#define UBOOT_SIZE_NAND								(unsigned long) (1536 * 1024) /* 1.5MiB hex length is 0x180000 */
#define ART_START_ADDR_NAND							(unsigned long) 0x1100000 /* offset 0x1100000 */
#define ART_SIZE_NAND								WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES
#define FIRMWARE_START_ADDR_NAND					(unsigned long) 0xa00000 /* offset 0xa00000 */
#define FIRMWARE_SIZE_NAND							(unsigned long) (115 * 1024 * 1024) /* 115MiB hex length is 0x7300000 */
#define CDT_START_ADDR_NAND							(unsigned long) 0xc80000 /* offset 0xc80000 */
#define CDT_1_START_ADDR_NAND						(unsigned long) 0xd00000 /* offset 0xd00000 */
#define CDT_SIZE_NAND								(unsigned long) (256 * 1024) /* 256KiB hex length is 0x40000 */
#define MIBIB_START_ADDR_NAND						(unsigned long) 0x180000 /* offset 0x180000 */
#define MIBIB_SIZE_NAND								(unsigned long) (1024 * 1024) /* 1MiB hex length is 0x100000 */

/* nor flash offset start and size */
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

/* extract file name from oem binary */
#define HLOS_NAME									"hlos-0cc33b23252699d495d79a843032498bfa593aba"
#define ROOTFS_NAME									"rootfs-f3c50b484767661151cfb641e2622703e45020fe"
#define WIFIFW_NAME									"wififw-45b62ade000c18bfeeb23ae30e5a6811eac05e2f"

/* partition name */
#define GPT_NAME									"0:GPT" /* eMMC only,is equal to MIBIB. */
#define MIBIB_NAME									"0:MIBIB"
#define CDT_NAME									"0:CDT"
#define CDT_NAME_1									"0:CDT_1"
#define UBOOT_NAME									"0:APPSBL"
#define UBOOT_NAME_1								"0:APPSBL_1"
#define ART_NAME									"0:ART"
#define ROOTFS_NAME0								"rootfs"
#if defined(CONFIG_IPQ807X_XGLINK_5GCPE)
#define ROOTFS_NAME1								"rootfs_1"
#else
#define ROOTFS_NAME1								"rootfs1"
#endif
#define ROOTFS_NAME2								"rootfs2"

/* dynamic upgrade file size limit */
#define WEBFAILSAFE_UPLOAD_UBOOT_SIZE_IN_BYTES		get_uboot_size()
#define WEBFAILSAFE_UPLOAD_ART_SIZE_IN_BYTES		get_art_size()
#define WEBFAILSAFE_UPLOAD_CDT_SIZE_IN_BYTES		get_cdt_size()
#define WEBFAILSAFE_UPLOAD_MIBIB_SIZE_IN_BYTES		get_mibib_size()

/* function declarations */
int check_test(void);
int check_config(void);
int auto_update_by_tftp(void);
int check_fw_type(void *address);
void led_toggle(const char *gpio_name);
void led_on(const char *gpio_name);
void led_off(const char *gpio_name);
void check_button_is_press(void);
/* main api for get smem table size*/
unsigned long get_smem_table_size_bytes(const char *name);
/* api for webfailsafe upgrade size limit */
unsigned long get_uboot_size(void);
unsigned long get_art_size(void);
unsigned long get_firmware_size(void);
unsigned long get_cdt_size(void);
unsigned long get_mibib_size(void);