/*
 * Copyright (C) 2026 Willem Lee <1980490718@qq.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <command.h>
#include <environment.h>
#include <asm/global_data.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <malloc.h>

DECLARE_GLOBAL_DATA_PTR;

/* Environment variable entry */
struct env_entry {
	char *name;
	char *value;
};

/* Decode URL-encoded string */
static void urldecode(char *dst, const char *src) {
	char a, b;
	while (*src) {
		if (*src == '%' && (a = src[1]) && (b = src[2]) && isxdigit(a) && isxdigit(b)) {
			a = (a >= 'a') ? a - ('a' - 'A') : a;
			a = (a >= 'A') ? a - ('A' - 10) : a - '0';
			b = (b >= 'a') ? b - ('a' - 'A') : b;
			b = (b >= 'A') ? b - ('A' - 10) : b - '0';
			*dst++ = 16 * a + b;
			src += 3;
		} else {
			*dst++ = (*src == '+') ? ' ' : *src;
			src++;
		}
	}
	*dst = '\0';
}

/* Compare function for qsort */
static int env_compare(const void *a, const void *b) {
	return strcmp(((const struct env_entry *)a)->name, ((const struct env_entry *)b)->name);
}

/* Get environment variables */
static int env_get_items(struct env_entry **entries, int *count) {
	ENTRY *ep;
	int env_size = 0, idx = 0, j, i = 0;
	struct env_entry *env_list;
	if (!(gd->flags & GD_FLG_ENV_READY))
		return -1;
	while ((idx = hmatch_r("", idx, &ep, &env_htab)))
		env_size++;
	env_list = malloc(env_size * sizeof(struct env_entry));
	if (!env_list)
		return -1;
	idx = 0;
	while ((idx = hmatch_r("", idx, &ep, &env_htab))) {
		env_list[i].name = strdup(ep->key);
		env_list[i].value = strdup(ep->data ? ep->data : "");
		if (!env_list[i].name || !env_list[i].value) {
			for (j = 0; j <= i; j++) {
				free(env_list[j].name);
				free(env_list[j].value);
			}
			free(env_list);
			return -1;
		}
		i++;
	}
	/* Sort the environment variables */
	qsort(env_list, env_size, sizeof(struct env_entry), env_compare);
	*entries = env_list;
	*count = env_size;
	return 0;
}

/* Append response to buffer */
static int append_resp(char *buf, int *len, int bufsize, const char *fmt, ...) {
	va_list args;
	int remaining = bufsize - *len, n;
	if (remaining <= 0)
		return 0;
	va_start(args, fmt);
	n = vsnprintf(buf + *len, remaining, fmt, args);
	va_end(args);
	if (n > 0 && n < remaining) {
		*len += n;
		return n;
	}
	return 0;
}

/* Handle setenv command */
int web_setenv_handle(int argc, char **argv, char *resp_buf, int bufsize) {
	char var_dec[4096] = {0}, val_dec[4096] = {0};
	const char *var = NULL, *val = NULL;
	int len = 0, i;
	struct env_entry *entries = NULL;
	int count = 0;
	char *env_val;
	for (i = 0; i < argc; i++) {
		/* Parse arguments */
		if (!strncmp(argv[i], "var=", 4))
			var = argv[i] + 4;
		/* val can be empty string to unset variable */
		else if (!strncmp(argv[i], "val=", 4))
			val = argv[i] + 4;
	}
	if (var) urldecode(var_dec, var);
	if (val) urldecode(val_dec, val);
	/* HTTP header */
	len += snprintf(resp_buf, bufsize, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n");
	/* Handle different cases */
	/* Get all environment variables */
	if (!var_dec[0] || !strcmp(var_dec, "all")) {
		if (env_get_items(&entries, &count) == 0) {
			for (i = 0; i < count; i++) {
				if (len >= bufsize - 1) {
					/* Truncate the response if buffer is full */
					append_resp(resp_buf, &len, bufsize, "[>>truncated<<]\n");
					break;
				}
				append_resp(resp_buf, &len, bufsize, "%s=%s\n", entries[i].name, entries[i].value);
				/* Free each entry */
				free(entries[i].name);
				free(entries[i].value);
			}
			/* Free memory */
			free(entries);
		}
	/* Reset to default environment */
	} else if (!strcmp(var_dec, "default")) {
		set_default_env("Resetting to default environment");
		append_resp(resp_buf, &len, bufsize, saveenv() == 0 ? "OK: default\n" : "ERR: default\n");
	/* Get environment variable */
	} else if (val == NULL) {
		env_val = getenv(var_dec);
		append_resp(resp_buf, &len, bufsize, env_val ? "%s=%s\n" : "ERR: not found\n", var_dec, env_val ? env_val : "");
	/* Set or unset environment variable */
	} else if (!val_dec[0]) {
		append_resp(resp_buf, &len, bufsize, (setenv(var_dec, NULL) == 0 && saveenv() == 0) ? "OK: unset %s\n" : "ERR: unset %s\n", var_dec);
	} else {
		append_resp(resp_buf, &len, bufsize, (setenv(var_dec, val_dec) == 0 && saveenv() == 0) ? "OK: %s=%s\n" : "ERR: set %s\n", var_dec, val_dec);
	}
	resp_buf[len] = '\0';
	return len;
}