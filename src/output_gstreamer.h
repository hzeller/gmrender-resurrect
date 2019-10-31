// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output_gstreamer.h - Definitions for GStreamer output module
 *
 * Copyright (C) 2019   Tucker Kern
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

#ifndef _OUTPUT_GSTREAMER_H
#define _OUTPUT_GSTREAMER_H

#include <gst/gst.h>

#include "output_module.h"

class GstreamerOutput : public OutputModule, public OutputModuleFactory<GstreamerOutput>
{
  public:
    struct Options : public OutputModule::Options
    {
      // Let GstreamerOutput access protected constructor
      friend class GstreamerOutput;

      char* audio_sink = nullptr;
      char* audio_device = nullptr;
      char* audio_pipe = nullptr;
      char* video_sink = nullptr;
      double initial_db = 0.0;
      double buffer_duration = 0.0; // Buffer disbled by default, see #182

      std::vector<GOptionGroup*> get_option_groups(void);

      static Options& get()
      {
        static Options options;
        return options;
      }

      protected:
        Options() {} // Hide away the constructor
        Options(const Options&) = delete; // Delete copy constructor
    };

    GstreamerOutput(Output::playback_callback_t play = nullptr, Output::metadata_callback_t meta = nullptr) : OutputModule(play, meta) {}
    
    result_t initalize(GstreamerOutput::Options& options);

    result_t initalize(OutputModule::Options& options)
    {
      return this->initalize((GstreamerOutput::Options&) options);
    }

    Output::mime_type_set_t get_supported_media(void);

    void set_uri(const std::string &uri);
    void set_next_uri(const std::string &uri);

    result_t play(void);
    result_t stop(void);
    result_t pause(void);
    result_t seek(int64_t position_ns);

    result_t get_position(track_state_t& position);
    result_t get_volume(float& volume);
    result_t set_volume(float volume);
    result_t get_mute(bool& mute);
    result_t set_mute(bool mute);

  private:
    GstElement* player = nullptr;

    std::string uri;
    std::string next_uri;

    GstreamerOutput::Options options;

    GstState get_player_state(void);
    void next_stream(void);
    bool bus_callback(GstMessage* message);
};

#endif
