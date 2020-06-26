/* output_module.h - Output module interface definition
 *
 * Copyright (C) 2007   Ivo Clarysse
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

#ifndef _OUTPUT_MODULE_H
#define _OUTPUT_MODULE_H

#include <stdint.h>
#include "output.h"

struct output_module {
        const char *shortname;
        const char *description;
	int (*add_options)(int *argc, char **argv[]);

	// Commands.
	int (*init)(void);
	const char *(*version)(char *buffer, size_t len);
	void (*set_uri)(const char *uri, output_update_meta_cb_t meta_info);
	void (*set_next_uri)(const char *uri);
	int (*play)(output_transition_cb_t transition_callback);
	int (*stop)(void);
	int (*pause)(void);
	int (*seek)(int64_t position_nanos);

	// parameters
	int (*get_position)(int64_t *track_duration, int64_t *track_pos);
	int (*get_volume)(float *);
	int (*set_volume)(float);
	int (*get_mute)(int *);
	int (*set_mute)(int);

	struct output_module *next;
};

#endif
