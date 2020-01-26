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

class XMLElement;
class XMLDoc {
public:
  // Factory for a new XML document: parse document; if valid, return a doc.
  static std::unique_ptr<XMLDoc> Parse(const std::string &xml_text);

  XMLDoc();
  ~XMLDoc();

  // TODO: this should just have an ... argument list with a sequence
  XMLElement findElement(const char *name) const;

private:
  XMLDoc(IXML_Document *doc) : doc_(doc) {}

  IXML_Document *const doc_;
};

class XMLElement {
public:
  std::string value() const;
  XMLElement findElement(const char *name) const;
  bool isEmpty() const { return element_ == nullptr; }

private:
  friend class XMLDoc;
  XMLElement(IXML_Element *element) : element_(element) {}

  IXML_Element *const element_;
};

#endif  // XMLDOC_H_
