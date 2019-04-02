/* output.c - Output module frontend
 *
 * Copyright (C) 2007 Ivo Clarysse,  (C) 2012 Henner Zeller
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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "logging.h"
#include "output_module.h"
#include "output.h"

static const struct output_module *output_module = NULL;

int output_init(const struct output_module *module)
{
	output_module = module;
	
	Log_info("output", "Using output module: %s (%s)",
		 output_module->shortname, output_module->description);

	if (output_module->init) {
		return output_module->init();
	}

	return 0;
}

int output_loop(void)
{
	if (output_module && output_module->loop) {
		return output_module->loop();
	}

    return 0;
}

void output_set_uri(const char *uri, output_update_meta_cb_t meta_cb) {
	if (output_module && output_module->set_uri) {
		output_module->set_uri(uri, meta_cb);
	}
}
void output_set_next_uri(const char *uri) {
	if (output_module && output_module->set_next_uri) {
		output_module->set_next_uri(uri);
	}
}

int output_play(output_transition_cb_t transition_callback) {
	if (output_module && output_module->play) {
		return output_module->play(transition_callback);
	}
	return -1;
}

int output_pause(void) {
	if (output_module && output_module->pause) {
		return output_module->pause();
	}
	return -1;
}

int output_stop(void) {
	if (output_module && output_module->stop) {
		return output_module->stop();
	}
	return -1;
}

int output_seek(int64_t position_nanos) {
	if (output_module && output_module->seek) {
		return output_module->seek(position_nanos);
	}
	return -1;
}

int output_get_position(int64_t *track_dur, int64_t *track_pos) {
	if (output_module && output_module->get_position) {
		return output_module->get_position(track_dur, track_pos);
	}
	return -1;
}

int output_get_volume(float *value) {
	if (output_module && output_module->get_volume) {
		return output_module->get_volume(value);
	}
	return -1;
}
int output_set_volume(float value) {
	if (output_module && output_module->set_volume) {
		return output_module->set_volume(value);
	}
	return -1;
}
int output_get_mute(int *value) {
	if (output_module && output_module->get_mute) {
		return output_module->get_mute(value);
	}
	return -1;
}
int output_set_mute(int value) {
	if (output_module && output_module->set_mute) {
		return output_module->set_mute(value);
	}
	return -1;
}
