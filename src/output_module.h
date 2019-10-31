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

#include <string>
#include <vector>
#include <set>

#include "output.h"

class OutputModule
{
  public:
    struct Options 
    {
      virtual std::vector<GOptionGroup*> get_option_groups(void) = 0;
    };

    typedef struct track_state_t {int64_t duration_ns; int64_t position_ns;} track_state_t;

    typedef enum result_t
    {
      Success = 0,
      Error = -1
    } result_t;
  
    OutputModule(Output::playback_callback_t play = nullptr, Output::metadata_callback_t meta = nullptr)
    {
      this->playback_callback = play;
      this->metadata_callback = meta;
    }

    virtual result_t initalize(Options& options) = 0;

    virtual Output::mime_type_set_t get_supported_media(void) = 0;

    virtual void set_uri(const std::string &uri) = 0;
    virtual void set_next_uri(const std::string &uri) = 0;

    virtual result_t play(void) = 0;
    virtual result_t stop(void) = 0;
    virtual result_t pause(void) = 0;
    virtual result_t seek(int64_t position_ns) = 0;

    virtual result_t get_position(track_state_t& position) = 0;
    virtual result_t get_volume(float& volume) = 0;
    virtual result_t set_volume(float volume) = 0;
    virtual result_t get_mute(bool& mute) = 0;
    virtual result_t set_mute(bool mute) = 0;

  protected:
    track_metadata_t metadata;

    Output::playback_callback_t playback_callback = nullptr;
    Output::metadata_callback_t metadata_callback = nullptr;

    virtual void notify_playback_update(Output::output_state_t state)
    {
      if (this->playback_callback)
        this->playback_callback(state);
    }

    virtual void notify_metadata_change(const track_metadata_t& metadata)
    {
      if (this->metadata_callback)
        this->metadata_callback(metadata);
    }
};

template <class Class>
class OutputModuleFactory 
{
  public:
    static OutputModule* create(Output::playback_callback_t play = nullptr, Output::metadata_callback_t meta = nullptr)
    { 
      return new Class(play, meta);
    }
};

#endif