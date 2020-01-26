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
char *TrackMetadata::generateDIDL(const std::string &id) const {
  char *result = nullptr;
  int ret = asprintf(
    &result, R"(%s
<item id="%s">
  <dc:title>%s</dc:title>
  <upnp:artist>%s</upnp:artist>
  <upnp:album>%s</upnp:album>
  <upnp:genre>%s</upnp:genre>
  <upnp:creator>%s</upnp:creator>
</item>
%s)",
    kDidlHeader, id.c_str(),
    xmlescape(title_).c_str(), xmlescape(artist_).c_str(),
    xmlescape(album_).c_str(), xmlescape(genre_).c_str(),
    xmlescape(composer_).c_str(), kDidlFooter);
  return ret >= 0 ? result : NULL;
}

// Takes input, if it finds the given XML-tag, then replaces the content
// between these with 'content'.
// Make sure to pass something without namespace to "tag_start", so that
// we are namespace agnostic.
// It might re-allocate the original string and free the original; only the
// returned string is valid.
// updates "edit_count" if there was a change.
// Very crude way to edit XML.
// TODO: use std::string to simplify operations.
static char *replace_tag_content(char *const input,
                                 const char *tag_start, const char *tag_end,
                                 const std::string &unescaped_content,
                                 int *edit_count) {
  if (unescaped_content.empty())
    return input;  // unknown content; document unchanged.
  const std::string content = xmlescape(unescaped_content);
  const int total_len = strlen(input);
  const char *start_pos = strstr(input, tag_start);
  if (start_pos == NULL) return input;
  start_pos += strlen(tag_start);
  const int offset = start_pos - input;
  const char *end_pos = strstr(start_pos, tag_end);
  if (end_pos == NULL) return input;
  const int old_content_len = end_pos - start_pos;
  const int new_content_len = content.length();
  char *result = NULL;
  if (old_content_len != new_content_len) {
    result = (char *)malloc(total_len + new_content_len - old_content_len + 1);
    memcpy(result, input, start_pos - input);
    memcpy(result + offset, content.c_str(), new_content_len);
    strcpy(result + offset + new_content_len, end_pos);  // remaining
    free(input);
    ++*edit_count;
  } else {
    // Typically, we replace the same content with itself - same
    // length. No realloc in this case.
    if (strncmp(start_pos, content.c_str(), new_content_len) != 0) {
      memcpy(input + offset, content.c_str(), new_content_len);
      ++*edit_count;
    }
    result = input;
  }
  return result;
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
  struct xmldoc *doc = xmldoc_parsexml(xml.c_str());
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
  char *result;
  if (original_xml.empty()) {
    const std::string new_id = idgen ? idgen() : DefaultCreateNewId();
    result = generateDIDL(new_id);
  } else {
    int edits = 0;
    // Otherwise, surgically edit the original document to give
    // control points as close as possible what they sent themself.
    result = strdup(original_xml.c_str());
    result = replace_tag_content(result, ":title>", "</", title_, &edits);
    result = replace_tag_content(result, ":artist>", "</", artist_, &edits);
    result = replace_tag_content(result, ":album>", "</", album_, &edits);
    result = replace_tag_content(result, ":genre>",  "</", genre_, &edits);
    result = replace_tag_content(result, ":creator>", "</", composer_, &edits);
    if (edits) {
      // Only if we changed the content, we generate a new
      // unique id.
      const std::string new_id = idgen ? idgen() : DefaultCreateNewId();
      result = replace_tag_content(result, " id=\"", "\"", new_id, &edits);
    }
  }
  // TODO: make all the above use std::string operations to begin with, so
  // that we don't have to do the silly copy here.
  const std::string to_return(result);
  free(result);
  return to_return;
}
