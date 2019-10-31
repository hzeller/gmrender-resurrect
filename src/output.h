// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output.h - Output module frontend
 *
 * Copyright (C) 2007 Ivo Clarysse,  (C) 2012 Henner Zeller, (C) 2019 Tucker Kern
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

#include <set>
#include <string>

#include <glib.h>
#include "song-meta-data.h"

namespace Output
{
  typedef enum output_state_t 
  {
    PlaybackStopped,
    StartedNextStream
  } output_state_t;

  // Callbacks types from output to higher levels
  typedef void (*playback_callback_t)(output_state_t);
  typedef void (*metadata_callback_t)(const track_metadata_t&);

  typedef std::set<std::string> mime_type_set_t;

  int add_options(GOptionContext* ctx);
  void dump_modules(void);
  int loop(void);

  int init(const char* shortname, playback_callback_t play_callback, metadata_callback_t metadata_callback);

  mime_type_set_t get_supported_media(void);

  void set_uri(const char* uri);
  void set_next_uri(const char* uri);

  int play(void);
  int pause(void);
  int stop(void);
  int seek(int64_t position_nanos);

  int get_position(int64_t& duration, int64_t& position);
  int get_volume(float& value);
  int set_volume(float value);
  int get_mute(bool& value);
  int set_mute(bool value);
};

#endif /* _OUTPUT_H */
