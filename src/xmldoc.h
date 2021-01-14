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
#include <string>

// Basic XML api with only the functions we need. Wrapper around underlying
// ixml.

class XMLElement;
class ElementRange;
class XMLDoc {
public:
  // Create a new, empty XMLDocument.
  XMLDoc();
  ~XMLDoc();

  // Factory for a new XML document: parse document; if valid, return a doc.
  static std::unique_ptr<XMLDoc> Parse(const std::string &xml_text);

  // Find child element with the given name.
  // If it doesn't exist, returns an XMLElement with exists() == false.
  XMLElement FindChild(const std::string &name) const;

  // Iterate through children. Use in Loops such as
  // for (XMLElement e : foo.children()) { do something }
  // Or if you are just interested in the first element, use
  // foo.children().first().
  ElementRange children() const;

  // Find nested elements with given name. Recurses down the document,
  // returning iterable elements with the given name.
  ElementRange AllNested(const char *name) const;

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

  // Find child element with the given name.
  // If it doesn't exist, returns an XMLElement with exists() == false.
  XMLElement FindChild(const std::string &name) const;

  // Iterate through children. Use in Loops such as
  // for (XMLElement e : foo.children()) { do something }
  ElementRange children() const;

  // Find nested elements with given name. Recurses down the document,
  // returning iterable elements with the given name.
  ElementRange AllNested(const char *name) const;

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

// Iterable range over nodes returned from children() or AllNested()
class ElementRange {
public:
  class iterator {
  public:
    ~iterator() { ixmlNodeList_free(list_); }
    XMLElement operator*() const {
      return { doc_, (IXML_Element*)it_->nodeItem };
    }
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
    if (!parent_) return end();
    if (filter_name_) {
      // find nested. Different for document or element.
      return parent_is_doc_
        ? iterator(doc_, ixmlDocument_getElementsByTagName(
                     (IXML_Document*)parent_, filter_name_))
        : iterator(doc_, ixmlElement_getElementsByTagName(
                     (IXML_Element*)parent_, filter_name_));
    } else {
      return iterator(doc_, ixmlNode_getChildNodes(parent_));
    }
  }

  iterator end() const { return iterator(); }

  // Get the first element of this Range or a non-exist XMLElement if there
  // is non in the range.
  XMLElement first() const {
    iterator it = begin();
    if (it != end()) return *it;
    return { doc_, nullptr };
  }

private:
  friend class XMLElement;
  friend class XMLDoc;
  ElementRange(IXML_Document *doc, IXML_Element *parent, const char *name)
    : doc_(doc), parent_((IXML_Node*)parent), parent_is_doc_(false),
      filter_name_(name) {}

  ElementRange(IXML_Document *doc, IXML_Document *parent, const char *name)
    : doc_(doc), parent_((IXML_Node*)parent), parent_is_doc_(true),
      filter_name_(name) {}

  IXML_Document *const doc_;
  IXML_Node *const parent_;
  const bool parent_is_doc_;
  const char *const filter_name_;
};

#endif  // XMLDOC_H_
