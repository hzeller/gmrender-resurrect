// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output_module.h - Output module interface definition
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

#ifndef _OUTPUT_MODULE_H
#define _OUTPUT_MODULE_H

#include <set>
#include <string>
#include <vector>

#include "output.h"

class OutputModule {
 public:
  struct Options {
    virtual std::vector<GOptionGroup*> GetOptionGroups(void) = 0;
  };

  struct TrackState {
    int64_t duration_ns;
    int64_t position_ns;
  };

  enum Result { kSuccess = 0, kError = -1 };

  OutputModule(Output::PlaybackCallback play, Output::MetadataCallback meta)
      : playback_callback_(play), metadata_callback_(meta) {}

  virtual Result Initalize(Options& options) = 0;

  virtual Output::MimeTypeSet GetSupportedMedia(void) = 0;

  virtual void SetUri(const std::string& uri) = 0;
  virtual void SetNextUri(const std::string& uri) = 0;

  virtual Result Play(void) = 0;
  virtual Result Stop(void) = 0;
  virtual Result Pause(void) = 0;
  virtual Result Seek(int64_t position_ns) = 0;

  virtual Result GetPosition(TrackState* track) = 0;
  virtual Result GetVolume(float* volume) = 0;
  virtual Result SetVolume(float volume) = 0;
  virtual Result GetMute(bool* mute) = 0;
  virtual Result SetMute(bool mute) = 0;

 protected:
  virtual void NotifyPlaybackUpdate(Output::State state) {
    if (playback_callback_) playback_callback_(state);
  }

  virtual void NotifyMetadataChange(const TrackMetadata& metadata) {
    if (metadata_callback_) metadata_callback_(metadata);
  }

  TrackMetadata metadata_;

  Output::PlaybackCallback playback_callback_ = nullptr;
  Output::MetadataCallback metadata_callback_ = nullptr;
};

template <class Class>
class OutputModuleFactory {
 public:
  static OutputModule* Create(Output::PlaybackCallback play,
                              Output::MetadataCallback meta) {
    return new Class(play, meta);
  }
};

#endif