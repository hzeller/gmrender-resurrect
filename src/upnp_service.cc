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

#include "config.h"

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

static struct xmlelement *gen_specversion(struct xmldoc *doc, int major,
                                          int minor) {
  struct xmlelement *top;

  top = xmlelement_new(doc, "specVersion");

  add_value_element_int(doc, top, "major", major);
  add_value_element_int(doc, top, "minor", minor);

  return top;
}

static struct xmlelement *gen_scpd_action(
  struct xmldoc *doc,
  struct action *act,
  struct argument *arglist,
  const VariableContainer::MetaContainer &meta) {
  struct xmlelement *top;
  struct xmlelement *parent, *child;

  top = xmlelement_new(doc, "action");

  add_value_element(doc, top, "name", act->action_name);
  if (arglist) {
    struct argument *arg;
    int j;
    parent = xmlelement_new(doc, "argumentList");
    xmlelement_add_element(doc, top, parent);
    /* a NULL name is the sentinel for 'end of list' */
    for (j = 0; (arg = &arglist[j], arg->name); j++) {
      child = xmlelement_new(doc, "argument");
      add_value_element(doc, child, "name", arg->name);
      add_value_element(doc, child, "direction",
                        (arg->direction == ParamDir::kIn) ? "in" : "out");
      add_value_element(doc, child, "relatedStateVariable",
                        meta[arg->statevar]->name);
      xmlelement_add_element(doc, parent, child);
    }
  }
  return top;
}

static struct xmlelement *gen_scpd_actionlist(struct xmldoc *doc,
                                              struct service *srv) {
  struct xmlelement *top;
  struct xmlelement *child;
  int i;

  top = xmlelement_new(doc, "actionList");
  for (i = 0; i < srv->command_count; i++) {
    struct action *act;
    struct argument *arglist;
    act = &(srv->actions[i]);
    arglist = srv->action_arguments[i];
    if (act) {
      child = gen_scpd_action(doc, act, arglist,
                              srv->variable_container->meta());
      xmlelement_add_element(doc, top, child);
    }
  }
  return top;
}

static struct xmlelement *gen_scpd_statevar(struct xmldoc *doc,
                                            const struct var_meta *meta) {
  struct xmlelement *top, *parent;
  const char **valuelist;
  struct param_range *range;

  valuelist = meta->allowed_values;
  range = meta->allowed_range;

  top = xmlelement_new(doc, "stateVariable");

  xmlelement_set_attribute(doc, top, "sendEvents",
                           (meta->sendevents == Eventing::kYes) ? "yes" : "no");
  add_value_element(doc, top, "name", meta->name);
  add_value_element(doc, top, "dataType", ParamDatatypeName(meta->datatype));

  if (valuelist) {
    const char *allowed_value;
    int i;
    parent = xmlelement_new(doc, "allowedValueList");
    xmlelement_add_element(doc, top, parent);
    for (i = 0; (allowed_value = valuelist[i]); i++) {
      add_value_element(doc, parent, "allowedValue", allowed_value);
    }
  }
  if (range) {
    parent = xmlelement_new(doc, "allowedValueRange");
    xmlelement_add_element(doc, top, parent);
    add_value_element_long(doc, parent, "minimum", range->min);
    add_value_element_long(doc, parent, "maximum", range->max);
    if (range->step != 0L) {
      add_value_element_long(doc, parent, "step", range->step);
    }
  }
  assert(!(valuelist && range));  // Discrete values _and_ range ?

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
      add_value_element(doc, top, "defaultValue", meta->default_value);
    }
  }

  return top;
}

static struct xmlelement *gen_scpd_servicestatetable(struct xmldoc *doc,
                                                     struct service *srv) {
  struct xmlelement *top;
  struct xmlelement *child;

  top = xmlelement_new(doc, "serviceStateTable");
  for (auto meta : srv->variable_container->meta()) {
    child = gen_scpd_statevar(doc, meta);
    xmlelement_add_element(doc, top, child);
  }
  return top;
}

static struct xmldoc *generate_scpd(struct service *srv) {
  struct xmldoc *doc;
  struct xmlelement *root;
  struct xmlelement *child;

  doc = xmldoc_new();

  root = xmldoc_new_topelement(doc, "scpd", "urn:schemas-upnp-org:service-1-0");
  child = gen_specversion(doc, 1, 0);
  xmlelement_add_element(doc, root, child);

  child = gen_scpd_actionlist(doc, srv);
  xmlelement_add_element(doc, root, child);

  child = gen_scpd_servicestatetable(doc, srv);
  xmlelement_add_element(doc, root, child);

  return doc;
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

char *upnp_get_scpd(struct service *srv) {
  char *result = NULL;
  struct xmldoc *doc;

  doc = generate_scpd(srv);
  if (doc != NULL) {
    result = xmldoc_tostring(doc);
    xmldoc_free(doc);
  }
  return result;
}
