// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* xmlescape.c - helper routines for escaping XML strings
 *
 * Copyright (C) 2019 Henner Zeller
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
#include "xmlescape.h"

std::string xmlescape(const std::string &str) {
  std::string result;
  result.reserve(str.length() + 10);

  // TODO: If we ever deal with attributes, within attributes also %22-escape
  // double-quotes.
  for (char c : str) {
    switch (c) {
    case '<': result.append("&lt;"); break;
    case '>': result.append("&gt;"); break;
    case '&': result.append("&amp;"); break;
    default: result.append(1, c); break;
    }
  }
  return result;
}
