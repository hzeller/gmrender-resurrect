/* output_dummy.c - Dummy Output module
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

#include <glib.h>

//#define ENABLE_TRACING

#include "logging.h"
#include "output_module.h"
#include "output_dummy.h"

struct output_module dummy_output = {
        .shortname = "dummy",
	.description = "Dummy output module",
	//.init        = output_dummy_init,
	//.add_options = output_dummy_add_options,
	//.set_uri     = output_dummy_set_uri,
	//.play        = output_dummy_play,
	//.stop        = output_dummy_stop,
	//.pause       = output_dummy_pause,
};

