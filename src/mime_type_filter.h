// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* mime_type_filter.h - MIME type filtering
 *
 * Copyright (C) 2019 Tucker Kern
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

#ifndef _MIME_TYPE_FILTER_H
#define _MIME_TYPE_FILTER_H

#include <set>
#include <sstream>
#include <string>

class MimeTypeFilter {
 public:
  MimeTypeFilter(const char* filter_string) {
    // Ensure input is not NULL
    parse_string(std::string(filter_string ? filter_string : ""));
  }

  MimeTypeFilter(const std::string& filter_string) {
    parse_string(filter_string);
  }

  /**
    @brief  Apply the MIME type filter.
            * Remove types by prefix
            * Insert additioanl types
            * Erase specific types

    @param  types Set of MIME types to filter
    @retval none
  */
  void apply(std::set<std::string>& types) {
    // Apply base filter
    filter_by_root(types);

    // Manually add additional MIME types
    for (auto& mime_type : this->added_types) types.insert(mime_type);

    // Manually remove specific MIME types
    for (auto& mime_type : this->removed_types) types.erase(mime_type);
  }

 private:
  std::set<std::string> allowed_roots;
  std::set<std::string> removed_types;
  std::set<std::string> added_types;

  /**
    @brief  Parse the input string into filter terms

    @param  filter_string String representing filter function
    @retval none
  */
  void parse_string(const std::string& filter_string) {
    this->allowed_roots.clear();
    this->added_types.clear();
    this->removed_types.clear();

    if (filter_string.empty()) return;

    std::istringstream stream(filter_string);

    std::string token;
    while (std::getline(stream, token, ',')) {
      if (token[0] == '+')
        this->added_types.emplace(token, 1);
      else if (token[0] == '-')
        this->removed_types.emplace(token, 1);
      else
        this->allowed_roots.emplace(token);
    }
  }

  /**
    @brief  Remove MIME types that do not match a prefix

    @param  types Set of MIME types to filter
    @retval none
  */
  void filter_by_root(std::set<std::string>& types) {
    // Don't filter if list is empty
    if (this->allowed_roots.empty()) return;

    // Iterate through the supported types and filter by root
    auto it = types.begin();
    while (it != types.end()) {
      const std::string& type = *it;

      // Attempt to find the type in the allowed lists by matching the shortest
      // string
      auto result =
          std::find_if(this->allowed_roots.begin(), this->allowed_roots.end(),
                       [type](const std::string& root) {
                         size_t len = std::min(type.length(), root.length());
                         return (type.compare(0, len, root) == 0);
                       });

      if (result == this->allowed_roots.end()) {
        // Type was NOT in allowed list
        it = types.erase(it);
        continue;
      }

      it++;
    }
  }
};

#endif