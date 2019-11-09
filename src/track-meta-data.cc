// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* track-meta-data - Object holding meta data for a song.
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

#include "track-meta-data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xmldoc.h"

/**
  @brief  Create and append root element and requried attributes for metadata
  XML

  @param  xml_document pugi::xml_document to add root item to
  @retval none
*/
void TrackMetadata::CreateXmlRoot(XMLDoc& xml_document) const {
  XMLElement root = xml_document.AddElement("DIDL-Lite");

  root.SetAttribute("xmlns", "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/");
  root.SetAttribute("xmlns:dc", "http://purl.org/dc/elements/1.1/");
  root.SetAttribute("xmlns:upnp", "urn:schemas-upnp-org:metadata-1-0/upnp/");

  // An "item" element must have
  // dc:title element first
  // upnp:class element
  // id attribute
  // parentId attribute
  // restricted attribute
  XMLElement item = root.AddElement("item");
  item.SetAttribute("id", "");
  item.SetAttribute("parentID", "0");
  item.SetAttribute("restricted", "false");
}

/**
  @brief  Format metadata to XML (DIDL-Lite) format. Modifies existing XML
          if passed as argument

  @param  xml Original XML string to modify
  @retval std::string Metadata as XML (DIDL-Lite)
*/
std::string TrackMetadata::ToXml(const std::string& xml) const {
  // Parse existing document
  auto xml_document = XMLDoc::Parse(xml);

  XMLElement root;
  XMLElement item;

  // Attempt to find root and item element from original XML
  if (xml_document != nullptr)
  {
    root = xml_document->findElement("DIDL-Lite");
    item = root.findElement("item");
  }

  // Existing format sucks, just make our own
  if (!root.exists() || !item.exists()) {
    xml_document = std::unique_ptr<XMLDoc>(new XMLDoc()); // This is awkward

    CreateXmlRoot(*xml_document);

    // Update locals with new document objects
    root = xml_document->findElement("DIDL-Lite");
    item = root.findElement("item");
  }

  bool modified = false;
  for (const auto& kv : tags_) {
    const std::string& tag = kv.second.key_;
    const std::string& value = kv.second.value_;

    // Skip if no value
    if (value.empty()) continue;

    XMLElement element = item.findElement(tag.c_str());
    if (element) {
      // Check if already equal to avoid ID update
      if (value.compare(element.value()) == 0) continue;

      // Update existing XML element
      element.SetValue(value.c_str());

      modified = true;
    } else {
      // Insert new XML element
      element = item.AddElement(tag.c_str());
      element.SetValue(value.c_str());

      modified = true;
    }
  }

  if (modified) {
    char idString[20] = {0};
    snprintf(idString, sizeof(idString), "gmr-%08x", id_);

    item.SetAttribute("id", idString);
  }

  return xml_document->ToXMLString();
}
