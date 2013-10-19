/* mime_types.c - MIME types utils
 *
 * Copyright (C) 2013 Andrey Demenev
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mime_types.h"
#include "logging.h"

static struct mime_type *supported_types = NULL;

struct mime_type *get_supported_mime_types(void)
{
	return supported_types;
}

static void register_mime_type_internal(const char *mime_type) {
	struct mime_type *entry;

	for (entry = supported_types; entry; entry = entry->next) {
		if (strcmp(entry->mime_type, mime_type) == 0) {
			return;
		}
	}
	Log_info("connmgr", "Registering support for '%s'", mime_type);

	entry = malloc(sizeof(struct mime_type));
	entry->mime_type = strdup(mime_type);
	entry->next = supported_types;
	supported_types = entry;
}

void register_mime_type(const char *mime_type) {
	register_mime_type_internal(mime_type);
	if (strcmp("audio/mpeg", mime_type) == 0) {
		register_mime_type_internal("audio/x-mpeg");

		// BubbleUPnP does not seem to match generic "audio/*" types,
		// but only matches mime-types _exactly_, so we add some here.
		// TODO(hzeller): we already add the "audio/*" mime-type
		// output_gstream.c:scan_caps() which should just work once
		// BubbleUPnP allows for  matching "audio/*". Remove the code
		// here.

		// BubbleUPnP uses audio/x-scpl as an indicator to know if the
		// renderer can handle it (otherwise it will proxy).
		// Simple claim: if we can handle mpeg, then we can handle
		// shoutcast.
		// (For more accurate answer: we'd to check if all of
		// mpeg, aac, aacp, ogg are supported).
		register_mime_type_internal("audio/x-scpls");

		// This is apparently something sent by the spotifyd
		// https://gitorious.org/spotifyd
		register_mime_type("audio/L16;rate=44100;channels=2");
	}

	// Some workaround: some controllers seem to match the version without
	// x-, some with; though the mime-type is correct with x-, these formats
	// seem to be common enough to sometimes be used without.
	// If this works, we should probably collect all of these
	// in a set emit always both, foo/bar and foo/x-bar, as it is a similar
	// work-around as seen above with mpeg -> x-mpeg.
	if (strcmp("audio/x-alac", mime_type) == 0) {
	  register_mime_type_internal("audio/alac");
	}
	if (strcmp("audio/x-aiff", mime_type) == 0) {
	  register_mime_type_internal("audio/aiff");
	}
	if (strcmp("audio/x-m4a", mime_type) == 0) {
	  register_mime_type_internal("audio/m4a");
	  register_mime_type_internal("audio/mp4");
	}
}

