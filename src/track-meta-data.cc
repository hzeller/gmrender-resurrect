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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE   // for asprintf()
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xmldoc.h"
#include "xmlescape.h"

static const char kDidlHeader[] =
    "<DIDL-Lite "
    "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
    "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
    "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
static const char kDidlFooter[] = "</DIDL-Lite>";

// Allocates a new DIDL formatted XML and fill it with given data.
// The input fields are expected to be already xml escaped.
static char *generate_DIDL(const char *id, const char *title,
                           const char *artist, const char *album,
                           const char *genre, const char *composer) {
  char *result = NULL;
  int ret = asprintf(&result,
                     "%s\n<item id=\"%s\">\n"
                     "\t<dc:title>%s</dc:title>\n"
                     "\t<upnp:artist>%s</upnp:artist>\n"
                     "\t<upnp:album>%s</upnp:album>\n"
                     "\t<upnp:genre>%s</upnp:genre>\n"
                     "\t<upnp:creator>%s</upnp:creator>\n"
                     "</item>\n%s",
                     kDidlHeader, id, title ? title : "", artist ? artist : "",
                     album ? album : "", genre ? genre : "",
                     composer ? composer : "", kDidlFooter);
  return ret >= 0 ? result : NULL;
}

// Takes input, if it finds the given tag, then replaces the content between
// these with 'content'. It might re-allocate the original string; only the
// returned string is valid.
// updates "edit_count" if there was a change.
// Very crude way to edit XML.
static char *replace_range(char *const input, const char *tag_start,
                           const char *tag_end, const char *content,
                           int *edit_count) {
  if (content == NULL)  // unknown content; document unchanged.
    return input;
  const int total_len = strlen(input);
  const char *start_pos = strstr(input, tag_start);
  if (start_pos == NULL) return input;
  start_pos += strlen(tag_start);
  const int offset = start_pos - input;
  const char *end_pos = strstr(start_pos, tag_end);
  if (end_pos == NULL) return input;
  const int old_content_len = end_pos - start_pos;
  const int new_content_len = strlen(content);
  char *result = NULL;
  if (old_content_len != new_content_len) {
    result = (char *)malloc(total_len + new_content_len - old_content_len + 1);
    memcpy(result, input, start_pos - input);
    memcpy(result + offset, content, new_content_len);
    strcpy(result + offset + new_content_len, end_pos);  // remaining
    free(input);
    ++*edit_count;
  } else {
    // Typically, we replace the same content with itself - same
    // length. No realloc in this case.
    if (strncmp(start_pos, content, new_content_len) != 0) {
      memcpy(input + offset, content, new_content_len);
      ++*edit_count;
    }
    result = input;
  }
  return result;
}

bool TrackMetadata::UpdateFromTags(const GstTagList *tag_list) {
  auto attemptTagUpdate =
    [tag_list](std::string& tag, const char* tag_name) -> bool {
      // Attempt to fetch the tag
      gchar* value = NULL;
      if (gst_tag_list_get_string(tag_list, tag_name, &value) == false)
        return false;

      if (tag.compare(value) == 0) {
        // Identical tags
        g_free(value);
        return false;
      }

      tag = value;

      // Free the tag buffer
      g_free(value);

      // Log_info(TAG, "Got tag: '%s' value: '%s'", tag_name, tag.c_str());

      return true;
    };

  bool any_change = false;
  any_change |= attemptTagUpdate(title_, GST_TAG_TITLE);
  any_change |= attemptTagUpdate(artist_, GST_TAG_ARTIST);
  any_change |= attemptTagUpdate(album_, GST_TAG_ALBUM);
  any_change |= attemptTagUpdate(genre_, GST_TAG_GENRE);
  any_change |= attemptTagUpdate(composer_, GST_TAG_COMPOSER);

  return any_change;
}

bool TrackMetadata::ParseDIDL(const char *xml) {
  struct xmldoc *doc = xmldoc_parsexml(xml);
  if (doc == NULL) return false;

  // ... did I mention that I hate navigating XML documents ?
  struct xmlelement *didl_node = find_element_in_doc(doc, "DIDL-Lite");
  if (didl_node == NULL) return false;

  struct xmlelement *item_node = find_element_in_element(didl_node, "item");
  if (item_node == NULL) return false;

  struct xmlelement *value_node = NULL;
  value_node = find_element_in_element(item_node, "dc:title");
  if (value_node) title_ = get_node_value(value_node);

  value_node = find_element_in_element(item_node, "upnp:artist");
  if (value_node) artist_ = get_node_value(value_node);

  value_node = find_element_in_element(item_node, "upnp:album");
  if (value_node) album_ = get_node_value(value_node);

  value_node = find_element_in_element(item_node, "upnp:genre");
  if (value_node) genre_ = get_node_value(value_node);

  xmldoc_free(doc);
  return true;
}

// TODO: actually use some XML library for this, but spending too much time
// with XML is not good for the brain :) Worst thing that came out of the 90ies.
char *TrackMetadata::ToDIDL(const char *original_xml) const {
  // Generating a unique ID in case the players cache the content by
  // the item-ID. Right now this is experimental and not known to make
  // any difference - it seems that players just don't display changes
  // in the input stream. Grmbl.
  static unsigned int xml_id = 42;
  char unique_id[4 + 8 + 1];
  snprintf(unique_id, sizeof(unique_id), "gmr-%08x", xml_id++);

  char *result;
  char *title, *artist, *album, *genre, *composer;
  title = title_.length() ? xmlescape(title_.c_str(), 0) : NULL;
  artist = artist_.length() ? xmlescape(artist_.c_str(), 0) : NULL;
  album = album_.length() ? xmlescape(album_.c_str(), 0) : NULL;
  genre = genre_.length() ? xmlescape(genre_.c_str(), 0) : NULL;
  composer = composer_.length() ? xmlescape(composer_.c_str(), 0) : NULL;

  if (original_xml == NULL || strlen(original_xml) == 0) {
    result = generate_DIDL(unique_id, title, artist, album, genre, composer);
  } else {
    int edits = 0;
    // Otherwise, surgically edit the original document to give
    // control points as close as possible what they sent themself.
    result = strdup(original_xml);
    result = replace_range(result, "<dc:title>", "</dc:title>", title, &edits);
    result = replace_range(result, "<upnp:artist>", "</upnp:artist>", artist,
                           &edits);
    result =
        replace_range(result, "<upnp:album>", "</upnp:album>", album, &edits);
    result =
        replace_range(result, "<upnp:genre>", "</upnp:genre>", genre, &edits);
    result = replace_range(result, "<upnp:creator>", "</upnp:creator>",
                           composer, &edits);
    if (edits) {
      // Only if we changed the content, we generate a new
      // unique id.
      result = replace_range(result, " id=\"", "\"", unique_id, &edits);
    }
  }
  free(title);
  free(artist);
  free(album);
  free(genre);
  free(composer);
  return result;
}
