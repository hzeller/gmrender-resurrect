// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output.h - Output module frontend
 *
 * Copyright (C) 2007 Ivo Clarysse,  (C) 2012 Henner Zeller, (C) 2019 Tucker
 * Kern
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

namespace Output {
typedef enum OutputState { kPlaybackStopped, kStartedNextStream } OutputState;

// Callbacks types from output to higher levels
typedef void (*PlaybackCallback)(OutputState);
typedef void (*MetadataCallback)(const TrackMetadata&);

typedef std::set<std::string> MimeTypeSet;

int AddOptions(GOptionContext* ctx);
void DumpModules(void);
int Loop(void);

int Init(const char* shortname, PlaybackCallback play_callback,
         MetadataCallback metadata_callback);

MimeTypeSet GetSupportedMedia(void);

void SetUri(const char* uri);
void SetNextUri(const char* uri);

int Play(void);
int Pause(void);
int Stop(void);
int Seek(int64_t position_nanos);

int GetPosition(int64_t& duration, int64_t& position);
int GetVolume(float& value);
int SetVolume(float value);
int GetMute(bool& value);
int SetMute(bool value);
};  // namespace Output

#endif /* _OUTPUT_H */
