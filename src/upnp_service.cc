// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* upnp.c - Generic UPnP routines
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
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
#include "upnp_service.h"

#include <assert.h>
#include <errno.h>
#include <ithread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "variable-container.h"
#include "xmldoc.h"

static const char *ParamDatatypeName(DataType t) {
  switch (t) {
  case DataType::kString: return "string";
  case DataType::kBoolean: return "boolean";
  case DataType::kInt2: return "i2";
  case DataType::kInt4: return "i4";
  case DataType::kUint2: return "ui2";
  case DataType::kUint4: return "ui4";
  case DataType::kUnknown: return nullptr;
    /* no default to let compiler warn about new types */
  }
  return nullptr;  // not reached.
}

static void add_specversion(XMLElement parent, int major, int minor) {
  auto specVersion = parent.AddElement("specVersion");
  specVersion.AddElement("major").SetValue(major);
  specVersion.AddElement("minor").SetValue(minor);
}

static void add_scpd_action(XMLElement parent,
                            struct action *act,
                            struct argument *arglist,
                            const VariableContainer::MetaContainer &meta) {
  if (!act) return;

  auto action = parent.AddElement("action");
  action.AddElement("name").SetValue(act->action_name);
  if (arglist) {
    struct argument *arg;
    int j;
    auto argumentList = action.AddElement("argumentList");
    /* a NULL name is the sentinel for 'end of list' */
    for (j = 0; (arg = &arglist[j], arg->name); j++) {
      auto singleArg = argumentList.AddElement("argument");
      singleArg.AddElement("name").SetValue(arg->name);
      singleArg.AddElement("direction")
        .SetValue(arg->direction == ParamDir::kIn ? "in" : "out");
      singleArg.AddElement("relatedStateVariable").SetValue(meta[arg->statevar]->name);
    }
  }
}

static void add_scpd_actionlist(XMLElement parent, const service *srv) {
  auto actionList = parent.AddElement("actionList");
  for (int i = 0; i < srv->command_count; i++) {
    struct action *act;
    struct argument *arglist;
    act = &(srv->actions[i]);
    arglist = srv->action_arguments[i];
    add_scpd_action(actionList, act, arglist,
                    srv->variable_container->meta());
  }
}

static void add_scpd_statevar(XMLElement parent, const struct var_meta *meta) {
  // Discrete values _and_ range ?
  assert(!(meta->allowed_values && meta->allowed_range));

  auto stateVar = parent.AddElement("stateVariable")
    .SetAttribute("sendEvents",
                  (meta->sendevents == Eventing::kYes) ? "yes" : "no");
  stateVar.AddElement("name").SetValue(meta->name);
  stateVar.AddElement("dataType").SetValue(ParamDatatypeName(meta->datatype));

  if (const char **valuelist = meta->allowed_values) {
    const char *allowed_value;
    int i;
    auto allowedValueList = stateVar.AddElement("allowedValueList");
    for (i = 0; (allowed_value = valuelist[i]); i++) {
      allowedValueList.AddElement("allowedValue").SetValue(allowed_value);
    }
  }

  if (const param_range *range = meta->allowed_range) {
    auto allowedValueRange = stateVar.AddElement("allowedValueRange");
    allowedValueRange.AddElement("minimum").SetValue(range->min);
    allowedValueRange.AddElement("maximum").SetValue(range->max);
    if (range->step != 0L) {
      allowedValueRange.AddElement("step").SetValue(range->step);
    }
  }

  if (meta->default_value) {
    // Reconsider: we never set the default value before, mostly
    // because there/ were 'initial values' (also called default)
    // and default values in the service meta-data. Was it confusion
    // or intent ? Hard to tell.
    //
    // Now, the default/init values are all in the meta-data struct,
    // the XML would be populated with a lot more default values.
    // Let's find what the reference documentation says about this.
    //
    // Previously, there was exactly _one_ variable in the XML that
    // set the default value, which is CurrentPlayMode = "NORMAL".
    // To make sure that we're not breaking anything accidentally,
    // the following condition is _very_ specific keep it this way.
    // (technical changes should be separate from functional changes)
    if (strcmp(meta->name, "CurrentPlayMode") == 0 &&
        strcmp(meta->default_value, "NORMAL") == 0) {
      stateVar.AddElement("defaultValue").SetValue(meta->default_value);
    }
  }
}

static void add_scpd_servicestatetable(XMLElement parent, const service *srv) {
  auto serviceStateTable = parent.AddElement("serviceStateTable");
  for (auto meta : srv->variable_container->meta()) {
    add_scpd_statevar(serviceStateTable, meta);
  }
}

struct action *find_action(struct service *event_service,
                           const char *action_name) {
  struct action *event_action;
  int actionNum = 0;
  if (event_service == NULL) return NULL;
  while (event_action = &(event_service->actions[actionNum]),
         event_action->action_name != NULL) {
    if (strcmp(event_action->action_name, action_name) == 0)
      return event_action;
    actionNum++;
  }
  return NULL;
}

std::string upnp_get_scpd(const service *srv) {
  XMLDoc doc;

  auto root = doc.AddElement("scpd", "urn:schemas-upnp-org:service-1-0");
  add_specversion(root, 1, 0);
  add_scpd_actionlist(root, srv);
  add_scpd_servicestatetable(root, srv);
  return doc.ToXMLString();
}
