// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* xmldoc.h - XML abstraction around libupnp iXML
 *
 * Copyright (C) 2020 H. Zeller
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
#include "xmldoc2.h"

#include <ixml.h>
#include <string.h>

static IXML_Element *find_element(IXML_Node *node, const char *key) {
  if (!node) return nullptr;
  node = ixmlNode_getFirstChild(node);
  for (/**/; node != NULL; node = ixmlNode_getNextSibling(node)) {
    if (strcmp(ixmlNode_getNodeName(node), key) == 0) {
      return (IXML_Element*) node;
    }
  }
  return NULL;
}

std::unique_ptr<XMLDoc> XMLDoc::Parse(const std::string &xml_text) {
  IXML_Document *doc = ixmlParseBuffer(xml_text.c_str());
  if (!doc) return nullptr;
  std::unique_ptr<XMLDoc> result(new XMLDoc(doc));
  return result;
}

XMLDoc::XMLDoc() : XMLDoc(ixmlDocument_createDocument()) {}
XMLDoc::~XMLDoc() { ixmlDocument_free(doc_); }

std::string XMLElement::value() const {
  if (isEmpty()) return "";
  IXML_Node *node = (IXML_Node *)element_;
  node = ixmlNode_getFirstChild(node);
  if (!node) return "";
  const char *node_value = ixmlNode_getNodeValue(node);
  if (!node_value) return "";
  return node_value;
}

XMLElement XMLDoc::findElement(const char *name) const {
  return find_element((IXML_Node *)doc_, name);
}

XMLElement XMLElement::findElement(const char *name) const {
  return find_element((IXML_Node *)element_, name);
}
