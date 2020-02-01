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

#include <gst/gsttaglist.h>

#include <string>
#include <functional>

// Metadata for a song.
class TrackMetadata {
public:
  const std::string& title() const { return title_; }
  void set_title(const std::string &v) { title_ = v; }

  const std::string& artist() const { return artist_; }
  void set_artist(const std::string &v) { artist_ = v; }

  const std::string& album() const { return album_; }
  void set_album(const std::string &v) { album_ = v; }

  const std::string& genre() const { return genre_; }
  void set_genre(const std::string &v) { genre_ = v; }

  const std::string& composer() const { return composer_; }
  void set_composer(const std::string &v) { composer_ = v; }

  // Update from GstTags. Return if there was any change.
  bool UpdateFromTags(const GstTagList *tag_list);

  // Returns xml string with the song meta data encoded as
  // DIDL-Lite. If we get a non-empty original xml document, returns an
  // edited version of that document.
  // "idgen" is a generator for the toplevel identifier attribute of the
  // document; if null, a default generator is used.
  std::string ToXML(const std::string &original_xml,
                    std::function<std::string()> idgen = nullptr) const;

  // Parse DIDL-Lite and fill SongMetaData struct. Returns true when successful.
  bool ParseXML(const std::string &xml);

  bool operator==(const TrackMetadata &o) const {
    return title_ == o.title_ && artist_ == o.artist_ && album_ == o.album_
      && genre_ == o.genre_ && composer_ == o.composer_;
  }

private:
  static std::string DefaultCreateNewId();
  // Generate a new DIDL XML.
  std::string generateDIDL(const std::string &id) const;

  std::string title_;
  std::string artist_;
  std::string album_;
  std::string genre_;
  std::string composer_;
};

#endif  // _SONG_META_DATA_H
