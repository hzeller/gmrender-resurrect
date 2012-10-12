/* output.c - Output module frontend
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "output_module.h"
#include "output_dummy.h"
#ifdef HAVE_GST
#include "output_gstreamer.h"
#endif
#include "output.h"

static struct output_module *modules[] = {
#ifdef HAVE_GST
	&gstreamer_output,
#endif
	&dummy_output,
};

static struct output_module *output_module = NULL;

void output_dump_modules(void)
{
	int count;
	
	puts("Supported output modules:");

	count = sizeof(modules) / sizeof(struct output_module *);
	if (count == 0) {
		puts("  NONE!");
	} else {
		int i;
		for (i=0; i<count; i++) {
			printf("  %s\t%s%s\n", modules[i]->shortname,
			       modules[i]->description,
			       (i==0) ? " (default)" : "");
		}
	}
}

int output_init(const char *shortname)
{
	int count;
	int result = -1;

	count = sizeof(modules) / sizeof(struct output_module *);
	if (count == 0) {
		fprintf(stderr, "No output module available\n");
		goto out;
	}
	if (shortname == NULL) {
		output_module = modules[0];
	} else {
		int i;
		for (i=0; i<count; i++) {
			if (strcmp(modules[i]->shortname, shortname)==0) {
				output_module = modules[i];
				break;
			}
		}
	}
	
	if (output_module == NULL) {
		fprintf(stderr, "ERROR: No such output module: '%s'\n",
		        shortname);
		goto out;
	}

	printf("Using output module: %s (%s)\n", output_module->shortname,
	       output_module->description);

	if (output_module->init) {
		result = output_module->init();
	} else {
		result = 0;
	}
	
out:
	return result;
}

int output_loop()
{
        GMainLoop *loop;

        /* Create a main loop that runs the default GLib main context */
        loop = g_main_loop_new(NULL, FALSE);

        g_main_loop_run(loop);
        return 0;
}

int output_add_options(GOptionContext *ctx)
{
	int result = 0;
	int count, i;

	count = sizeof(modules) / sizeof(struct output_module *);
	for (i = 0; i < count; ++i) {
		if (modules[i]->add_options) {
			result = modules[i]->add_options(ctx);
			if (result != 0) {
				goto out;
			}
		}
	}
out:
	return result;
}

void output_set_uri(const char *uri)
{
	if (output_module && output_module->set_uri) {
		output_module->set_uri(uri);
	}
}

int output_play(void)
{
	int result = -1;
	if (output_module && output_module->play) {
		result = output_module->play();
	}
	return result;
}

int output_pause(void)
{
	int result = -1;
	if (output_module && output_module->pause) {
		result = output_module->pause();
	}
	return result;
}

int output_stop(void)
{
	int result = -1;
	if (output_module && output_module->stop) {
		result = output_module->stop();
	}
	return result;
}



