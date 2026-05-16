/*
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <libfdt.h>
#include <linux/compiler.h>

int __weak checkboard(void)
{
	return 0;
}

/*
 * If the root node of the DTB has a "model" and "config_name" property, show them.
 * Then call checkboard().
 */
int show_board_info(void)
{
	int ret = 0;

#if defined(CONFIG_OF_CONTROL) || !defined(CONFIG_CUSTOM_BOARDINFO)
	DECLARE_GLOBAL_DATA_PTR;
	const char *model = NULL;
#ifdef CONFIG_BOARD_DISPLAY_NAME
	const char *config_name = NULL;
#endif

	if (gd && gd->fdt_blob) {
		model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
#ifdef CONFIG_BOARD_DISPLAY_NAME
		config_name = fdt_getprop(gd->fdt_blob, 0, "config_name", NULL);
#endif

		if (model) {
#ifdef CONFIG_BOARD_DISPLAY_NAME
			printf("Model: %s (%s)\n", CONFIG_BOARD_DISPLAY_NAME, model);
#else
			printf("Model: %s\n", model);
#endif
		}

#ifdef CONFIG_BOARD_DISPLAY_NAME
		if (config_name) {
			int count = fdt_count_strings(gd->fdt_blob, 0, "config_name");
			if (count > 0) {
				printf("Config Name(s): ");
				int first = 1;
				for (int i = 0; i < count && i < 8; i++) {
					const char *name;
					int ret2 = fdt_get_string_index(gd->fdt_blob, 0, "config_name", i, &name);
					if (ret2 == 0 && name && strchr(name, '@')) {
						printf("%s%s", first ? "" : ", ", name);
						first = 0;
					}
				}
				printf("\n");
			}
		}
#endif
	}
#endif

	ret = checkboard();
	return ret;
}
