// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* song-meta-data - Object holding meta data for a song.
 *
 * Copyright (C) 2012 Henner Zeller
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

#ifndef _SONG_META_DATA_H
#define _SONG_META_DATA_H

#include <string>

// An 'object' dealing with the meta data of a song.
// TODO Tucker Great opertunity for a class here
struct TrackMetadata {
  std::string title;
  std::string artist;
  std::string album;
  std::string genre;
  std::string composer;
};

// Construct song meta data object.
void SongMetaData_init(TrackMetadata *object);

// Clear meta data strings and deallocate them.
void SongMetaData_clear(TrackMetadata *object);

// Returns a newly allocated xml string with the song meta data encoded as
// DIDL-Lite. If we get a non-empty original xml document, returns an
// edited version of that document.
char *SongMetaData_to_DIDL(const TrackMetadata* object,
                           const char *original_xml);

// Parse DIDL-Lite and fill SongMetaData struct. Returns 1 when successful.
int SongMetaData_parse_DIDL(TrackMetadata *object, const char *xml);

#endif  // _SONG_META_DATA_H
