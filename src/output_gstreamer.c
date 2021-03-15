/* output_gstreamer.c - Output module for GStreamer
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 *
 * Adapted to gstreamer-0.10 2006 David Siorpaes
 * Adapted to output to snapcast 2017 Daniel Jäcksch
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

#include <assert.h>
#include <gst/gst.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include "logging.h"
#include "upnp_connmgr.h"
#include "output_module.h"
#include "output_gstreamer.h"

static double buffer_duration = 0.0; /* Buffer disbled by default, see #182 */

static void scan_mime_list(void)
{
	GstRegistry* registry = NULL;

#if (GST_VERSION_MAJOR < 1)
	registry = gst_registry_get_default();
#else
	registry = gst_registry_get();
#endif

	// Fetch a list of all element factories
	GList* features =
		gst_registry_get_feature_list(registry, GST_TYPE_ELEMENT_FACTORY);

	// Save a copy of the list root so we can properly free later
	GList* root = features;

	while (features != NULL) {
		GstPluginFeature* feature = GST_PLUGIN_FEATURE(features->data);

		// Advance list
		features = g_list_next(features);

		// Better be an element factory
		assert(GST_IS_ELEMENT_FACTORY(feature));

		GstElementFactory* factory = GST_ELEMENT_FACTORY(feature);

		// Ignore elements without pads
		if (gst_element_factory_get_num_pad_templates(factory) == 0) continue;

		// Fetch a list of all pads
		const GList* pads = gst_element_factory_get_static_pad_templates(factory);

		while (pads) {
			GstStaticPadTemplate* padTemplate = (GstStaticPadTemplate*)pads->data;

			// Advance list
			pads = g_list_next(pads);

			// Skip pads that aren't sinks
			if (padTemplate->direction != GST_PAD_SINK) continue;

			// This is literally all known pad presences so it should be OK!
			assert(padTemplate->presence == GST_PAD_ALWAYS ||
				   padTemplate->presence == GST_PAD_SOMETIMES ||
				   padTemplate->presence == GST_PAD_REQUEST);

			GstCaps* capabilities = gst_static_caps_get(&padTemplate->static_caps);

			// Skip capabilities that they tell us nothing
			if (capabilities == NULL || gst_caps_is_any(capabilities) ||
				gst_caps_is_empty(capabilities))
			{
				gst_caps_unref(capabilities);
				continue;
			}

			for (guint i = 0; i < gst_caps_get_size(capabilities); i++) {
				GstStructure* structure = gst_caps_get_structure(capabilities, i);

				register_mime_type(gst_structure_get_name(structure));
			}

			gst_caps_unref(capabilities);
		}
	}

	// Free any allocated memory
	gst_plugin_feature_list_free(root);

	// There seem to be all kinds of mime types out there that start with
	// "audio/" but are not explicitly supported by gstreamer. Let's just
	// tell the controller that we can handle everything "audio/*" and hope
	// for the best.
	register_mime_type("audio/*");
}

static GstElement *player_ = NULL;
static char *gsuri_ = NULL;         // locally strdup()ed
static char *gs_next_uri_ = NULL;   // locally strdup()ed
static struct SongMetaData song_meta_;

static output_transition_cb_t play_trans_callback_ = NULL;
static output_update_meta_cb_t meta_update_callback_ = NULL;

struct track_time_info {
	gint64 duration;
	gint64 position;
};
static struct track_time_info last_known_time_ = {0, 0};

static GstState get_current_player_state() {
	GstState state = GST_STATE_PLAYING;
	GstState pending = GST_STATE_NULL;
	gst_element_get_state(player_, &state, &pending, 0);
	return state;
}

static void output_gstreamer_set_next_uri(const char *uri) {
	Log_info("gstreamer", "Set next uri to '%s'", uri);
	free(gs_next_uri_);
	gs_next_uri_ = (uri && *uri) ? strdup(uri) : NULL;
}

static void output_gstreamer_set_uri(const char *uri,
				     output_update_meta_cb_t meta_cb) {
	Log_info("gstreamer", "Set uri to '%s'", uri);
	free(gsuri_);
	gsuri_ = (uri && *uri) ? strdup(uri) : NULL;
	meta_update_callback_ = meta_cb;
	SongMetaData_clear(&song_meta_);
}

static int output_gstreamer_play(output_transition_cb_t callback) {
	play_trans_callback_ = callback;
	if (get_current_player_state() != GST_STATE_PAUSED) {
		if (gst_element_set_state(player_, GST_STATE_READY) ==
		    GST_STATE_CHANGE_FAILURE) {
			Log_error("gstreamer", "setting play state failed (1)");
			// Error, but continue; can't get worse :)
		}
		g_object_set(G_OBJECT(player_), "uri", gsuri_, NULL);
	}
	if (gst_element_set_state(player_, GST_STATE_PLAYING) ==
	    GST_STATE_CHANGE_FAILURE) {
		Log_error("gstreamer", "setting play state failed (2)");
		return -1;
	}
	return 0;
}

static int output_gstreamer_stop(void) {
	if (gst_element_set_state(player_, GST_STATE_READY) ==
	    GST_STATE_CHANGE_FAILURE) {
		return -1;
	} else {
		return 0;
	}
}

static int output_gstreamer_pause(void) {
	if (gst_element_set_state(player_, GST_STATE_PAUSED) ==
	    GST_STATE_CHANGE_FAILURE) {
		return -1;
	} else {
		return 0;
	}
}

static int output_gstreamer_seek(gint64 position_nanos) {
	if (gst_element_seek(player_, 1.0, GST_FORMAT_TIME,
			     GST_SEEK_FLAG_FLUSH,
			     GST_SEEK_TYPE_SET, position_nanos,
			     GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
		return -1;
	} else {
		return 0;
	}
}

#if 0
static const char *gststate_get_name(GstState state)
{
	switch(state) {
	case GST_STATE_VOID_PENDING:
		return "VOID_PENDING";
	case GST_STATE_NULL:
		return "NULL";
	case GST_STATE_READY:
		return "READY";
	case GST_STATE_PAUSED:
		return "PAUSED";
	case GST_STATE_PLAYING:
		return "PLAYING";
	default:
		return "Unknown";
	}
}
#endif

// This is crazy. I want C++ :)
struct MetaModify {
	struct SongMetaData *meta;
	int any_change;
};

static void MetaModify_add_tag(const GstTagList *list, const gchar *tag,
			       gpointer user_data) {
	struct MetaModify *data = (struct MetaModify*) user_data;
	const char **destination = NULL;
	if (strcmp(tag, GST_TAG_TITLE) == 0) {
		destination = &data->meta->title;
	} else if (strcmp(tag, GST_TAG_ARTIST) == 0) {
		destination = &data->meta->artist;
	} else if (strcmp(tag, GST_TAG_ALBUM) == 0) {
		destination = &data->meta->album;
	} else if (strcmp(tag, GST_TAG_GENRE) == 0) {
		destination = &data->meta->genre;
	} else if (strcmp(tag, GST_TAG_COMPOSER) == 0) {
		destination = &data->meta->composer;
	}
	if (destination != NULL) {
		char *replace = NULL;
		gst_tag_list_get_string(list, tag, &replace);
		if (replace != NULL &&
		    (*destination == NULL
		     || strcmp(replace, *destination) != 0)) {
			free((char*)*destination);
			*destination = replace;
			data->any_change++;
		} else {
			free(replace);
		}
	}
}

static gboolean my_bus_callback(GstBus * bus, GstMessage * msg,
				gpointer data)
{
	(void)bus;
	(void)data;

	GstMessageType msgType;
	const GstObject *msgSrc;
	const gchar *msgSrcName;

	msgType = GST_MESSAGE_TYPE(msg);
	msgSrc = GST_MESSAGE_SRC(msg);
	msgSrcName = GST_OBJECT_NAME(msgSrc);

	switch (msgType) {
	case GST_MESSAGE_EOS:
		Log_info("gstreamer", "%s: End-of-stream", msgSrcName);
		if (gs_next_uri_ != NULL) {
			// If playbin does not support gapless (old
			// versions didn't), this will trigger.
			free(gsuri_);
			gsuri_ = gs_next_uri_;
			gs_next_uri_ = NULL;
			gst_element_set_state(player_, GST_STATE_READY);
			g_object_set(G_OBJECT(player_), "uri", gsuri_, NULL);
			gst_element_set_state(player_, GST_STATE_PLAYING);
			if (play_trans_callback_) {
				play_trans_callback_(PLAY_STARTED_NEXT_STREAM);
			}
		} else if (play_trans_callback_) {
			play_trans_callback_(PLAY_STOPPED);
		}
		break;

	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *err;

		gst_message_parse_error(msg, &err, &debug);

		Log_error("gstreamer", "%s: Error: %s (Debug: %s)",
			  msgSrcName, err->message, debug);
		g_error_free(err);
		g_free(debug);

		break;
	}
	case GST_MESSAGE_STATE_CHANGED: {
		GstState oldstate, newstate, pending;
		gst_message_parse_state_changed(msg, &oldstate, &newstate,
						&pending);
		/*
		g_print("GStreamer: %s: State change: '%s' -> '%s', "
			"PENDING: '%s'\n", msgSrcName,
			gststate_get_name(oldstate),
			gststate_get_name(newstate),
			gststate_get_name(pending));
		*/
		break;
	}

	case GST_MESSAGE_TAG: {
		GstTagList *tags = NULL;

		if (meta_update_callback_ != NULL) {
			gst_message_parse_tag(msg, &tags);
			/*g_print("GStreamer: Got tags from element %s\n",
				GST_OBJECT_NAME (msg->src));
			*/
			struct MetaModify modify;
			modify.meta = &song_meta_;
			modify.any_change = 0;
			gst_tag_list_foreach(tags, &MetaModify_add_tag, &modify);
			gst_tag_list_free(tags);
			if (modify.any_change) {
				meta_update_callback_(&song_meta_);
			}
		}
		break;
	}

	case GST_MESSAGE_BUFFERING:
        {
                if (buffer_duration <= 0.0) break;  /* nothing to buffer */

                gint percent = 0;
                gst_message_parse_buffering (msg, &percent);


                /* Pause playback until buffering is complete. */
                if (percent < 100)
                        gst_element_set_state(player_, GST_STATE_PAUSED);
                else
                        gst_element_set_state(player_, GST_STATE_PLAYING);
		break;
        }
	default:
		/*
		g_print("GStreamer: %s: unhandled message type %d (%s)\n",
		        msgSrcName, msgType, gst_message_type_get_name(msgType));
		*/
		break;
	}

	return TRUE;
}

static gchar *audio_sink = NULL;
static gchar *audio_device = NULL;
static gchar *audio_pipe = NULL;
static gchar *video_sink = NULL;
static gchar *video_pipe = NULL;
static double initial_db = 0.0;

/* Options specific to output_gstreamer */
static GOptionEntry option_entries[] = {
        { "gstout-audiosink", 0, 0, G_OPTION_ARG_STRING, &audio_sink,
          "GStreamer audio sink to use "
	  "(autoaudiosink, alsasink, osssink, esdsink, ...)",
	  NULL },
        { "gstout-audiodevice", 0, 0, G_OPTION_ARG_STRING, &audio_device,
          "GStreamer device for the given audiosink. ",
	  NULL },
        { "gstout-audiopipe", 0, 0, G_OPTION_ARG_STRING, &audio_pipe,
          "GStreamer audio sink to pipeline"
          "(gst-launch format) useful for further output format conversion.",
	  NULL },
        { "gstout-videosink", 0, 0, G_OPTION_ARG_STRING, &video_sink,
          "GStreamer video sink to use "
	  "(autovideosink, xvimagesink, ximagesink, ...)",
	  NULL },
        { "gstout-videopipe", 0, 0, G_OPTION_ARG_STRING, &video_pipe,
          "GStreamer video sink to pipeline"
          "(gst-launch format) useful for further output format conversion.",
	  NULL },
        { "gstout-buffer-duration", 0, 0, G_OPTION_ARG_DOUBLE, &buffer_duration,
          "The size of the buffer in seconds. Set to zero to disable buffering.",
          NULL },
        { "gstout-initial-volume-db", 0, 0, G_OPTION_ARG_DOUBLE, &initial_db,
          "GStreamer initial volume in decibel (e.g. 0.0 = max; -6 = 1/2 max) ",
	  NULL },
        { NULL }
};


static int output_gstreamer_add_options(GOptionContext *ctx)
{
	GOptionGroup *option_group;
	option_group = g_option_group_new("gstout", "GStreamer Output Options",
	                                  "Show GStreamer Output Options",
	                                  NULL, NULL);
	g_option_group_add_entries(option_group, option_entries);

	g_option_context_add_group (ctx, option_group);

	g_option_context_add_group (ctx, gst_init_get_option_group ());
	return 0;
}

static int output_gstreamer_get_position(gint64 *track_duration,
					 gint64 *track_pos) {
	*track_duration = last_known_time_.duration;
	*track_pos = last_known_time_.position;

	int rc = 0;
	if (get_current_player_state() != GST_STATE_PLAYING) {
		return rc;  // playbin2 only returns valid values then.
	}
#if (GST_VERSION_MAJOR < 1)
	GstFormat fmt = GST_FORMAT_TIME;
	GstFormat* query_type = &fmt;
#else
	GstFormat query_type = GST_FORMAT_TIME;
#endif
	if (!gst_element_query_duration(player_, query_type, track_duration)) {
		Log_error("gstreamer", "Failed to get track duration.");
		rc = -1;
	}
	if (!gst_element_query_position(player_, query_type, track_pos)) {
		Log_error("gstreamer", "Failed to get track pos");
		rc = -1;
	}
	// playbin2 does not allow to query while paused. Remember in case
	// we're asked then (it actually returns something, but it is bogus).
	last_known_time_.duration = *track_duration;
	last_known_time_.position = *track_pos;
	return rc;
}

static int output_gstreamer_get_volume(float *v) {
	double volume;
	g_object_get(player_, "volume", &volume, NULL);
	Log_info("gstreamer", "Query volume fraction: %f", volume);
	*v = volume;
	return 0;
}
static int output_gstreamer_set_volume(float value) {
	Log_info("gstreamer", "Set volume fraction to %f", value);
	g_object_set(player_, "volume", (double) value, NULL);
	return 0;
}
static int output_gstreamer_get_mute(int *m) {
	gboolean val;
	g_object_get(player_, "mute", &val, NULL);
	*m = val;
	return 0;
}
static int output_gstreamer_set_mute(int m) {
	Log_info("gstreamer", "Set mute to %s", m ? "on" : "off");
	g_object_set(player_, "mute", (gboolean) m, NULL);
	return 0;
}

static void prepare_next_stream(GstElement *obj, gpointer userdata) {
	(void)obj;
	(void)userdata;

	Log_info("gstreamer", "about-to-finish cb: setting uri %s",
		 gs_next_uri_);
	free(gsuri_);
	gsuri_ = gs_next_uri_;
	gs_next_uri_ = NULL;
	if (gsuri_ != NULL) {
		g_object_set(G_OBJECT(player_), "uri", gsuri_, NULL);
		if (play_trans_callback_) {
			// TODO(hzeller): can we figure out when we _actually_
			// start playing this ? there are probably a couple
			// of seconds between now and actual start.
			play_trans_callback_(PLAY_STARTED_NEXT_STREAM);
		}
	}
}

static int output_gstreamer_init(void)
{
	GstBus *bus;

	SongMetaData_init(&song_meta_);
	scan_mime_list();

#if (GST_VERSION_MAJOR < 1)
	const char player_element_name[] = "playbin2";
#else
	const char player_element_name[] = "playbin";
#endif

	player_ = gst_element_factory_make(player_element_name, "play");
	assert(player_ != NULL);

        /* set buffer size */
        if (buffer_duration > 0) {
                gint64 buffer_duration_ns = round(buffer_duration * 1.0e9);
                Log_info("gstreamer",
                         "Setting buffer duration to %" PRId64 "ms",
                         buffer_duration_ns / 1000000);
                g_object_set(G_OBJECT(player_),
                             "buffer-duration",
                             buffer_duration_ns,
                             NULL);
        } else {
                Log_info("gstreamer",
			 "Buffering disabled (--gstout-buffer-duration)");
        }

	bus = gst_pipeline_get_bus(GST_PIPELINE(player_));
	gst_bus_add_watch(bus, my_bus_callback, NULL);
	gst_object_unref(bus);

	if (audio_sink != NULL && audio_pipe != NULL) {
		Log_error("gstreamer", "--gstout-audosink and --gstout-audiopipe are mutually exclusive.");
		return 1;
	}
	if (video_sink != NULL && video_pipe != NULL) {
		Log_error("gstreamer", "--gstout-videosink and --gstout-videopipe are mutually exclusive.");
		return 1;
	}

	if (audio_sink != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting audio sink to %s; device=%s\n",
			 audio_sink, audio_device ? audio_device : "");
		sink = gst_element_factory_make (audio_sink, "sink");
		if (sink == NULL) {
		  Log_error("gstreamer", "Couldn't create sink '%s'",
			    audio_sink);
		} else {
		  if (audio_device != NULL) {
		    g_object_set (G_OBJECT(sink), "device", audio_device, NULL);
		  }
		  g_object_set (G_OBJECT (player_), "audio-sink", sink, NULL);
		}
	}
	if (audio_pipe != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting audio sink-pipeline to %s\n",audio_pipe);
		sink = gst_parse_bin_from_description(audio_pipe, TRUE, NULL);

		if (sink == NULL) {
			Log_error("gstreamer", "Could not create pipeline.");
		} else {
			g_object_set (G_OBJECT (player_), "audio-sink", sink, NULL);
		}
	}
	if (video_sink != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting video sink to %s", video_sink);
		sink = gst_element_factory_make (video_sink, "sink");
		g_object_set (G_OBJECT (player_), "video-sink", sink, NULL);
	}
	if (video_pipe != NULL) {
		GstElement *sink = NULL;
		Log_info("gstreamer", "Setting video sink-pipeline to %s\n", video_pipe);
		sink = gst_parse_bin_from_description(video_pipe, TRUE, NULL);

		if (sink == NULL) {
			Log_error("gstreamer", "Could not create pipeline.");
		} else {
			g_object_set (G_OBJECT (player_), "video-sink", sink, NULL);
		}
	}

	if (gst_element_set_state(player_, GST_STATE_READY) ==
	    GST_STATE_CHANGE_FAILURE) {
		Log_error("gstreamer", "Error: pipeline doesn't become ready.");
	}

	g_signal_connect(G_OBJECT(player_), "about-to-finish",
			 G_CALLBACK(prepare_next_stream), NULL);
	output_gstreamer_set_mute(0);
	if (initial_db < 0) {
		output_gstreamer_set_volume(exp(initial_db / 20 * log(10)));
	}

	return 0;
}

struct output_module gstreamer_output = {
        .shortname = "gst",
	.description = "GStreamer multimedia framework",
	.add_options = output_gstreamer_add_options,

	.init        = output_gstreamer_init,
	.set_uri     = output_gstreamer_set_uri,
	.set_next_uri= output_gstreamer_set_next_uri,
	.play        = output_gstreamer_play,
	.stop        = output_gstreamer_stop,
	.pause       = output_gstreamer_pause,
	.seek        = output_gstreamer_seek,

	.get_position = output_gstreamer_get_position,
	.get_volume  = output_gstreamer_get_volume,
	.set_volume  = output_gstreamer_set_volume,
	.get_mute  = output_gstreamer_get_mute,
	.set_mute  = output_gstreamer_set_mute,
};
