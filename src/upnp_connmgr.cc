// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* upnp_connmgr.c - UPnP Connection Manager routines
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 * Copyright (C) 2019        Tucker Kern
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <set>
#include <string>

#include <ithread.h>
#include <upnp.h>

#include "upnp_connmgr.h"

#include "logging.h"
#include "mime_type_filter.h"
#include "output.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"

#define CONNMGR_TYPE "urn:schemas-upnp-org:service:ConnectionManager:1"

// Changing this back now to what it is supposed to be, let's see what happens.
// For some reason (predates me), this was explicitly commented out and
// set to the service type; were there clients that were confused about the
// right use of the service-ID ? Setting this back, let's see what happens.
#define CONNMGR_SERVICE_ID "urn:upnp-org:serviceId:ConnectionManager"
//#define CONNMGR_SERVICE_ID CONNMGR_TYPE
#define CONNMGR_SCPD_URL "/upnp/renderconnmgrSCPD.xml"
#define CONNMGR_CONTROL_URL "/upnp/control/renderconnmgr1"
#define CONNMGR_EVENT_URL "/upnp/event/renderconnmgr1"

typedef enum {
  CONNMGR_VAR_AAT_CONN_MGR,
  CONNMGR_VAR_SINK_PROTO_INFO,
  CONNMGR_VAR_AAT_CONN_STATUS,
  CONNMGR_VAR_AAT_AVT_ID,
  CONNMGR_VAR_AAT_DIR,
  CONNMGR_VAR_AAT_RCS_ID,
  CONNMGR_VAR_AAT_PROTO_INFO,
  CONNMGR_VAR_AAT_CONN_ID,
  CONNMGR_VAR_SRC_PROTO_INFO,
  CONNMGR_VAR_CUR_CONN_IDS,
  CONNMGR_VAR_COUNT
} connmgr_variable;

typedef enum {
  CONNMGR_CMD_GETCURRENTCONNECTIONIDS,
  CONNMGR_CMD_SETCURRENTCONNECTIONINFO,
  CONNMGR_CMD_GETPROTOCOLINFO,
  CONNMGR_CMD_PREPAREFORCONNECTION,
  // CONNMGR_CMD_CONNECTIONCOMPLETE,
  CONNMGR_CMD_COUNT
} connmgr_cmd;

static struct argument arguments_getprotocolinfo[] = {
    {"Source", ParamDir::kOut, CONNMGR_VAR_SRC_PROTO_INFO},
    {"Sink", ParamDir::kOut, CONNMGR_VAR_SINK_PROTO_INFO},
    {NULL},
};

static struct argument arguments_getcurrentconnectionids[] = {
    {"ConnectionIDs", ParamDir::kOut, CONNMGR_VAR_CUR_CONN_IDS}, {NULL}};

static struct argument arguments_setcurrentconnectioninfo[] = {
    {"ConnectionID", ParamDir::kIn, CONNMGR_VAR_AAT_CONN_ID},
    {"RcsID", ParamDir::kOut, CONNMGR_VAR_AAT_RCS_ID},
    {"AVTransportID", ParamDir::kOut, CONNMGR_VAR_AAT_AVT_ID},
    {"ProtocolInfo", ParamDir::kOut, CONNMGR_VAR_AAT_PROTO_INFO},
    {"PeerConnectionManager", ParamDir::kOut, CONNMGR_VAR_AAT_CONN_MGR},
    {"PeerConnectionID", ParamDir::kOut, CONNMGR_VAR_AAT_CONN_ID},
    {"Direction", ParamDir::kOut, CONNMGR_VAR_AAT_DIR},
    {"Status", ParamDir::kOut, CONNMGR_VAR_AAT_CONN_STATUS},
    {NULL}};
static struct argument arguments_prepareforconnection[] = {
    {"RemoteProtocolInfo", ParamDir::kIn, CONNMGR_VAR_AAT_PROTO_INFO},
    {"PeerConnectionManager", ParamDir::kIn, CONNMGR_VAR_AAT_CONN_MGR},
    {"PeerConnectionID", ParamDir::kIn, CONNMGR_VAR_AAT_CONN_ID},
    {"Direction", ParamDir::kIn, CONNMGR_VAR_AAT_DIR},
    {"ConnectionID", ParamDir::kOut, CONNMGR_VAR_AAT_CONN_ID},
    {"AVTransportID", ParamDir::kOut, CONNMGR_VAR_AAT_AVT_ID},
    {"RcsID", ParamDir::kOut, CONNMGR_VAR_AAT_RCS_ID},
    {NULL}};
// static struct argument *arguments_connectioncomplete[] = {
//	{ "ConnectionID", ParamDir::kIn, CONNMGR_VAR_AAT_CONN_ID },
//        NULL
//};

static struct argument* argument_list[] = {
    [CONNMGR_CMD_GETCURRENTCONNECTIONIDS] = arguments_getcurrentconnectionids,
    [CONNMGR_CMD_SETCURRENTCONNECTIONINFO] = arguments_setcurrentconnectioninfo,
    [CONNMGR_CMD_GETPROTOCOLINFO] = arguments_getprotocolinfo,

    [CONNMGR_CMD_PREPAREFORCONNECTION] = arguments_prepareforconnection,
    //[CONNMGR_CMD_CONNECTIONCOMPLETE] = arguments_connectioncomplete,
    [CONNMGR_CMD_COUNT] = NULL};

static const char* connstatus_values[] = {"OK",
                                          "ContentFormatMismatch",
                                          "InsufficientBandwidth",
                                          "UnreliableChannel",
                                          "Unknown",
                                          NULL};
static const char* direction_values[] = {"Input", "Output", NULL};

static ithread_mutex_t connmgr_mutex;

/**
    @brief  Augement the supported MIME types set with additional
            types for improved compatibilty

    @param  types MimeTypeSet containing supported MIME types
    @retval none
*/
void connmgr_augment_supported_types(Output::MimeTypeSet& types) {
  if (types.count("audio/mpeg")) {
    types.emplace("audio/x-mpeg");

    // BubbleUPnP uses audio/x-scpl as an indicator to know if the
    // renderer can handle it (otherwise it will proxy).
    // Simple claim: if we can handle mpeg, then we can handle
    // shoutcast.
    // (For more accurate answer: we'd to check if all of
    // mpeg, aac, aacp, ogg are supported).
    types.emplace("audio/x-scpls");

    // This is apparently something sent by the spotifyd
    // https://gitorious.org/spotifyd
    types.emplace("audio/L16;rate=44100;channels=2");
  }

  // Some workaround: some controllers seem to match the version without
  // x-, some with; though the mime-type is correct with x-, these formats
  // seem to be common enough to sometimes be used without.
  // If this works, we should probably collect all of these
  // in a set emit always both, foo/bar and foo/x-bar, as it is a similar
  // work-around as seen above with mpeg -> x-mpeg.
  if (types.count("audio/x-alac")) types.emplace("audio/alac");

  if (types.count("audio/x-aiff")) types.emplace("audio/aiff");

  if (types.count("audio/x-m4a")) {
    types.emplace("audio/m4a");
    types.emplace("audio/mp4");
  }

  // There seem to be all kinds of mime types out there that start with
	// "audio/" but are not explicitly supported by gstreamer. Let's just
	// tell the controller that we can handle everything "audio/*" and hope
	// for the best.
  types.emplace("audio/*");
}

int connmgr_init(const char* mime_filter_string) {
  struct service* srv = upnp_connmgr_get_service();

  // Get supported MIME types from the output module
  Output::MimeTypeSet supported_types = Output::GetSupportedMedia();

  // Augment the set for better compatibility
  connmgr_augment_supported_types(supported_types);

  // Construct and apply the MIME type filter
  MimeTypeFilter filter(mime_filter_string);
  filter.apply(supported_types);

  std::string protoInfo;
  for (auto& mime_type : supported_types) {
    Log_info("connmgr", "Registering support for '%s'", mime_type.c_str());
    protoInfo += ("http-get:*:" + mime_type + ":*,");
  }

  if (protoInfo.empty() == false) {
    // Truncate final comma
    protoInfo.pop_back();
    srv->variable_container->Set(CONNMGR_VAR_SINK_PROTO_INFO, protoInfo);
  }

  return 0;
}

static int get_protocol_info(struct action_event* event) {
  upnp_append_variable(event, CONNMGR_VAR_SRC_PROTO_INFO, "Source");
  upnp_append_variable(event, CONNMGR_VAR_SINK_PROTO_INFO, "Sink");
  return event->status;
}

static int get_current_conn_ids(struct action_event* event) {
  int rc = -1;
  upnp_add_response(event, "ConnectionIDs", "0");
  /// rc = upnp_append_variable(event, CONNMGR_VAR_CUR_CONN_IDS,
  //			    "ConnectionIDs");
  return rc;
}

static int prepare_for_connection(struct action_event* event) {
  upnp_append_variable(event, CONNMGR_VAR_CUR_CONN_IDS, "ConnectionID");
  upnp_append_variable(event, CONNMGR_VAR_AAT_AVT_ID, "AVTransportID");
  upnp_append_variable(event, CONNMGR_VAR_AAT_RCS_ID, "RcsID");
  return 0;
}

static int get_current_conn_info(struct action_event* event) {
  const char* value = upnp_get_string(event, "ConnectionID");
  if (value == NULL) {
    return -1;
  }
  Log_info("connmgr", "Query ConnectionID='%s'", value);

  upnp_append_variable(event, CONNMGR_VAR_AAT_RCS_ID, "RcsID");
  upnp_append_variable(event, CONNMGR_VAR_AAT_AVT_ID, "AVTransportID");
  upnp_append_variable(event, CONNMGR_VAR_AAT_PROTO_INFO, "ProtocolInfo");
  upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_MGR,
                       "PeerConnectionManager");
  upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_ID, "PeerConnectionID");
  upnp_append_variable(event, CONNMGR_VAR_AAT_DIR, "Direction");
  upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_STATUS, "Status");
  return 0;
}

static struct action connmgr_actions[] = {
    [CONNMGR_CMD_GETCURRENTCONNECTIONIDS] = {"GetCurrentConnectionIDs",
                                             get_current_conn_ids},
    [CONNMGR_CMD_SETCURRENTCONNECTIONINFO] = {"GetCurrentConnectionInfo",
                                              get_current_conn_info},
    [CONNMGR_CMD_GETPROTOCOLINFO] = {"GetProtocolInfo", get_protocol_info},
    [CONNMGR_CMD_PREPAREFORCONNECTION] =
        {"PrepareForConnection", prepare_for_connection}, /* optional */
    //[CONNMGR_CMD_CONNECTIONCOMPLETE] =	{"ConnectionComplete", NULL},
    ///* optional */
    [CONNMGR_CMD_COUNT] = {NULL, NULL}};

struct service* upnp_connmgr_get_service(void) {
  static struct service connmgr_service_ = {
      .service_mutex = &connmgr_mutex,
      .service_id = CONNMGR_SERVICE_ID,
      .service_type = CONNMGR_TYPE,
      .scpd_url = CONNMGR_SCPD_URL,
      .control_url = CONNMGR_CONTROL_URL,
      .event_url = CONNMGR_EVENT_URL,
      .event_xml_ns = NULL,  // we never send change events.
      .actions = connmgr_actions,
      .action_arguments = argument_list,
      .variable_container = NULL,  // initialized below
      .last_change = NULL,
      .command_count = CONNMGR_CMD_COUNT,
  };

  static struct var_meta connmgr_var_meta[] = {
      {CONNMGR_VAR_SRC_PROTO_INFO, "SourceProtocolInfo", "", Eventing::kYes,
       DataType::kString, NULL, NULL},
      {CONNMGR_VAR_SINK_PROTO_INFO, "SinkProtocolInfo",
       "http-get:*:audio/mpeg:*", Eventing::kYes, DataType::kString, NULL,
       NULL},
      {CONNMGR_VAR_CUR_CONN_IDS, "CurrentConnectionIDs", "0", Eventing::kYes,
       DataType::kString, NULL, NULL},

      {CONNMGR_VAR_AAT_CONN_STATUS, "A_ARG_TYPE_ConnectionStatus", "Unknown",
       Eventing::kNo, DataType::kString, connstatus_values, NULL},
      {CONNMGR_VAR_AAT_CONN_MGR, "A_ARG_TYPE_ConnectionManager", "/",
       Eventing::kNo, DataType::kString, NULL, NULL},
      {CONNMGR_VAR_AAT_DIR, "A_ARG_TYPE_Direction", "Input", Eventing::kNo,
       DataType::kString, direction_values, NULL},
      {CONNMGR_VAR_AAT_PROTO_INFO, "A_ARG_TYPE_ProtocolInfo",
       ":::", Eventing::kNo, DataType::kString, NULL, NULL},
      {CONNMGR_VAR_AAT_CONN_ID, "A_ARG_TYPE_ConnectionID", "-1", Eventing::kNo,
       DataType::kInt4, NULL, NULL},
      {CONNMGR_VAR_AAT_AVT_ID, "A_ARG_TYPE_AVTransportID", "0", Eventing::kNo,
       DataType::kInt4, NULL, NULL},
      {CONNMGR_VAR_AAT_RCS_ID, "A_ARG_TYPE_RcsID", "0", Eventing::kNo,
       DataType::kInt4, NULL, NULL},

      {CONNMGR_VAR_COUNT, NULL, NULL, Eventing::kNo, DataType::kUnknown, NULL,
       NULL}};

  if (connmgr_service_.variable_container == NULL) {
    connmgr_service_.variable_container =
        new VariableContainer(CONNMGR_VAR_COUNT, connmgr_var_meta);
    // no changes expected; no collector.
  }
  return &connmgr_service_;
}
