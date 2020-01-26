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

#include "xmldoc2.h"
#include "xmlescape.h"

static constexpr char kDidlHeader[] =
    "<DIDL-Lite "
    "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
    "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
static constexpr char kDidlFooter[] = "</item></DIDL-Lite>";

std::string TrackMetadata::generateDIDL(const std::string &id) const {
  std::string result(kDidlHeader);
  result.append("<item id=\"").append(id).append("\">\n");
  struct XMLAddTag { const std::string tag; const std::string &value; };
  const XMLAddTag printTags[]
    = { { "dc:title",     title_ }, { "upnp:artist", artist_ },
        { "upnp:album",   album_ }, { "upnp:genre",  genre_ },
        { "upnp:creator", composer_ } };
  for (auto t : printTags) {
    if (t.value.empty()) continue;
    result += "  <" + t.tag + ">" + xmlescape(t.value) + "</" + t.tag + ">\n";
  }
  result.append(kDidlFooter);
  return result;
}

// Takes input, if it finds the given XML-tag, then replaces the content
// between these with 'content'. Returns true if tag was found and replace.
static bool replace_tag(const char *tag_start, const char *tag_end,
                        const std::string &unescaped_content,
                        std::string *document) {
  if (unescaped_content.empty())
    return false;  // unknown content; document unchanged.
  const std::string content = xmlescape(unescaped_content);

  auto begin_replace = document->find(tag_start);
  if (begin_replace == std::string::npos) return false;
  begin_replace += strlen(tag_start);
  auto end_replace = document->find(tag_end, begin_replace);
  if (end_replace == std::string::npos) return false;
  document->replace(begin_replace, end_replace - begin_replace, content);
  return true;
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
  if (items.isEmpty()) return false;
  title_ = items.findElement("dc:title").value();
  artist_ = items.findElement("upnp:artist").value();
  album_ = items.findElement("upnp:album").value();
  genre_ = items.findElement("upnp:genre").value();
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

// TODO: Maybe use some XMLdoc library for this; however, we are also
// interested in minimally invasive edit incoming XML - control points might
// only really be able to parse their own style of XML. So it might be
// not compatible with the control point if we re-create the XML.
std::string TrackMetadata::ToDIDL(const std::string &original_xml,
                                  std::function<std::string()> idgen) const {
  if (original_xml.empty()) {
    const std::string new_id = idgen ? idgen() : DefaultCreateNewId();
    return generateDIDL(new_id);
  }
  std::string result;
  // Otherwise, surgically edit the original document to give
  // control points as close as possible what they sent themself.
  result = original_xml;
  bool any_change = false;
  any_change |= replace_tag(":title>", "</", title_, &result);
  any_change |= replace_tag(":artist>", "</", artist_, &result);
  any_change |= replace_tag(":album>", "</", album_, &result);
  any_change |= replace_tag(":genre>",  "</", genre_, &result);
  any_change |= replace_tag(":creator>", "</", composer_, &result);
  if (any_change) {
    // Only if we changed the content, we generate a new
    // unique id.
    const std::string new_id = idgen ? idgen() : DefaultCreateNewId();
    replace_tag(" id=\"", "\"", new_id, &result);
  }
  return result;
}
