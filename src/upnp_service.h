// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* upnp.h - Generic UPnP definitions
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

#ifndef _UPNP_SERVICE_H
#define _UPNP_SERVICE_H

#include <ithread.h>
#include <upnp.h>
#include "upnp_compat.h"
#include "variable-container.h"

struct action;
struct service;
struct action_event;
struct variable_container;
struct upnp_last_change_collector;

struct action {
  const char *action_name;
  int (*callback)(struct action_event *);
};

enum class ParamDir {
  kIn,
  kOut,
};

struct argument {
  const char *name;
  ParamDir direction;
  int statevar;
};

enum class DataType {
  kString,
  kBoolean,
  kInt2,
  kInt4,
  kUint2,
  kUint4,
  kUnknown,
};

enum class Eventing {
  kNo,
  kYes
};

struct param_range {
  long long min;
  long long max;
  long long step;
};

struct var_meta {
  int id;
  const char *name;
  const char *default_value;
  Eventing sendevents;
  DataType datatype;
  const char **allowed_values;
  struct param_range *allowed_range;
};

struct icon {
  int width;
  int height;
  int depth;
  const char *url;
  const char *mimetype;
};

struct service {
  ithread_mutex_t *service_mutex;
  const char *service_id;
  const char *service_type;
  const char *scpd_url;
  const char *control_url;
  const char *event_url;
  const char *event_xml_ns;
  struct action *actions;
  struct argument **action_arguments;
  VariableContainer *variable_container;
  UPnPLastChangeCollector *last_change;
  int command_count;
};

struct action_event {
  UpnpActionRequest *request;
  int status;
  struct service *service;
  struct upnp_device *device;
};

struct action *find_action(struct service *event_service,
                           const char *action_name);

char *upnp_get_scpd(struct service *srv);

#endif /* _UPNP_SERVICE_H */
