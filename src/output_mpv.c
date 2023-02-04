/* output_mpv.c - Output module for MPV
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
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

#include <math.h>
#include <mpv/client.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "logging.h"
#include "upnp_connmgr.h"
#include "output_module.h"

typedef enum {
	MPV_PROPERTY_EVENT_METADATA = 1,
} MPVPropertyEventType;

static void scan_mpv_mime_list(void)
{
	register_mime_type("video/*");
	register_mime_type("audio/*");
}

static GRWLock lock;
static mpv_handle *locked_mpv_handle = NULL;

static int get_mpv_handle(int f(mpv_handle *, void *), void *data)
{
	g_rw_lock_reader_lock(&lock);
	int rc;
	if (locked_mpv_handle) {
		rc = f(locked_mpv_handle, data);
	} else {
		Log_error("mpv", "MPV used before created");
		rc = -1;
	}
	g_rw_lock_reader_unlock(&lock);
	return rc;
}

static char *gsuri_ = NULL;         // locally strdup()ed
static char *gs_next_uri_ = NULL;   // locally strdup()ed
static int file_loaded = 0;
static int paused = 0;
static struct SongMetaData song_meta_;

static output_transition_cb_t play_trans_callback_ = NULL;
static output_update_meta_cb_t meta_update_callback_ = NULL;

typedef struct {
	const char *name;
	mpv_format format;
	void *data;
} property_access_data;

inline int do_set_property(mpv_handle *mpv_player, void *args)
{
	property_access_data *data = args;
	if (mpv_set_property(mpv_player, data->name, data->format, data->data) < 0) {
		Log_error("mpv", "Failed set property: %s", data->name);
		return -1;
	} else {
		return 0;
	}
}

// using write lock
static inline int set_property(const char *name, mpv_format format, void *data)
{
	property_access_data args = {name, format, data};
	return get_mpv_handle(do_set_property, &args);
}

inline int do_get_property(mpv_handle *mpv_player, void *args)
{
	property_access_data *data = args;
	if (mpv_get_property(mpv_player, data->name, data->format, data->data) < 0) {
		Log_error("mpv", "Failed get property: %s", data->name);
		return -1;
	} else {
		return 0;
	}
}

// using read lock
static inline int get_property(const char *name, mpv_format format, void *data)
{
	property_access_data args = {name, format, data};
	return get_mpv_handle(do_get_property, &args);
}

static inline int do_exec_command(mpv_handle *mpv_player, void *data)
{
	const char **cmd = (const char **) data;
	if (mpv_command(mpv_player, cmd) < 0) {
		Log_error("mpv", "Failed exec command: %s", cmd[0]);
		return -1;
	} else {
		return 0;
	}
}

// using read lock
static inline int exec_command(const char *cmd[])
{
	return get_mpv_handle(do_exec_command, cmd);
}

static int output_mpv_stop(void)
{
	const char *cmd[] = {"stop", NULL};
	return exec_command(cmd);
}

static int output_mpv_pause(int pause_target)
{
	int rc = set_property("pause", MPV_FORMAT_FLAG, &pause_target);
	if (rc == 0) {
		Log_info("mpv", "Set pause: %d", pause_target);
		paused = pause_target;
	}
	return rc;
}

static int output_mpv_pause_wrap(void)
{
	return output_mpv_pause(1);
}

static int output_mpv_seek(gint64 position_nanos)
{
	gint64 pos = position_nanos / 1000000000;
	if (set_property("time-pos", MPV_FORMAT_INT64, &pos) == 0) {
		Log_info("mpv", "Set time-pos: %ld", pos);
		return 0;
	} else {
		return -1;
	}
}

static void output_mpv_set_next_uri(const char *uri)
{
	Log_info("mpv", "Set next uri to '%s'", uri);
	free(gs_next_uri_);
	gs_next_uri_ = (uri && *uri) ? strdup(uri) : NULL;
}

static double initial_db = 0.0;

/* Options specific to output_mpv */
static GOptionEntry option_entries[] = {
		{"mpvout-initial-volume-db", 0, 0, G_OPTION_ARG_DOUBLE, &initial_db,
				"MPV initial volume in decibel (e.g. 0.0 = max; -6 = 1/2 max) ",
				NULL},
		{NULL}
};

static int output_mpv_add_options(GOptionContext *ctx)
{
	GOptionGroup *option_group;
	option_group = g_option_group_new("mpvout", "MPV Output Options",
	                                  "Show MPV Output Options",
	                                  NULL, NULL);
	g_option_group_add_entries(option_group, option_entries);

	g_option_context_add_group(ctx, option_group);

	return 0;
}

static int output_mpv_get_position(gint64 *track_duration,
                                   gint64 *track_pos)
{
	if (!file_loaded) {
		*track_duration = 0;
		*track_pos = 0;
		return 0;
	}

	int rc = 0;
	gint64 time;
	if (get_property("duration", MPV_FORMAT_INT64, &time) == 0) {
		*track_duration = time * 1000000000;
	} else {
		rc = -1;
	}
	if (get_property("time-pos", MPV_FORMAT_INT64, &time) == 0) {
		*track_pos = time * 1000000000;
	} else {
		rc = -1;
	}

	return rc;
}

static int output_mpv_get_volume(float *v)
{
	double volume;
	if (get_property("volume", MPV_FORMAT_DOUBLE, &volume) == 0) {
		Log_info("mpv", "Query volume fraction: %f", volume);
		*v = volume / 100;
		return 0;
	} else {
		return -1;
	}
}

static int output_mpv_set_volume(float value)
{
	double percent = value * 100;
	if (set_property("volume", MPV_FORMAT_DOUBLE, &percent) == 0) {
		Log_info("mpv", "Set volume fraction: %f", percent);
		return 0;
	} else {
		return -1;
	}
}

static int output_mpv_get_mute(int *m)
{
	if (get_property("mute", MPV_FORMAT_FLAG, m) == 0) {
		Log_info("mpv", "Get mute: %d", *m);
	}
	return 0;
}

static int output_mpv_set_mute(int m)
{
	if (set_property("mute", MPV_FORMAT_FLAG, &m) == 0) {
		Log_info("mpv", "Set mut: %d", m);
	}
	return 0;
}

static void prepare_next_stream()
{
	Log_info("mpv", "about-to-finish cb: setting uri %s",
	         gs_next_uri_);
	free(gsuri_);
	gsuri_ = gs_next_uri_;
	gs_next_uri_ = NULL;
	file_loaded = 0;
	if (gsuri_ != NULL) {
		const char *cmd[] = {"loadfile", gsuri_, NULL};
		exec_command(cmd);
		if (play_trans_callback_) {
			// TODO(hzeller): can we figure out when we _actually_
			// start playing this ? there are probably a couple
			// of seconds between now and actual start.
			play_trans_callback_(PLAY_STARTED_NEXT_STREAM);
		}
	}
}

static void output_mpv_update_properties(MPVPropertyEventType type, mpv_event_property *data)
{
	switch (type) {
		case MPV_PROPERTY_EVENT_METADATA:
			SongMetaData_clear(&song_meta_);
			// TODO , by now, can not find any stream with metadata updated.
			break;
		default:
			break;
	}
}

static int create_mpv_handle(void);

static gpointer output_mpv_event_processor(gpointer user_data)
{
	mpv_handle *mpv_player = user_data;
	int stop = 0;
	while (!stop) {
		mpv_event *event = mpv_wait_event(mpv_player, -1);
		switch (event->event_id) {
			case MPV_EVENT_SHUTDOWN:
				// when play video, and user close video window by hand
				create_mpv_handle();
				stop = 1;
				break;
			case MPV_EVENT_FILE_LOADED:
				file_loaded = 1;
				break;
			case MPV_EVENT_END_FILE:
				prepare_next_stream();
				break;
			case MPV_EVENT_PROPERTY_CHANGE:
				output_mpv_update_properties(event->reply_userdata, event->data);
				break;
			default:
				Log_info("mpv", "Got unprocessed event: %d", event->event_id);
				break;
		}
	}
	return NULL;
}

static mpv_handle *output_mpv_create()
{
	mpv_handle *handle = mpv_create();
	if (!handle) {
		Log_error("mpv", "Can not create mpv handle");
		return NULL;
	}

	file_loaded = 0;
	paused = 0;
	int rc = mpv_set_option_string(handle, "input-default-bindings", "yes");
	if (rc < 0) {
		Log_error("mpv", "Set option failed; %s", mpv_error_string(rc));
		mpv_destroy(handle);
		return NULL;
	}
	mpv_set_option_string(handle, "input-vo-keyboard", "yes");

	rc = mpv_initialize(handle);
	if (rc < 0) {
		Log_error("mpv", "Can not initialize; %s", mpv_error_string(rc));
		mpv_destroy(handle);
		return NULL;
	}

	g_thread_new("mpv", output_mpv_event_processor, handle);

	int flag = 0;
	property_access_data data = {"mute", MPV_FORMAT_FLAG, &flag};
	do_set_property(handle, &data);
	data.name = "fullscreen";
	flag = 1;
	do_set_property(handle, &data);

	if (initial_db < 0) {
		float value = exp(initial_db / 20 * log(10));
		data.name = "volume";
		data.format = MPV_FORMAT_DOUBLE;
		data.data = &value;
		do_set_property(handle, &data);
	}

	mpv_observe_property(handle, MPV_PROPERTY_EVENT_METADATA, "metadata", MPV_FORMAT_NODE_MAP);

	return handle;
}

static int output_mpv_play(output_transition_cb_t callback)
{
	play_trans_callback_ = callback;
	if (paused) {
		return output_mpv_pause(0);
	} else {
		const char *cmd[] = {"loadfile", gsuri_, NULL};
		return exec_command(cmd);
	}
}

static void output_mpv_set_uri(const char *uri,
                               output_update_meta_cb_t meta_cb)
{
	Log_info("mpv", "Set uri to '%s'", uri);
	free(gsuri_);
	gsuri_ = (uri && *uri) ? strdup(uri) : NULL;
	meta_update_callback_ = meta_cb;
	SongMetaData_clear(&song_meta_);

	paused = 0;
	output_mpv_play(play_trans_callback_);
}

static int create_mpv_handle()
{
	g_rw_lock_writer_lock(&lock);
	if (locked_mpv_handle != NULL) {
		mpv_destroy(locked_mpv_handle);
	}
	locked_mpv_handle = output_mpv_create();
	g_rw_lock_writer_unlock(&lock);
	if (locked_mpv_handle) {
		return 0;
	} else {
		return 1;
	}
}

static int output_mpv_init(void)
{
	SongMetaData_init(&song_meta_);
	scan_mpv_mime_list();

	setlocale(LC_NUMERIC, "C");
	return create_mpv_handle();
}

struct output_module mpv_output = {
		.shortname = "mpv",
		.description = "Cross-platform media player",
		.add_options = output_mpv_add_options,

		.init        = output_mpv_init,
		.set_uri     = output_mpv_set_uri,
		.set_next_uri= output_mpv_set_next_uri,
		.play        = output_mpv_play,
		.stop        = output_mpv_stop,
		.pause       = output_mpv_pause_wrap,
		.seek        = output_mpv_seek,

		.get_position = output_mpv_get_position,
		.get_volume  = output_mpv_get_volume,
		.set_volume  = output_mpv_set_volume,
		.get_mute  = output_mpv_get_mute,
		.set_mute  = output_mpv_set_mute,
};
