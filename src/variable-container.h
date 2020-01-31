// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/*
 * Copyright (C) 2013 Henner Zeller
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
 * -----------------
 *
 * Helpers for keeping track of server state variables. UPnP is about syncing
 * state between server and connected controllers and it does so by variables
 * (such as 'CurrentTrackDuration') that can be queried and whose changes
 * can be actively sent to parties that have registered for updates.
 * However, changes are not sent individually when a variable changes
 * but instead encapsulated in XML in a 'LastChange' variable, that contains
 * recent changes since the last update.
 *
 * These utility classes are here to help getting this done:
 *
 * variable_container - handling a bunch of variables containting NUL
 *   terminated strings, allowing C-callbacks to be called when content changes
 *   and differs from previous value.
 *
 * upnp_last_change_builder - a builder for the LastChange XML document
 *   containing name/value pairs of variables.
 *
 * upnp_last_change_collector - handling of the LastChange variable in UPnP.
 *   Hooks into the callback mechanism of the variable_container to assemble
 *   the LastChange variable to be sent over (using the last change builder).
 *
 */
#ifndef VARIABLE_CONTAINER_H
#define VARIABLE_CONTAINER_H

#include <string>
#include <functional>
#include <vector>
#include <set>

#include "xmldoc.h"

struct var_meta;
struct upnp_device;

class VariableContainer {
public:
  using MetaContainer = std::vector<const var_meta *>;

  // TODO: read from initializer list.
  VariableContainer(int variable_num,
                    const struct var_meta *var_array);
  ~VariableContainer();

  // Get number of variables.
  int variable_count() const { return variable_count_; }

  // Get meta-data.
  // TODO(hzeller): this breaks abstraction, but this is to make sure to
  // simplify the transition.
  const MetaContainer &meta() const { return meta_; }

  // Get variable name/value. if OUT parameter 'name' is not NULL, returns
  // name of variable for given number.
  // Returns current value of variable.
  const std::string &Get(int var_num, std::string *name = nullptr) const;

  // Change content of variable with given number to NUL terminated content.
  // Returns true if value actually changed and all callbacks were called,
  // false if no change was detected.
  bool Set(int variable_num, const std::string &new_value);

  // Callback handling. Whenever a variable changes, the callback is called.
  // Be careful when changing variables in the original container as this will
  // trigger recursive calls to the container.
  using ChangeListener =
    std::function<void(int var_num, const std::string &var_name,
                       const std::string &old_value,
                       const std::string &new_value)>;
  void RegisterCallback(const ChangeListener &callback);

private:
  const int variable_count_;
  MetaContainer meta_;           // TODO: make this a map of sorts.
  std::vector<std::string> variable_values_;
  std::vector<ChangeListener> callbacks_;
};

// -- UPnP LastChange Builder - builds a LastChange XML document from
// added name/value pairs.
class UPnPLastChangeBuilder {
public:
  UPnPLastChangeBuilder(const char *xml_namespace);

  // Add a name/value pair to event on.
  void Add(const std::string &name, const std::string &value);

  // Return the collected change as XML document.
  std::string toXML();

private:
  const char *const xml_ns_;
  std::unique_ptr<XMLDoc> change_event_doc_;
  XMLElement instance_element_;
};

class UPnPLastChangeCollector {
public:
  // Create a new last change collector that registers at the
  // "variable_container" for changes in variables. It assembles a LastChange
  // event and sends it to the given "upnp_device".
  // The variable_container is expected to contain one variable with name
  // "LastChange", otherwise this collector is not applicable and fails.
  UPnPLastChangeCollector(VariableContainer *variable_container,
                          const char *event_xml_namespace,
                          upnp_device *upnp_device, const char *service_id);

  // Set variable number that should be ignored in eventing.
  // TODO(hzeller): get this from the meta-data.
  void AddIgnore(int variable_num);

  // If we know that there are a couple of changes upcoming, we can
  // 'start' a transaction and tell the collector to keep collecting until we
  // 'finish'. This can be nested.
  void Start();
  void Finish();

private:
  void Notify();
  void ReceiveChange(int var_num, const std::string &var_name,
                     const std::string &old_value,
                     const std::string &new_value);

  VariableContainer *const variable_container_;
  struct upnp_device *const upnp_device_;
  const char *const service_id_;
  UPnPLastChangeBuilder *const builder_;

  int last_change_variable_num_;      // the LastChange variable we manipulate.
  std::set<int> not_eventable_variables_;  // Variables to ignore eventing on.
  int open_transactions_ = 0;
};

#endif /* VARIABLE_CONTAINER_H */
