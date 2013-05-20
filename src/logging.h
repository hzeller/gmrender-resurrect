/* logging.h - Logging facility
 *
 * Copyright (C) 2013 Henner Zeller
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
 * MA 02110-1301, USA.
 *
 */

#ifndef _LOGGING_H
#define _LOGGING_H

// Define this with empty, if you're not using gcc.
#define PRINTF_FMT_CHECK(fmt_pos, args_pos) \
    __attribute__ ((format (printf, fmt_pos, args_pos)))

// With filename given, logs info and error to that file. If filename is NULL,
// nothing is logged (TODO: log error to syslog).
void Log_init(const char *filename);
int Log_color_allowed(void);  // Returns if we're allowed to use terminal color.
int Log_info_enabled(void);
int Log_error_enabled(void);

void Log_info(const char *category, const char *format, ...)
	PRINTF_FMT_CHECK(2, 3);
void Log_error(const char *category, const char *format, ...)
	PRINTF_FMT_CHECK(2, 3);

#endif /* _LOGGING_H */
