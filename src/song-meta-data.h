// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* song-meta-data - Object holding meta data for a song.
 *
 * Copyright (C) 2012 Henner Zeller
 * Copyright (C) 2020 Tucker Kern
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

#include <assert.h>

#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include <logging.h>
#include <pugixml/pugixml.hpp>

/**
  @brief  Class to contain and maintain a map of supported track metadata tags
*/
class TrackMetadata {
 public:
  /**
    @brief  Public interface for a metadata entry
  */
  class IEntry {
   public:
    virtual IEntry& operator=(const std::string& other) = 0;
    virtual operator const std::string&() const = 0;
  };

  enum Tag {
    kTitle = 0,  // Title must be first
    kArtist,
    kAlbum,
    kGenre,
    kCreator,
    kDate,
    kTrackNumber,
    // kClass, // TODO Required but not sure how to detect from stream
    // Not yet implemented
    // kBitrate,
  };

  TrackMetadata(void) {
    // Create all known tags_ in the map
    tags_.emplace(kTitle, Entry("dc:title", this));
    tags_.emplace(kArtist, Entry("upnp:artist", this));
    tags_.emplace(kAlbum, Entry("upnp:album", this));
    tags_.emplace(kGenre, Entry("upnp:genre", this));
    tags_.emplace(kCreator, Entry("dc:creator", this));
    tags_.emplace(kDate, Entry("dc:date", this));
    tags_.emplace(kTrackNumber, Entry("upnp:originalTrackNumber", this));
    // tags_.emplace(kClass,   "upnp:class");
  }

  const std::string& operator[](Tag tag) const {
    assert(tags_.count(tag));

    return tags_.at(tag);
  }

  IEntry& operator[](Tag tag) {
    assert(tags_.count(tag));

    return tags_.at(tag);
  }

  /**
    @brief  Allows metadata entry to be accessed by a name instead of the Tag
    enum.

    @param  name C string of tag name.
    @retval IEntry&
  */
  IEntry& operator[](const char* name) {
    static Entry invalid_entry("null", this);

    if (name_tag_map_.count(name)) return (*this)[name_tag_map_.at(name)];

    Log_warn("Metadata", "Unsupported tag name '%s'", name);
    return invalid_entry;
  }

  /**
    @brief  Checks if the metadata has been modified since the last call.

    @param  none
    @retval bool - True if Metadata ID changed since the last call.
  */
  bool Modified() const {
    static uint32_t lastId = 0;

    bool modified = (id_ != lastId);

    lastId = id_;

    return modified;
  }

  /**
    @brief  Tests if the tag name is supported by the class

    @param  name C string of tag name.
    @retval bool - True if tag name is known
  */
  bool TagSupported(const char* name) const {
    return name_tag_map_.count(name) > 0;
  }

  /**
    @brief  Clear all tag values

    @param  none
    @retval none
  */
  void Clear() {
    for (auto& kv : tags_) kv.second.value_.clear();
  }

  std::string ToXml(const std::string& xml = "") const;

 private:
  /**
    @brief  Private implementation of a metadata entry implementing the
    interface. Notifies parent when metadata value is modified.
  */
  class Entry : public IEntry {
   public:
    Entry(const std::string& k, TrackMetadata* const p)
        : parent_(*p), key_(k) {}

    /**
      @brief  Assignment operator for tag value. Notifies parent if a change
      occurs.

      @param  other std::string of new tag value
      @retval Entry&
    */
    Entry& operator=(const std::string& other) {
      if (value_.compare(other) == 0) return *this;  // Identical tags_

      value_.assign(other);

      // Notify parent value has changed
      parent_.Notify();

      return *this;
    }

    /**
      @brief  Converstion operator for Entry to std::string

      @param  none
      @retval const std::string - Tag value
    */
    operator const std::string&() const { return value_; }

    TrackMetadata& parent_;
    const std::string key_;  // DIDL-Lite key
    std::string value_;      // DIDL-Lite value
  };

  void Notify() { id_++; }
  void CreateXmlRoot(pugi::xml_document& xml_document) const;

  uint32_t id_ = 0;
  std::map<Tag, Entry> tags_;

  /**
    @brief  Map of common tag names to Tag enum values
  */
  std::unordered_map<std::string, Tag> name_tag_map_ = {
      {"artist", kArtist},
      {"title", kTitle},
      {"album", kAlbum},
      {"composer", kCreator},
      {"genre", kGenre},
      {"date", kDate},
      {"datetime", kDate},
      {"tracknumber", kTrackNumber},
      {"track-number", kTrackNumber},
      // Not yet implemented
      //{"bitrate",     kBitrate},
  };
};

#endif  // _SONG_META_DATA_H
