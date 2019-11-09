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

// TODO: we're assuming that the namespaces are abbreviated with 'dc' and 'upnp'
// ... but if I understand that correctly, that doesn't need to be the case.

#include "song-meta-data.h"

/**
  @brief  Create and append root element and requried attributes for metadata
  XML

  @param  xml_document pugi::xml_document to add root item to
  @retval none
*/
void TrackMetadata::CreateXmlRoot(pugi::xml_document& xml_document) const {
  pugi::xml_node root = xml_document.append_child("DIDL-Lite");
  root.append_attribute("xmlns").set_value(
      "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/");
  root.append_attribute("xmlns:dc")
      .set_value("http://purl.org/dc/elements/1.1/");
  root.append_attribute("xmlns:upnp")
      .set_value("urn:schemas-upnp-org:metadata-1-0/upnp/");

  // An "item" element must have
  // dc:title element first
  // upnp:class element
  // id attribute
  // parentId attribute
  // restricted attribute
  pugi::xml_node item = root.append_child("item");
  item.append_attribute("id").set_value("");
  item.append_attribute("parentID").set_value("0");
  item.append_attribute("restricted").set_value("false");
}

/**
  @brief  Format metadata to XML (DIDL-Lite) format. Modifies existing XML
          if passed as argument

  @param  xml Original XML string to modify
  @retval std::string Metadata as XML (DIDL-Lite)
*/
std::string TrackMetadata::ToXml(const std::string& xml) const {
  pugi::xml_document xml_document;

  // Parse existing document
  xml_document.load_string(xml.c_str());

  pugi::xml_node root = xml_document.child("DIDL-Lite");
  pugi::xml_node item = root.child("item");

  // Existing format sucks, just make our own
  if (root == NULL || item == NULL) {
    xml_document.reset();
    CreateXmlRoot(xml_document);

    // Update locals with new document objects
    root = xml_document.child("DIDL-Lite");
    item = root.child("item");
  }

  bool modified = false;
  for (const auto& kv : tags_) {
    const std::string& tag = kv.second.key_;
    const std::string& value = kv.second.value_;

    // Skip if no value
    if (value.empty()) continue;

    pugi::xml_node xml = item.child(tag.c_str());
    if (xml) {
      // Check if already equal to avoid ID update
      if (value.compare(xml.first_child().value()) == 0) continue;

      // Update existing XML element
      xml.first_child().set_value(value.c_str());

      modified = true;
    } else {
      // Insert new XML element
      xml = item.append_child(tag.c_str());
      xml.append_child(pugi::node_pcdata).set_value(value.c_str());

      modified = true;
    }
  }

  if (modified) {
    char idString[20] = {0};
    snprintf(idString, sizeof(idString), "gmr-%08x", id_);

    item.attribute("id").set_value(idString);
  }

  std::ostringstream stream;
  xml_document.save(stream, "\t",
                    pugi::format_default | pugi::format_no_declaration);

  return stream.str();
}