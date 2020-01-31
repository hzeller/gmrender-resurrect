// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* track-meta-data - Object holding meta data for a song.
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

// TODO: we're assuming that the namespaces are abbreviated with 'dc' and 'upnp'
// ... but if I understand that correctly, that doesn't need to be the case.

#include "track-meta-data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xmldoc.h"

std::string TrackMetadata::generateDIDL(const std::string &id) const {
  XMLDoc doc;
  auto item = doc.AddElement("DIDL-Lite",
                             "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/")
    .SetAttribute("xmlns:dc", "http://purl.org/dc/elements/1.1/")
    .SetAttribute("xmlns:upnp", "urn:schemas-upnp-org:metadata-1-0/upnp/")
    .AddElement("item").SetAttribute("id", id);
  auto SetOptional = [&item](const char *name, const std::string &val) {
                       if (!val.empty()) item.AddElement(name).SetValue(val);
                     };
  SetOptional("dc:title", title_);
  SetOptional("upnp:artist", artist_);
  SetOptional("upnp:album", album_);
  SetOptional("upnp:genre", genre_);
  SetOptional("upnp:creator", composer_);
  return doc.ToString();
}

bool TrackMetadata::UpdateFromTags(const GstTagList *tag_list) {
  // Update provided tag if the content in tag_list exists and is different.
  auto attemptTagUpdate =
    [tag_list](const char *tag_name, std::string *tag) -> bool {
      gchar* value = nullptr;
      if (!gst_tag_list_get_string(tag_list, tag_name, &value))
        return false;

      const bool needs_update = (*tag != value);
      if (needs_update) {
        *tag = value;
      }
      g_free(value);
      return needs_update;
    };

  bool any_change = false;
  any_change |= attemptTagUpdate(GST_TAG_TITLE, &title_);
  any_change |= attemptTagUpdate(GST_TAG_ARTIST, &artist_);
  any_change |= attemptTagUpdate(GST_TAG_ALBUM, &album_);
  any_change |= attemptTagUpdate(GST_TAG_GENRE, &genre_) ;
  any_change |= attemptTagUpdate(GST_TAG_COMPOSER, &composer_);

  return any_change;
}

bool TrackMetadata::ParseDIDL(const std::string &xml) {
  const auto doc = XMLDoc::Parse(xml);
  if (!doc) return false;

  const auto items = doc->findElement("DIDL-Lite").findElement("item");
  if (!items.exists()) return false;
  title_ = items.findElement("dc:title").value();
  artist_ = items.findElement("upnp:artist").value();
  album_ = items.findElement("upnp:album").value();
  genre_ = items.findElement("upnp:genre").value();
  composer_ = items.findElement("upnp:creator").value();
  return true;
}

/*static*/ std::string TrackMetadata::DefaultCreateNewId() {
  // Generating a unique ID in case the players cache the content by
  // the item-ID. Right now this is experimental and not known to make
  // any difference - it seems that players just don't display changes
  // in the input stream. Grmbl.
  static unsigned int xml_id = 42;
  char unique_id[4 + 8 + 1];
  snprintf(unique_id, sizeof(unique_id), "gmr-%08x", xml_id++);
  return unique_id;
}

std::string TrackMetadata::ToDIDL(const std::string &original_xml,
                                  std::function<std::string()> idgen) const {
  auto doc = XMLDoc::Parse(original_xml);
  XMLElement items;
  if (!doc || !(items = doc->findElement("DIDL-Lite").findElement("item"))) {
    return generateDIDL(idgen ? idgen() : DefaultCreateNewId());
  }

  auto updateFun = [&items](const char *name, const std::string &val) -> bool {
                     auto tag = items.findElement(name);
                     if (tag.value() == val) return false; // no change.
                     if (!tag.exists()) tag = items.AddElement(name);
                     tag.SetValue(val);
                     return true;
                   };

  bool any_change = false;
  any_change |= updateFun("dc:title", title_);
  any_change |= updateFun("upnp:artist", artist_);
  any_change |= updateFun("upnp:genre", genre_);
  any_change |= updateFun("upnp:album", album_);
  any_change |= updateFun("upnp:creator", composer_);
  if (any_change) {
    // Only if we changed the content, we generate a new unique id.
    const std::string new_id = idgen ? idgen() : DefaultCreateNewId();
    doc->findElement("DIDL-Lite").SetAttribute("id", new_id);
  }
  return doc->ToString();
}
