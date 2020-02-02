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
#ifndef XMLDOC_H_
#define XMLDOC_H_

#include <ixml.h>
#include <memory>

// Basic XML api with only the functions we need. Wrapper around underlying
// ixml.

class XMLElement;
class ElementRange;
class XMLDoc {
public:
  XMLDoc();
  ~XMLDoc();

  // Factory for a new XML document: parse document; if valid, return a doc.
  static std::unique_ptr<XMLDoc> Parse(const std::string &xml_text);

  // TODO: this should just have an ... argument list with a sequence
  XMLElement FindElement(const std::string &name) const;

  // Create a new top element with an optional namespace.
  XMLElement AddElement(const std::string &name, const char *ns = nullptr);

  // Create an XML string representation out of the document.
  std::string ToXMLString() const;

private:
  XMLDoc(IXML_Document *doc) : doc_(doc) {}

  IXML_Document *const doc_;
};

class XMLElement {
public:
  XMLElement() {}
  bool exists() const { return element_ != nullptr; }
  operator bool() const { return exists(); }

  // Get name of element.
  const char *name() const;

  XMLElement FindElement(const std::string &name) const;

  // Iterate through children. Use in Loops such as
  // for (XMLElement e : foo.children()) { do something }
  ElementRange children() const;

  // Create new sub-element within this "name".
  XMLElement AddElement(const std::string &name);

  // Set attribute "name" to "value" of this element.
  // Returns itself for chaining.
  XMLElement &SetAttribute(const std::string &name, const std::string &value);

  // Get an attribute of "name".
  std::string attribute(const std::string &name) const;

  // Set text Value. Returns itself for chaining. Overloads for various types.
  XMLElement &SetValue(const char *value);
  XMLElement &SetValue(const std::string &value);
  XMLElement &SetValue(long v);

  // Get text value
  std::string value() const;

private:
  friend class XMLDoc;
  friend class ElementRange;
  XMLElement(IXML_Document *doc, IXML_Element *element)
    : doc_(doc), element_(element) {}

  IXML_Document *doc_ = nullptr;    // Not owned.
  IXML_Element *element_ = nullptr; // Not owned.
};

class ElementRange {
public:
  class iterator {
  public:
    ~iterator() { ixmlNodeList_free(list_); }
    XMLElement operator*() const { return { doc_, (IXML_Element*)it_->nodeItem }; }
    iterator& operator++() { it_ = it_->next; return *this; }
    bool operator!=(const iterator &other) const { return other.it_ != it_; }

  private:
    friend class ElementRange;
    iterator() : iterator(nullptr, nullptr) {}
    iterator(IXML_Document *doc, IXML_NodeList *l)
      : doc_(doc), list_(l), it_(l) {}

    IXML_Document *const doc_;
    IXML_NodeList *const list_;
    IXML_NodeList *it_;
  };

  iterator begin() const {
    if (!parent_) return iterator();
    return iterator(doc_, ixmlNode_getChildNodes((IXML_Node*)parent_));
  }
  iterator end() const { return iterator(); }

private:
  friend class XMLElement;
  ElementRange(IXML_Document *doc, IXML_Element *parent)
    : doc_(doc), parent_(parent) {}

  IXML_Document *const doc_;
  IXML_Element *const parent_;
};

#endif  // XMLDOC_H_
