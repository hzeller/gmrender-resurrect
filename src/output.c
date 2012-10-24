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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "output_module.h"
#include "output_dummy.h"
#ifdef HAVE_GST
#include "output_gstreamer.h"
#endif
#include "output.h"

#include "xmlescape.h"

static struct output_module *modules[] = {
#ifdef HAVE_GST
	&gstreamer_output,
#endif
	&dummy_output,
};

static struct output_module *output_module = NULL;

void SongMetaData_init(struct SongMetaData *value) {
	memset(value, 0, sizeof(struct SongMetaData));
}
void SongMetaData_clear(struct SongMetaData *value) {
	free(value->title);
	value->title = NULL;
	free(value->artist);
	value->artist = NULL;
	free(value->album);
	value->album = NULL;
	free(value->genre);
	value->genre = NULL;
}

static const char kDidlHeader[] = "<DIDL-Lite "
	"xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
	"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
	"xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
static const char kDidlFooter[] = "</DIDL-Lite>";

static char *generate_DIDL(const char *id,
			   const char *title, const char *artist,
			   const char *album, const char *genre) {
	char *result = NULL;
	asprintf(&result, "%s\n<item id=\"%s\">\n"
		 "\t<dc:title>%s</dc:title>\n"
		 "\t<upnp:artist>%s</upnp:artist>\n"
		 "\t<upnp:album>%s</upnp:album>\n"
		 "\t<upnp:genre>%s</upnp:genre>\n</item>\n%s",
		 kDidlHeader, id,
		 title ? title : "", artist ? artist : "",
		 album ? album : "", genre ? genre : "",
		 kDidlFooter);
	return result;
}

// Takes input, if it finds the given tag, then replaces the content between
// these with 'content'. It might re-allocate the original string; only the
// returned string is valid.
// Very crude way to edit XML.
static char *replace_range(char *input,
			   const char *tag_start, const char *tag_end,
			   const char *content) {
	if (content == NULL)  // unknown content; document unchanged.
		return input;
	//fprintf(stderr, "------- Before ------\n%s\n-------------\n", input);
	const int total_len = strlen(input);
	const char *start_pos = strstr(input, tag_start);
	if (start_pos == NULL) return input;
	start_pos += strlen(tag_start);
	const char *end_pos = strstr(start_pos, tag_end);
	if (end_pos == NULL) return input;
	int old_content_len = end_pos - start_pos;
	int new_content_len = strlen(content);
	char *result = NULL;
	if (old_content_len != new_content_len) {
		result = (char*)malloc(total_len
				       + new_content_len - old_content_len + 1);
		strncpy(result, input, start_pos - input);
		strncpy(result + (start_pos - input), content, new_content_len);
		strcpy(result + (start_pos - input) + new_content_len, end_pos);
		free(input);
	} else {
		// Typically, we replace the same content with itself - same
		// length. No realloc in this case.
		strncpy(input + (start_pos - input), content, new_content_len);
		result = input;
	}
	//fprintf(stderr, "------- After ------\n%s\n-------------\n", result);
	return result;
}

// TODO: actually use some XML library for this, but spending too much time
// with XML is not good for the brain :) Worst thing that came out of the 90ies.
char *SongMetaData_to_DIDL(const char *original_xml,
			   const struct SongMetaData *value) {
	// Generating a unique ID in case the players cache the content by
	// the item-ID. Right now this is experimental and not known to make
	// any difference - it seems that players just don't display changes
	// in the input stream. Grmbl.
	static unsigned int xml_id = 42;
	char unique_id[4 + 8 + 1];
	snprintf(unique_id, sizeof(unique_id), "gmr-%08x", xml_id++);

	char *result;
	char *title, *artist, *album, *genre;
	title = value->title ? xmlescape(value->title, 0) : NULL;
	artist = value->artist ? xmlescape(value->artist, 0) : NULL;
	album = value->album ? xmlescape(value->album, 0) : NULL;
	genre = value->genre ? xmlescape(value->genre, 0) : NULL;
	if (original_xml == NULL || strlen(original_xml) == 0) {
		result = generate_DIDL(unique_id, title, artist, album, genre);
	} else {
		// Otherwise, surgically edit the original document to give
		// control points as close as possible what they sent themself.
		result = strdup(original_xml);
		result = replace_range(result, "<dc:title>", "</dc:title>",
				       title);
		result = replace_range(result,
				       "<upnp:artist>", "</upnp:artist>",
				       artist);
		result = replace_range(result, "<upnp:album>", "</upnp:album>",
				       album);
		result = replace_range(result, "<upnp:genre>", "</upnp:genre>",
				       genre);
		result = replace_range(result, "id=\"", "\"", unique_id);
	}
	free(title);
	free(artist);
	free(album);
	free(genre);
	return result;
}

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

void output_set_uri(const char *uri, update_meta_cb meta_cb) {
	if (output_module && output_module->set_uri) {
		output_module->set_uri(uri, meta_cb);
	}
}
void output_set_next_uri(const char *uri) {
	if (output_module && output_module->set_next_uri) {
		output_module->set_next_uri(uri);
	}
}

int output_play(done_cb done_callback) {
	int result = -1;
	if (output_module && output_module->play) {
		result = output_module->play(done_callback);
	}
	return result;
}

int output_pause(void) {
	int result = -1;
	if (output_module && output_module->pause) {
		result = output_module->pause();
	}
	return result;
}

int output_stop(void) {
	int result = -1;
	if (output_module && output_module->stop) {
		result = output_module->stop();
	}
	return result;
}

int output_seek(gint64 position_nanos) {
	int result = -1;
	if (output_module && output_module->seek) {
		result = output_module->seek(position_nanos);
	}
	return result;
}

int output_get_position(gint64 *track_dur, gint64 *track_pos) {
	int result = -1;
	if (output_module && output_module->get_position) {
		result = output_module->get_position(track_dur, track_pos);
	}
	return result;
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
