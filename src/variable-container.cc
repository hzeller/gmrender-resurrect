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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
struct upnp_last_change_builder {
  const char *xml_namespace;
  struct xmldoc *change_event_doc;
  struct xmlelement *instance_element;
};

upnp_last_change_builder_t *UPnPLastChangeBuilder_new(
    const char *xml_namespace) {
  upnp_last_change_builder_t *result =
      (upnp_last_change_builder_t *)malloc(sizeof(upnp_last_change_builder_t));
  result->xml_namespace = xml_namespace;
  result->change_event_doc = NULL;
  result->instance_element = NULL;
  return result;
}

void UPnPLastChangeBuilder_delete(upnp_last_change_builder_t *builder) {
  if (builder->change_event_doc != NULL) {
    xmldoc_free(builder->change_event_doc);
  }
  free(builder);
}

void UPnPLastChangeBuilder_add(upnp_last_change_builder_t *builder,
                               const char *name, const char *value) {
  assert(name != NULL);
  assert(value != NULL);
  if (builder->change_event_doc == NULL) {
    builder->change_event_doc = xmldoc_new();
    struct xmlelement *toplevel = xmldoc_new_topelement(
        builder->change_event_doc, "Event", builder->xml_namespace);
    // Right now, we only have exactly one instance.
    builder->instance_element = add_attributevalue_element(
        builder->change_event_doc, toplevel, "InstanceID", "val", "0");
  }
  struct xmlelement *xml_value;
  xml_value = add_attributevalue_element(
      builder->change_event_doc, builder->instance_element, name, "val", value);
  // HACK!
  // The volume related events need another qualifying
  // attribute that represents the channel. Since all other elements just
  // have one value to transmit without qualifier, the variable container
  // is oblivious about this notion of a qualifier.
  // So this is a bit ugly: if we see the variables in question,
  // we add the attribute manually.
  if (strcmp(name, "Volume") == 0 || strcmp(name, "VolumeDB") == 0 ||
      strcmp(name, "Mute") == 0 || strcmp(name, "Loudness") == 0) {
    xmlelement_set_attribute(builder->change_event_doc, xml_value, "channel",
                             "Master");
  }
}

char *UPnPLastChangeBuilder_to_xml(upnp_last_change_builder_t *builder) {
  if (builder->change_event_doc == NULL) return NULL;

  char *xml_doc_string = xmldoc_tostring(builder->change_event_doc);
  xmldoc_free(builder->change_event_doc);
  builder->change_event_doc = NULL;
  builder->instance_element = NULL;
  return xml_doc_string;
}

// -- UPnPLastChangeCollector
UPnPLastChangeCollector::UPnPLastChangeCollector(
  VariableContainer *variable_container,
  const char *event_xml_namespace,
  upnp_device *upnp_device, const char *service_id)
  : variable_container_(variable_container), upnp_device_(upnp_device),
    service_id_(service_id),
    builder_(UPnPLastChangeBuilder_new(event_xml_namespace)) {
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
    UPnPLastChangeBuilder_add(builder_, name.c_str(), value.c_str());
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

  char *xml_doc_string = UPnPLastChangeBuilder_to_xml(builder_);
  if (xml_doc_string == NULL) return;

  // Only if there is actually a change, send it over.
  if (variable_container_->Set(last_change_variable_num_, xml_doc_string)) {
    const char *varnames[] = {"LastChange", NULL};
    const char *varvalues[] = {NULL, NULL};
    // Yes, now, the whole XML document is encapsulated in
    // XML so needs to be XML quoted. The time around 2000 was
    // pretty sick - people did everything in XML.
    varvalues[0] = xmlescape(xml_doc_string, 0);
    upnp_device_notify(upnp_device_, service_id_, varnames, varvalues,  1);
    free((char *)varvalues[0]);
  }
  free(xml_doc_string);
}

void UPnPLastChangeCollector::ReceiveChange(int var_num,
                                            const std::string &var_name,
                                            const std::string &old_value,
                                            const std::string &new_value) {
  if (not_eventable_variables_.find(var_num) != not_eventable_variables_.end())
    return;  // ignore changes on non-eventable variables.
  UPnPLastChangeBuilder_add(builder_, var_name.c_str(), new_value.c_str());
  Notify();
}
