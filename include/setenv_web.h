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

#ifndef _SETENV_WEB_H_
#define _SETENV_WEB_H_

/* Function to handle environment variable operations via web interface */
int web_setenv_handle(int argc, char **argv, char *resp_buf, int bufsize);

#endif /* _SETENV_WEB_H_ */