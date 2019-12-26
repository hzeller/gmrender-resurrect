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

class GstreamerOutput : public OutputModule,
                        public OutputModuleFactory<GstreamerOutput> {
 public:
  struct Options : public OutputModule::Options {
    // Let GstreamerOutput access protected constructor
    friend class GstreamerOutput;

    std::vector<GOptionGroup*> GetOptionGroups(void);

    static Options& Get() {
      static Options options;
      return options;
    }

    const char* audio_sink = nullptr;
    const char* audio_device = nullptr;
    const char* audio_pipe = nullptr;
    const char* video_sink = nullptr;
    double initial_db = 0.0;
    double buffer_duration = 0.0;  // Buffer disbled by default, see #182

   protected:
    Options() {}                       // Hide away the constructor
    Options(const Options&) = delete;  // Delete copy constructor
  };

  GstreamerOutput(Output::PlaybackCallback play = nullptr,
                  Output::MetadataCallback meta = nullptr)
      : OutputModule(play, meta) {}

  Result Initalize(GstreamerOutput::Options& options);

  Result Initalize(OutputModule::Options& options) {
    return this->Initalize((GstreamerOutput::Options&)options);
  }

  Output::MimeTypeSet GetSupportedMedia(void);

  void SetUri(const std::string& uri);
  void SetNextUri(const std::string& uri);

  Result Play(void);
  Result Stop(void);
  Result Pause(void);
  Result Seek(int64_t position_ns);

  Result GetPosition(TrackState& track);
  Result GetVolume(float& volume);
  Result SetVolume(float volume);
  Result GetMute(bool& mute);
  Result SetMute(bool mute);

 private:
  GstState GetPlayerState(void);
  void NextStream(void);
  bool BusCallback(GstMessage* message);

  GstElement* player_ = nullptr;

  std::string uri_;
  std::string next_uri_;

  GstreamerOutput::Options options_;
};

#endif
