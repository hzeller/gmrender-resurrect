// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output_gstreamer.c - Output module for GStreamer
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 * Copyright (C) 2019        Tucker Kern
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

#include "logging.h"
#include "output_gstreamer.h"
#include "output_module.h"
#include "upnp_connmgr.h"

static constexpr const char* TAG = "gstreamer";

/**
  @brief  Return the command line options assocaited with the output module

  @param  void
  @retval std::vector<GOptionGroup*>
*/
std::vector<GOptionGroup*> GstreamerOutput::Options::GetOptionGroups() {
  std::vector<GOptionGroup*> optionGroups;

  GOptionGroup* option_group =
      g_option_group_new("gstout", "GStreamer Output Options",
                         "Show GStreamer Output Options", NULL, NULL);

  GOptionEntry option_entries[] = {
      {"gstout-audiosink", 0, 0, G_OPTION_ARG_STRING, &audio_sink,
       "GStreamer audio sink to use (autoaudiosink, alsasink, osssink, "
       "esdsink, ...)",
       NULL},

      {"gstout-audiodevice", 0, 0, G_OPTION_ARG_STRING, &audio_device,
       "GStreamer device for the given audiosink. ", NULL},

      {"gstout-audiopipe", 0, 0, G_OPTION_ARG_STRING, &audio_pipe,
       "GStreamer audio sink to pipeline (gst-launch format) useful for "
       "further output format conversion.",
       NULL},

      {"gstout-videosink", 0, 0, G_OPTION_ARG_STRING, &video_sink,
       "GStreamer video sink to use (autovideosink, xvimagesink, ximagesink, "
       "...)",
       NULL},

      {"gstout-buffer-duration", 0, 0, G_OPTION_ARG_DOUBLE, &buffer_duration,
       "The size of the buffer in seconds. Set to zero to disable buffering.",
       NULL},

      {"gstout-initial-volume-db", 0, 0, G_OPTION_ARG_DOUBLE, &initial_db,
       "GStreamer initial volume in decibel (e.g. 0.0 = max; -6 = 1/2 max) ",
       NULL},
      {NULL}};

  g_option_group_add_entries(option_group, option_entries);

  optionGroups.push_back(option_group);
  optionGroups.push_back(gst_init_get_option_group());

  return optionGroups;
}

/**
  @brief  Initialize the output module

  @param  opts GstreamerOuptut::Options to initalize module with
  @retval Result
*/
OutputModule::Result GstreamerOutput::Initalize(
    GstreamerOutput::Options& opts) {
  // Check if Gstreamer is initalized. It should be if the options were properly
  // loaded
  if (gst_is_initialized() == false) {
    Log_warn(TAG, "Initalizing Gstreamer without options.");

    GError* error = NULL;
    if (gst_init_check(NULL, NULL, &error) == false) {
      Log_error(TAG, "Failed to initalize Gstreamer. Error: %s",
                error->message);
      g_error_free(error);

      return OutputModule::kError;
    }
  }

  // Save a copy of the options
  options_ = opts;

  if (options_.audio_sink != NULL && options_.audio_pipe != NULL) {
    Log_error(TAG,
        "--gstout-audosink and --gstout-audiopipe are mutually exclusive.");
    return OutputModule::kError;
  }

#if (GST_VERSION_MAJOR < 1)
  const char player_element_name[] = "playbin2";
#else
  const char player_element_name[] = "playbin";
#endif

  player_ = gst_element_factory_make(player_element_name, "play");
  if (player_ == NULL) return OutputModule::kError;

  // Configure buffering if enabled
  if (options_.buffer_duration > 0) {
    int64_t buffer_duration_ns = round(options_.buffer_duration * 1.0e9);
    Log_info(TAG, "Setting buffer duration to %ldms",
             buffer_duration_ns / 1000000);

    g_object_set(G_OBJECT(player_), "buffer-duration",
                 (gint64)buffer_duration_ns, NULL);
  } else {
    Log_info(TAG, "Buffering disabled (--gstout-buffer-duration)");
  }

  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(player_));

  // Attach a callback to the bus via lambda function
  gst_bus_add_watch(bus,
                    [](GstBus* b, GstMessage* m, gpointer d) -> gboolean {
                      return ((GstreamerOutput*)d)->BusCallback(m);
                    },
                    this);

  gst_object_unref(bus);

  // Configure audio sink
  GstElement* asink = NULL;
  if (options_.audio_sink) {
    Log_info(TAG, "Setting audio sink to '%s'; device=%s\n",
             options_.audio_sink,
             options_.audio_device ? options_.audio_device : "");

    asink = gst_element_factory_make(options_.audio_sink, "sink");
    if (asink == NULL) Log_error(TAG, "Could not create sink.");
  } else if (options_.audio_pipe) {
    Log_info(TAG, "Setting audio sink-pipeline to '%s'\n", options_.audio_pipe);

    asink = gst_parse_bin_from_description(options_.audio_pipe, TRUE, NULL);
    if (asink == NULL) Log_error(TAG, "Could not create pipeline.");
  }

  if (asink != NULL) {
    // Add the audio device if it exists
    if (options_.audio_device != NULL)
      g_object_set(G_OBJECT(asink), "device", options_.audio_device, NULL);

    g_object_set(G_OBJECT(player_), "audio-sink", asink, NULL);
  }

  // Configure video sink
  if (options_.video_sink != NULL) {
    Log_info(TAG, "Setting video sink to '%s'", options_.video_sink);

    GstElement* vsink = gst_element_factory_make(options_.video_sink, "sink");
    if (vsink == NULL)
      Log_error(TAG, "Could not create sink.");
    else
      g_object_set(G_OBJECT(player_), "video-sink", vsink, NULL);
  }

  if (gst_element_set_state(player_, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE)
    Log_error(TAG, "Pipeline doesn't become ready.");

  // Typedef a function pointer of the about-to-finish callback
  typedef void (*CallbackType)(GstElement*, gpointer);

  // Attach a callback to the about-to-finish event via a lambda
  g_signal_connect(
      G_OBJECT(player_), "about-to-finish",
      G_CALLBACK((CallbackType)[](GstElement * o, gpointer d)->void {
        ((GstreamerOutput*)d)->NextStream();
      }),
      this);

  // Turn mute off on the output
  SetMute(false);

  // Set initial volume
  if (options_.initial_db < 0)
    SetVolume(exp(options_.initial_db / 20 * log(10)));

  return OutputModule::kSuccess;
}

/**
  @brief  Get all the media types supported by this output

  @param  none
  @retval Output::MimeTypeSet
*/
Output::MimeTypeSet GstreamerOutput::GetSupportedMedia(void) {
  GstRegistry* registry = NULL;

#if (GST_VERSION_MAJOR < 1)
  registry = gst_registry_get_default();
#else
  registry = gst_registry_get();
#endif

  Output::MimeTypeSet mime_types;

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
          gst_caps_is_empty(capabilities)) {
        gst_caps_unref(capabilities);
        continue;
      }

      for (guint i = 0; i < gst_caps_get_size(capabilities); i++) {
        GstStructure* structure = gst_caps_get_structure(capabilities, i);

        mime_types.emplace(gst_structure_get_name(structure));
      }

      gst_caps_unref(capabilities);
    }
  }

  // Free the feature list
  gst_plugin_feature_list_free(root);

  return mime_types;
}

/**
  @brief  Set the URI of the Gstreamer playback module

  @param  uri URI to set
  @retval none
*/
void GstreamerOutput::SetUri(const std::string& uri) {
  Log_info(TAG, "Set uri to '%s'", uri.c_str());

  uri_ = uri;
}

/**
  @brief  Set the next URI of the Gstreamer playback module

  @param  uri URI to set
  @retval none
*/
void GstreamerOutput::SetNextUri(const std::string& uri) {
  Log_info(TAG, "Set next uri to '%s'", uri.c_str());

  next_uri_ = uri;
}

/**
  @brief  Start playback

  @param  none
  @retval Result
*/
OutputModule::Result GstreamerOutput::Play(void) {
  if (GetPlayerState() != GST_STATE_PAUSED) {
    if (gst_element_set_state(player_, GST_STATE_READY) ==
        GST_STATE_CHANGE_FAILURE)
      Log_error(TAG, "setting play state failed (1)");  // Error, but continue;
                                                        // can't get worse :)

    g_object_set(G_OBJECT(player_), "uri", uri_.c_str(), NULL);
  }

  if (gst_element_set_state(player_, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    Log_error(TAG, "setting play state failed (2)");
    return OutputModule::kError;
  }

  return OutputModule::kSuccess;
}

/**
  @brief  Stop playback

  @param  none
  @retval Result
*/
OutputModule::Result GstreamerOutput::Stop(void) {
  if (gst_element_set_state(player_, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE)
    return OutputModule::kError;

  return OutputModule::kSuccess;
}

/**
  @brief  Pause playback

  @param  none
  @retval Result
*/
OutputModule::Result GstreamerOutput::Pause(void) {
  if (gst_element_set_state(player_, GST_STATE_PAUSED) ==
      GST_STATE_CHANGE_FAILURE)
    return OutputModule::kError;

  return OutputModule::kSuccess;
}

/**
  @brief  Seek player to supplied time position

  @param  position_ns Seek position in nanoseconds
  @retval Result
*/
OutputModule::Result GstreamerOutput::Seek(int64_t position_ns) {
  if (gst_element_seek(player_, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                       GST_SEEK_TYPE_SET, (gint64)position_ns,
                       GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE))
    return OutputModule::kSuccess;

  return OutputModule::kError;
}

/**
  @brief  Get the duration and position of the current track

  @param  track track_state_t to populate with duration and position information
  @retval Result
*/
OutputModule::Result GstreamerOutput::GetPosition(TrackState* track) {
  // Validate output pointer
  if (track == nullptr) return OutputModule::kError;

  // Can't query position yet
  if (GetPlayerState() <= GST_STATE_READY) return OutputModule::kError;

#if (GST_VERSION_MAJOR < 1)
  static TrackState last_state;

  // playbin2 does not allow to query while paused, return last known state
  *track = last_state;
  if (GetPlayerState() != GST_STATE_PLAYING) return OutputModule::kSuccess;

  GstFormat fmt = GST_FORMAT_TIME;
  GstFormat* query_type = &fmt;
#else
  GstFormat query_type = GST_FORMAT_TIME;
#endif

  OutputModule::Result result = OutputModule::kSuccess;
  if (!gst_element_query_duration(player_, query_type,
                                  (gint64*)&track->duration_ns)) {
    Log_warn(TAG, "Failed to get track duration.");
    result = OutputModule::kError;
  }

  if (!gst_element_query_position(player_, query_type,
                                  (gint64*)&track->position_ns)) {
    Log_warn(TAG, "Failed to get track position.");
    result = OutputModule::kError;
  }

#if (GST_VERSION_MAJOR < 1)
  // Update last known state
  last_state = *track;
#endif

  return result;
}

/**
  @brief  Get the volume of the Gstreamer output

  @param  volume Current volume (0.0 - 1.0)
  @retval Result
*/
OutputModule::Result GstreamerOutput::GetVolume(float* volume) {
  // Validate output pointer
  if (volume == nullptr) return OutputModule::kError;

  double vol = 0;
  g_object_get(player_, "volume", &vol, NULL);

  *volume = (float)vol;

  Log_info(TAG, "Query volume fraction: %f", vol);

  return OutputModule::kSuccess;
}

/**
  @brief  Set the volume of the Gstreamer output

  @param  volume Desired volume (0.0 - 1.0)
  @retval Result
*/
OutputModule::Result GstreamerOutput::SetVolume(float volume) {
  Log_info(TAG, "Set volume fraction to %f", volume);

  g_object_set(player_, "volume", (double)volume, NULL);

  return OutputModule::kSuccess;
}

/**
  @brief  Get the mute state of the Gstreamer output

  @param  bool Mute state
  @retval Result
*/
OutputModule::Result GstreamerOutput::GetMute(bool* mute) {
  // Validate output pointer
  if (mute == nullptr) return OutputModule::kError;

  gboolean val = false;
  g_object_get(player_, "mute", &val, NULL);

  *mute = (bool)val;

  return OutputModule::kSuccess;
}

/**
  @brief  Set the mute on the Gstreamer output

  @param  mute Mute state
  @retval Result
*/
OutputModule::Result GstreamerOutput::SetMute(bool mute) {
  g_object_set(player_, "mute", (gboolean)mute, NULL);

  return OutputModule::kSuccess;
}

/**
  @brief  Get the current state of the Gstreamer player

  @param  none
  @retval GstState
*/
GstState GstreamerOutput::GetPlayerState(void) {
  GstState state = GST_STATE_PLAYING;  // TODO(Tucker) default to playing?
  gst_element_get_state(player_, &state, NULL, 0);

  return state;
}

/**
  @brief  Sets the next stream for playback. Triggered by the "about-to-finish"
  signal

  @param  none
  @retval void
*/
void GstreamerOutput::NextStream(void) {
  Log_info(TAG, "about-to-finish cb: set uri to '%s'", next_uri_.c_str());

  // Swap contents of next URI into current URI
  uri_.swap(next_uri_);

  // Cear next URI so we don't repeat
  next_uri_.clear();

  if (uri_.length() > 0) {
    g_object_set(G_OBJECT(player_), "uri", uri_.c_str(), NULL);

    // TODO(hzeller): can we figure out when we _actually_ start playing this?
    // There are probably a couple of seconds between now and actual start.
    NotifyPlaybackUpdate(Output::State::kStartedNextStream);
  }
}

/**
  @brief  Handle message from the Gstreamer bus

  @param  message GstMessage to process
  @retval bool - Message handled
*/
bool GstreamerOutput::BusCallback(GstMessage* message) {
  switch (message->type) {
    case GST_MESSAGE_EOS: {
      Log_info(TAG, "%s: End-of-stream", message->src->name);

      if (next_uri_.length() > 0) {
        // If playbin does not support gapless (old versions didn't), this will
        // trigger.

        // Swap contents of next URI into current URI
        uri_.swap(next_uri_);

        // Cear next URI so we don't repeat
        next_uri_.clear();

        gst_element_set_state(player_, GST_STATE_READY);

        g_object_set(G_OBJECT(player_), "uri", uri_.c_str(), NULL);

        gst_element_set_state(player_, GST_STATE_PLAYING);

        NotifyPlaybackUpdate(Output::State::kStartedNextStream);
      } else
        NotifyPlaybackUpdate(Output::State::kPlaybackStopped);

      break;
    }

    case GST_MESSAGE_ERROR: {
      GError* err = NULL;
      gchar* debug = NULL;
      gst_message_parse_error(message, &err, &debug);

      Log_error(TAG, "%s: Error: %s (Debug: %s)", message->src->name,
                err->message, debug);

      g_error_free(err);
      g_free(debug);

      break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
#ifdef DEBUG_STATE_CHANGE
      GstState oldstate, newstate, pending;
      gst_message_parse_state_changed(message, &oldstate, &newstate, &pending);

      Log_info(TAG,
               "Source: %s: State change: '%s' -> '%s', "
               "PENDING: '%s'\n",
               message->src->name, gststate_get_name(oldstate),
               gststate_get_name(newstate), gststate_get_name(pending));
#endif
      break;
    }

    case GST_MESSAGE_TAG: {
      // Nothing to do
      if (metadata_callback_ == NULL) break;

      GstTagList* tag_list = NULL;
      gst_message_parse_tag(message, &tag_list);

      auto attemptTagUpdate = [tag_list](std::string& tag,
                                         const char* tag_name) -> bool {
        // Attempt to fetch the tag
        gchar* value = NULL;
        if (gst_tag_list_get_string(tag_list, tag_name, &value) == false)
          return false;

        if (tag.compare(value) == 0) {
          // Identical tags
          g_free(value);
          return false;
        }

        tag = value;

        // Free the tag buffer
        g_free(value);

        // Log_info(TAG, "Got tag: '%s' value: '%s'", tag_name, tag.c_str());

        return true;
      };

      bool notify = false;

      notify |= attemptTagUpdate(metadata.title, GST_TAG_TITLE);
      notify |= attemptTagUpdate(metadata.artist, GST_TAG_ARTIST);
      notify |= attemptTagUpdate(metadata.album, GST_TAG_ALBUM);
      notify |= attemptTagUpdate(metadata.genre, GST_TAG_GENRE);
      notify |= attemptTagUpdate(metadata.composer, GST_TAG_COMPOSER);

      if (notify) NotifyMetadataChange(metadata);

      gst_tag_list_free(tag_list);

      break;
    }

    case GST_MESSAGE_BUFFERING: {
      if (options_.buffer_duration <= 0.0) break;  // Buffering disabled

      gint percent = 0;
      gst_message_parse_buffering(message, &percent);

      /* Pause playback until buffering is complete. */
      if (percent < 100)
        gst_element_set_state(player_, GST_STATE_PAUSED);
      else
        gst_element_set_state(player_, GST_STATE_PLAYING);

      break;
    }

    default:
      break;
  }

  return true;
}