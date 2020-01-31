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

static IXML_Element *find_element(IXML_Node *node, const std::string &key) {
  if (!node) return nullptr;
  node = ixmlNode_getFirstChild(node);
  for (/**/; node != NULL; node = ixmlNode_getNextSibling(node)) {
    if (key == ixmlNode_getNodeName(node)) {
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

XMLElement XMLDoc::AddElement(const std::string &name, const char *ns) {
  IXML_Element *element;
  if (ns) {
    element = ixmlDocument_createElementNS(doc_, ns, name.c_str());
    ixmlElement_setAttribute(element, "xmlns", ns);
  } else {
    element = ixmlDocument_createElement(doc_, name.c_str());
  }
  ixmlNode_appendChild((IXML_Node *)doc_, (IXML_Node *)element);
  return { doc_, element };
}

std::string XMLDoc::ToString() const {
  char *result_raw = ixmlDocumenttoString(doc_);
  std::string result = result_raw;
  free(result_raw);
  return result;
}

std::string XMLElement::value() const {
  if (!exists()) return "";
  IXML_Node *node = (IXML_Node *)element_;
  node = ixmlNode_getFirstChild(node);
  if (!node) return "";
  const char *node_value = ixmlNode_getNodeValue(node);
  if (!node_value) return "";
  return node_value;
}

XMLElement XMLElement::AddElement(const std::string &name) {
  IXML_Element *new_child = ixmlDocument_createElement(doc_, name.c_str());
  ixmlNode_appendChild((IXML_Node*)element_, (IXML_Node*)new_child);
  return { doc_, new_child };
}

XMLElement &XMLElement::SetAttribute(const std::string &name,
                                     const std::string &value) {
  ixmlElement_setAttribute(element_, name.c_str(), value.c_str());
  return *this;
}

XMLElement &XMLElement::SetValue(const char *value) {
  IXML_Node *textNode;
  textNode = ixmlDocument_createTextNode(doc_, value);
  ixmlNode_appendChild((IXML_Node *)element_, textNode);
  return *this;
}

XMLElement &XMLElement::SetValue(const std::string &value) {
  return SetValue(value.c_str());
}

XMLElement &XMLElement::SetValue(int v) {
  char buf[10];
  snprintf(buf, sizeof(buf), "%d", v);
  return SetValue(buf);
}

XMLElement XMLDoc::findElement(const std::string &name) const {
  return { doc_, find_element((IXML_Node *)doc_, name) };
}

XMLElement XMLElement::findElement(const std::string &name) const {
  return { doc_, find_element((IXML_Node *)element_, name) };
}
