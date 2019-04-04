/* output_module.c - Output module frontend
 *
 * Copyright (C) 2014-2019 Marc Chalain
 *
 * This file is part of GMediaRender.
 *
 * uplaymusic is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * uplaymusic is distributed in the hope that it will be useful,
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "logging.h"
#include "output_module.h"
#ifdef HAVE_GST
#include "output_gstreamer.h"
#endif
#ifdef HAVE_MPG123
#include "output_mpg123.h"
#endif
#include "output.h"

static const struct output_module *modules[] = {
#ifdef HAVE_GST
	&gstreamer_output,
#endif
};

void output_module_dump_modules(void)
{
	int count;
	
	count = sizeof(modules) / sizeof(struct output_module *);
	if (count == 0) {
		puts("  NONE!");
	} else {
		int i;
		for (i=0; i<count; i++) {
			printf("Available output: %s\t%s%s\n",
			       modules[i]->shortname,
			       modules[i]->description,
			       (i==0) ? " (default)" : "");
		}
	}
}

const struct output_module *output_module_get(const char *shortname)
{
	const struct output_module *output_module = NULL;
	int count;

	count = sizeof(modules) / sizeof(struct output_module *);
	if (count == 0) {
		Log_error("output", "No output module available");
		return NULL;
	}
	if (shortname == NULL) {
		output_module = modules[0];
	} else {
		int i;
		for (i=0; i<count; i++) {
			if (strcmp(modules[i]->shortname, shortname)==0) {
				Log_info("output_module", "get %s",modules[i]->shortname);
				output_module = modules[i];
				break;
			}
		}
	}
	
	return output_module;
}

int output_module_add_goptions(GOptionContext *ctx)
{
	int count, i;

	count = sizeof(modules) / sizeof(struct output_module *);
	for (i = 0; i < count; ++i) {
		if (modules[i]->add_goptions) {
			int result = modules[i]->add_goptions(ctx);
			if (result != 0) {
				return result;
			}
		}
	}
	return 0;
}
