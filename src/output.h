/* output.h - Output module frontend
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

#ifndef _OUTPUT_H
#define _OUTPUT_H

#include <glib.h>

enum PlayFeedback {
	PLAY_STOPPED,
	PLAY_STARTED_NEXT_STREAM,
};
typedef void (*done_cb)(enum PlayFeedback);

// In case the stream gets to know details about the song...
struct SongMetaData {
	char *title;
	char *artist;
	char *album;
	char *genre;
};
void SongMetaData_init(struct SongMetaData *value);
void SongMetaData_clear(struct SongMetaData *value);

// Returns a newly allocated xml string with the song meta data encoded as
// DIDL-Lite. If we get an original xml document, returns an edited version of
// that document.
char *SongMetaData_to_DIDL(const char *original_xml,
			   const struct SongMetaData *value);

// Callback with changes
typedef void (*update_meta_cb)(const struct SongMetaData *);

int output_init(const char *shortname);
int output_add_options(GOptionContext *ctx);
void output_dump_modules(void);

int output_loop(void);

void output_set_uri(const char *uri, update_meta_cb meta_info);
void output_set_next_uri(const char *uri);

int output_play(done_cb done_callback);
int output_stop(void);
int output_pause(void);
int output_get_position(gint64 *track_dur_nanos, gint64 *track_pos_nanos);
int output_seek(gint64 position_nanos);

int output_get_volume(float *v);
int output_set_volume(float v);
int output_get_mute(int *m);
int output_set_mute(int m);

#endif /* _OUTPUT_H */
