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

/**
  @brief  Modifies a set of MIME types by filtering the set, and adding or
  removing additional types manually. Filter is initalize by supplying a string
  of CSV values.
*/
class MimeTypeFilter {
 public:
  MimeTypeFilter(const char* filter_string) {
    // Ensure input is not NULL
    ParseString(std::string(filter_string ? filter_string : ""));
  }

  /**
    @brief  Apply the MIME type filter.
            * Remove types by prefix
            * Insert additioanl types
            * Erase specific types

    @param  types Set of MIME types to filter
    @retval none
  */
  void Apply(std::set<std::string>* types) {
    // Validate IN-OUT pointer
    if (types == nullptr) return;

    // Apply base filter
    FilterByRoot(types);

    // Manually add additional MIME types
    for (auto& mime_type : added_types_) types->insert(mime_type);

    // Manually remove specific MIME types
    for (auto& mime_type : removed_types_) types->erase(mime_type);
  }

 private:
  /**
    @brief  Parse the input string into filter terms

    @param  filter_string String representing filter function
    @retval none
  */
  void ParseString(const std::string& filter_string) {
    allowed_roots_.clear();
    added_types_.clear();
    removed_types_.clear();

    if (filter_string.empty()) return;

    std::istringstream stream(filter_string);

    std::string token;
    while (std::getline(stream, token, ',')) {
      switch (token[0]) {
        case '+':
          added_types_.emplace(token, 1);
          break;
        case '-':
          removed_types_.emplace(token, 1);
          break;
        default:
          allowed_roots_.emplace(token);
          break;
      }
    }
  }

  /**
    @brief  Remove MIME types that do not match a prefix

    @param  types Set of MIME types to filter
    @retval none
  */
  void FilterByRoot(std::set<std::string>* types) {
    // Validate IN-OUT pointer
    if (types == nullptr) return;

    // Don't filter if list is empty
    if (allowed_roots_.empty()) return;

    // Iterate through the supported types and filter by root
    auto it = types->begin();
    while (it != types->end()) {
      const std::string& type = *it;

      // Attempt to find the type in the allowed lists by matching the shortest
      // string
      auto result = std::find_if(allowed_roots_.begin(), allowed_roots_.end(),
                                 [&type](const std::string& root) {
                                   size_t len =
                                       std::min(type.length(), root.length());
                                   return (type.compare(0, len, root) == 0);
                                 });

      if (result == allowed_roots_.end()) {
        // Type was NOT in allowed list
        it = types->erase(it);
        continue;
      }

      it++;
    }
  }

  std::set<std::string> allowed_roots_;
  std::set<std::string> removed_types_;
  std::set<std::string> added_types_;
};

#endif