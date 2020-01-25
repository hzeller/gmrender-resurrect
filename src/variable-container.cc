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
 */
#include "variable-container.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "upnp_device.h"
#include "upnp_service.h"
#include "xmldoc.h"
#include "xmlescape.h"

VariableContainer::VariableContainer(int variable_num,
                                     const var_meta *var_array)
  : variable_count_(variable_num) {
  assert(variable_num > 0);
  for (int i = 0; i < variable_num; ++i)
    meta_.push_back(&var_array[i]);
  std::sort(meta_.begin(), meta_.end(),
            [](const var_meta *a, const var_meta *b) {
              return a->id < b->id;
            });
  variable_values_.resize(variable_num);
  for (int i = 0; i < variable_num; ++i) {
    assert(meta_[i]->name != NULL);
    assert(meta_[i]->id == i);
    assert(meta_[i]->default_value != NULL);
    variable_values_[i] = meta_[i]->default_value;
  }
}

const std::string &VariableContainer::Get(int var_num, std::string *name)
  const {
  assert(var_num >= 0 && var_num < variable_count_);
  if (name) *name = meta_[var_num]->name;
  return variable_values_[var_num];
}

// Change content of variable with given number to NUL terminated content.
// Returns true if value actually changed and all callbacks were called,
// false if no change was detected.
bool VariableContainer::Set(int variable_num, const std::string &new_value) {
  assert(variable_num >= 0 && variable_num < variable_count_);
  if (variable_values_[variable_num] == new_value)
    return false;
  const std::string var_name = meta_[variable_num]->name;
  std::string old_value = new_value;
  std::swap(variable_values_[variable_num], old_value);
  for (auto cb : callbacks_) {
    cb(variable_num, var_name, old_value, new_value);
  }
  return true;
}

void VariableContainer::RegisterCallback(const ChangeListener &callback) {
  callbacks_.push_back(callback);
}

// -- UPnPLastChangeBuilder
UPnPLastChangeBuilder::UPnPLastChangeBuilder(const char *xml_namespace)
  : xml_namespace_(xml_namespace) {}
UPnPLastChangeBuilder::~UPnPLastChangeBuilder() {
  if (change_event_doc_) {
    xmldoc_free(change_event_doc_);
  }
}

// Add a name/value pair to event on. We just append it
void UPnPLastChangeBuilder::Add(const std::string &name,
                                const std::string &value) {
  if (change_event_doc_ == NULL) {
    change_event_doc_ = xmldoc_new();
    struct xmlelement *toplevel = xmldoc_new_topelement(
      change_event_doc_, "Event", xml_namespace_);
    // Right now, we only have exactly one instance.
    instance_element_ = add_attributevalue_element(
      change_event_doc_, toplevel, "InstanceID", "val", "0");
  }
  xmlelement *const xml_value = add_attributevalue_element(
    change_event_doc_, instance_element_, name.c_str(), "val", value.c_str());
  // HACK!
  // The volume related events need another qualifying
  // attribute that represents the channel. Since all other elements just
  // have one value to transmit without qualifier, the variable container
  // is oblivious about this notion of a qualifier.
  // So this is a bit ugly: if we see the variables in question,
  // we add the attribute manually.
  if (name == "Volume" || name == "VolumeDB" ||
      name == "Mute" || name == "Loudness") {
    xmlelement_set_attribute(change_event_doc_, xml_value, "channel",
                             "Master");
  }
}

// Return the collected change as XML document.
std::string UPnPLastChangeBuilder::toXML() {
  if (!change_event_doc_) return "";

  char *xml_doc_string = xmldoc_tostring(change_event_doc_);
  std::string result = xml_doc_string;
  free(xml_doc_string);  // TODO: avoid double allocation and one free()

  xmldoc_free(change_event_doc_);
  change_event_doc_ = nullptr;
  instance_element_ = nullptr;

  return result;
}

// -- UPnPLastChangeCollector
UPnPLastChangeCollector::UPnPLastChangeCollector(
  VariableContainer *variable_container,
  const char *event_xml_namespace,
  upnp_device *upnp_device, const char *service_id)
  : variable_container_(variable_container), upnp_device_(upnp_device),
    service_id_(service_id),
    builder_(new UPnPLastChangeBuilder(event_xml_namespace)) {
  // Create initial LastChange that contains all variables in their
  // current state. This might help devices that silently re-connect
  // without proper registration.
  // Also determine, which variable is actually the "LastChange" one.
  last_change_variable_num_ = -1;
  const int var_count = variable_container->variable_count();
  for (int i = 0; i < var_count; ++i) {
    std::string name;
    const std::string &value = variable_container->Get(i, &name);
    if (name == "LastChange") {
      last_change_variable_num_ = i;
      continue;
    }
    // Send over all variables except "LastChange" itself.
    builder_->Add(name, value);
  }
  assert(last_change_variable_num_ >= 0);  // we expect to have one.
  // The state change variable itself is not eventable.
  AddIgnore(last_change_variable_num_);
  Notify();

  variable_container->RegisterCallback(
    [this](int variable_num, const std::string &var_name,
           const std::string &old_value,
           const std::string &new_value) {
      ReceiveChange(variable_num, var_name, old_value, new_value);
    });
 }

void UPnPLastChangeCollector::AddIgnore(int variable_num) {
  not_eventable_variables_.insert(variable_num);
}

void UPnPLastChangeCollector::Start() {
  open_transactions_ += 1;
}

void UPnPLastChangeCollector::Finish() {
  assert(open_transactions_ >= 1);
  open_transactions_ -= 1;
  Notify();
}

// TODO(hzeller): add rate limiting. The standard talks about some limited
// amount of events per time-unit.
void UPnPLastChangeCollector::Notify() {
  if (open_transactions_ != 0) return;

  const std::string &xml_doc_string = builder_->toXML();
  if (xml_doc_string.empty()) return;

  // Only if there is actually a change, send it over.
  if (variable_container_->Set(last_change_variable_num_, xml_doc_string)) {
    const char *varnames[] = {"LastChange", NULL};
    const char *varvalues[] = {NULL, NULL};
    // Yes, now, the whole XML document is encapsulated in
    // XML so needs to be XML quoted. The time around 2000 was
    // pretty sick - people did everything in XML.
    varvalues[0] = xmlescape(xml_doc_string.c_str(), 0);
    upnp_device_notify(upnp_device_, service_id_, varnames, varvalues,  1);
    free((char *)varvalues[0]);
  }
}

void UPnPLastChangeCollector::ReceiveChange(int var_num,
                                            const std::string &var_name,
                                            const std::string &old_value,
                                            const std::string &new_value) {
  if (not_eventable_variables_.find(var_num) != not_eventable_variables_.end())
    return;  // ignore changes on non-eventable variables.
  builder_->Add(var_name, new_value);
  Notify();
}
