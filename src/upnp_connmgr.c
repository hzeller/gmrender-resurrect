/* upnp_connmgr.c - UPnP Connection Manager routines
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <upnp.h>
#include <ithread.h>

// Can't include above upnp.h breaks stdbool?
#include <stdbool.h>
#include <glib.h>

#include "upnp_connmgr.h"

#include "logging.h"
#include "upnp_service.h"
#include "upnp_device.h"
#include "variable-container.h"

#define CONNMGR_TYPE	"urn:schemas-upnp-org:service:ConnectionManager:1"

// Changing this back now to what it is supposed to be, let's see what happens.
// For some reason (predates me), this was explicitly commented out and
// set to the service type; were there clients that were confused about the
// right use of the service-ID ? Setting this back, let's see what happens.
#define CONNMGR_SERVICE_ID "urn:upnp-org:serviceId:ConnectionManager"
//#define CONNMGR_SERVICE_ID CONNMGR_TYPE
#define CONNMGR_SCPD_URL "/upnp/renderconnmgrSCPD.xml"
#define CONNMGR_CONTROL_URL "/upnp/control/renderconnmgr1"
#define CONNMGR_EVENT_URL "/upnp/event/renderconnmgr1"

typedef struct mime_type_filters_t
{
	GSList* allowed_roots;
	GSList* removed_types;
	GSList* added_types;
} mime_type_filters_t;

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
	//CONNMGR_CMD_CONNECTIONCOMPLETE,
	CONNMGR_CMD_COUNT
} connmgr_cmd;

static struct argument arguments_getprotocolinfo[] = {
	{ "Source", PARAM_DIR_OUT, CONNMGR_VAR_SRC_PROTO_INFO },
	{ "Sink", PARAM_DIR_OUT, CONNMGR_VAR_SINK_PROTO_INFO },
        { NULL },
};

static struct argument arguments_getcurrentconnectionids[] = {
	{ "ConnectionIDs", PARAM_DIR_OUT, CONNMGR_VAR_CUR_CONN_IDS },
        { NULL }
};

static struct argument arguments_setcurrentconnectioninfo[] = {
	{ "ConnectionID", PARAM_DIR_IN, CONNMGR_VAR_AAT_CONN_ID },
	{ "RcsID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_RCS_ID },
	{ "AVTransportID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_AVT_ID },
	{ "ProtocolInfo", PARAM_DIR_OUT, CONNMGR_VAR_AAT_PROTO_INFO },
	{ "PeerConnectionManager", PARAM_DIR_OUT, CONNMGR_VAR_AAT_CONN_MGR },
	{ "PeerConnectionID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_CONN_ID },
	{ "Direction", PARAM_DIR_OUT, CONNMGR_VAR_AAT_DIR },
	{ "Status", PARAM_DIR_OUT, CONNMGR_VAR_AAT_CONN_STATUS },
        { NULL }
};
static struct argument arguments_prepareforconnection[] = {
	{ "RemoteProtocolInfo", PARAM_DIR_IN, CONNMGR_VAR_AAT_PROTO_INFO },
	{ "PeerConnectionManager", PARAM_DIR_IN, CONNMGR_VAR_AAT_CONN_MGR },
	{ "PeerConnectionID", PARAM_DIR_IN, CONNMGR_VAR_AAT_CONN_ID },
	{ "Direction", PARAM_DIR_IN, CONNMGR_VAR_AAT_DIR },
	{ "ConnectionID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_CONN_ID },
	{ "AVTransportID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_AVT_ID },
	{ "RcsID", PARAM_DIR_OUT, CONNMGR_VAR_AAT_RCS_ID },
	{ NULL }
};
//static struct argument *arguments_connectioncomplete[] = {
//	{ "ConnectionID", PARAM_DIR_IN, CONNMGR_VAR_AAT_CONN_ID },
//        NULL
//};

static struct argument *argument_list[] = {
	[CONNMGR_CMD_GETCURRENTCONNECTIONIDS] =	arguments_getcurrentconnectionids,
	[CONNMGR_CMD_SETCURRENTCONNECTIONINFO] = arguments_setcurrentconnectioninfo,
	[CONNMGR_CMD_GETPROTOCOLINFO] =	 arguments_getprotocolinfo,

	[CONNMGR_CMD_PREPAREFORCONNECTION] = arguments_prepareforconnection,
	//[CONNMGR_CMD_CONNECTIONCOMPLETE] = arguments_connectioncomplete,
	[CONNMGR_CMD_COUNT]	=	NULL
};

static const char *connstatus_values[] = {
	"OK",
	"ContentFormatMismatch",
	"InsufficientBandwidth",
	"UnreliableChannel",
	"Unknown",
	NULL
};
static const char *direction_values[] = {
	"Input",
	"Output",
	NULL
};

static ithread_mutex_t connmgr_mutex;

static GSList* supported_types_list;

static bool add_mime_type(const char* mime_type)
{
	// Check for duplicate MIME type
	if (g_slist_find_custom(supported_types_list, mime_type, (GCompareFunc) strcmp) != NULL)
		return false;

	// Sorted insert into list
	supported_types_list = g_slist_insert_sorted(supported_types_list, strdup(mime_type), (GCompareFunc) strcmp);

	return true;
}

static bool remove_mime_type(const char* mime_type)
{
	// Check that the list exists
	if (supported_types_list == NULL)
		return false;

	// Search for the MIME type
	GSList* entry = g_slist_find_custom(supported_types_list, mime_type, (GCompareFunc) strcmp);
	if (entry != NULL)
	{
		// Free the string pointer
		free(entry->data);

		// Free the list entry
		supported_types_list = g_slist_delete_link(supported_types_list, entry);
		return true;
	}

	return false;
}

static int g_compare_mime_root(const void* a, const void* b)
{
	size_t aLen = strlen((const char*)a);
	size_t bLen = strlen((const char*)b);

	// Only compare up to the small string
	int min = (aLen < bLen) ? aLen : bLen;

	return strncmp((const char*) a, (const char*) b, min);
}

static void g_add_mime_type(void* data, void* user_data)
{
	add_mime_type((const char*) data);
}

static void g_remove_mime_type(void* data, void* user_data)
{
	remove_mime_type((const char*) data);
}

static void register_mime_type_internal(const char *mime_type) {
	add_mime_type(mime_type);
}

void register_mime_type(const char *mime_type) {
	register_mime_type_internal(mime_type);
	if (strcmp("audio/mpeg", mime_type) == 0) {
		register_mime_type_internal("audio/x-mpeg");

		// BubbleUPnP does not seem to match generic "audio/*" types,
		// but only matches mime-types _exactly_, so we add some here.
		// TODO(hzeller): we already add the "audio/*" mime-type
		// output_gstream.c:scan_caps() which should just work once
		// BubbleUPnP allows for  matching "audio/*". Remove the code
		// here.

		// BubbleUPnP uses audio/x-scpl as an indicator to know if the
		// renderer can handle it (otherwise it will proxy).
		// Simple claim: if we can handle mpeg, then we can handle
		// shoutcast.
		// (For more accurate answer: we'd to check if all of
		// mpeg, aac, aacp, ogg are supported).
		register_mime_type_internal("audio/x-scpls");

		// This is apparently something sent by the spotifyd
		// https://gitorious.org/spotifyd
		register_mime_type("audio/L16;rate=44100;channels=2");
	}

	// Some workaround: some controllers seem to match the version without
	// x-, some with; though the mime-type is correct with x-, these formats
	// seem to be common enough to sometimes be used without.
	// If this works, we should probably collect all of these
	// in a set emit always both, foo/bar and foo/x-bar, as it is a similar
	// work-around as seen above with mpeg -> x-mpeg.
	if (strcmp("audio/x-alac", mime_type) == 0) {
	  register_mime_type_internal("audio/alac");
	}
	if (strcmp("audio/x-aiff", mime_type) == 0) {
	  register_mime_type_internal("audio/aiff");
	}
	if (strcmp("audio/x-m4a", mime_type) == 0) {
	  register_mime_type_internal("audio/m4a");
	  register_mime_type_internal("audio/mp4");
	}
}

static mime_type_filters_t connmgr_parse_mime_filter_string(const char* filter_string)
{
	mime_type_filters_t mime_filter;

	mime_filter.allowed_roots = NULL;
	mime_filter.added_types = NULL;
	mime_filter.removed_types = NULL;

	if (filter_string == NULL)
		return mime_filter;

	char* filters = strdup(filter_string);

	char* saveptr = NULL; // State pointer for strtok_r
	char* token = strtok_r(filters, ",", &saveptr);
	while(token != NULL)
	{
		if (token[0] == '+')
		{
			mime_filter.added_types = g_slist_prepend(mime_filter.added_types,
				strdup(&token[1]));
		}
		else if (token[0] == '-')
		{
			mime_filter.removed_types = g_slist_prepend(mime_filter.removed_types,
				strdup(&token[1]));
		}
		else
		{
			mime_filter.allowed_roots = g_slist_prepend(mime_filter.allowed_roots,
				strdup(token));
		}

		token = strtok_r(NULL, ",", &saveptr);
	}

	free(filters);

	return mime_filter;
}

static void connmgr_filter_mime_type_root(const mime_type_filters_t* mime_filter)
{
	if (mime_filter == NULL || mime_filter->allowed_roots == NULL)
		return;

	// Iterate through the supported types and filter by root
	GSList* entry = supported_types_list;
	while (entry != NULL)
	{
		GSList* next = entry->next;

		if (g_slist_find_custom(mime_filter->allowed_roots, entry->data, g_compare_mime_root) == NULL)
		{
			// Free matching MIME type and remove the entry
			free(entry->data);
			supported_types_list = g_slist_delete_link(supported_types_list, entry);
		}
		entry = next;
	}
}

int connmgr_init(const char* mime_filter_string) {

	struct service *srv = upnp_connmgr_get_service();

	// Parse MIME filter into separate fields
	mime_type_filters_t mime_filter = connmgr_parse_mime_filter_string(mime_filter_string);

	// Filter MIME types by root
	connmgr_filter_mime_type_root(&mime_filter);

	// Manually add additional MIME types
	g_slist_foreach(mime_filter.added_types, g_add_mime_type, NULL);

	// Manually remove specific MIME types
	g_slist_foreach(mime_filter.removed_types, g_remove_mime_type, NULL);

	char *protoInfo = NULL;
	for (GSList* entry = supported_types_list; entry != NULL; entry = g_slist_next(entry))
	{
		Log_info("connmgr", "Registering support for '%s'", (const char*) entry->data);
		int rc = 0;
		if (protoInfo == NULL && entry->data != NULL)
			rc = asprintf(&protoInfo, "http-get:*:%s:*,", (const char*) entry->data);
		else if (entry->data != NULL)
		{
			char *tempo = protoInfo;
			rc = asprintf(&protoInfo, "%shttp-get:*:%s:*,", tempo, (const char*) entry->data);
			free(tempo);
		}
		if (rc == -1)
			break;
	}

	if (protoInfo != NULL) {
		// Truncate final comma
		size_t length = strlen(protoInfo);
		protoInfo[length] = '\0';
		VariableContainer_change(srv->variable_container,
					 CONNMGR_VAR_SINK_PROTO_INFO, protoInfo);
	}

	// Free string and its data
	free(protoInfo);

	// Free all lists that were generated
	g_slist_free_full(supported_types_list, free);
	g_slist_free_full(mime_filter.allowed_roots, free);
	g_slist_free_full(mime_filter.added_types, free);
	g_slist_free_full(mime_filter.removed_types, free);

	return 0;
}


static int get_protocol_info(struct action_event *event)
{
	upnp_append_variable(event, CONNMGR_VAR_SRC_PROTO_INFO, "Source");
	upnp_append_variable(event, CONNMGR_VAR_SINK_PROTO_INFO, "Sink");
	return event->status;
}

static int get_current_conn_ids(struct action_event *event)
{
	int rc = -1;
	upnp_add_response(event, "ConnectionIDs", "0");
	///rc = upnp_append_variable(event, CONNMGR_VAR_CUR_CONN_IDS,
	//			    "ConnectionIDs");
	return rc;
}

static int prepare_for_connection(struct action_event *event) {
	upnp_append_variable(event, CONNMGR_VAR_CUR_CONN_IDS, "ConnectionID");
	upnp_append_variable(event, CONNMGR_VAR_AAT_AVT_ID,  "AVTransportID");
	upnp_append_variable(event, CONNMGR_VAR_AAT_RCS_ID, "RcsID");
	return 0;
}

static int get_current_conn_info(struct action_event *event)
{
	const char *value = upnp_get_string(event, "ConnectionID");
	if (value == NULL) {
		return -1;
	}
	Log_info("connmgr", "Query ConnectionID='%s'", value);

	upnp_append_variable(event, CONNMGR_VAR_AAT_RCS_ID, "RcsID");
	upnp_append_variable(event, CONNMGR_VAR_AAT_AVT_ID, "AVTransportID");
	upnp_append_variable(event, CONNMGR_VAR_AAT_PROTO_INFO,
			     "ProtocolInfo");
	upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_MGR,
			     "PeerConnectionManager");
	upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_ID, "PeerConnectionID");
	upnp_append_variable(event, CONNMGR_VAR_AAT_DIR, "Direction");
	upnp_append_variable(event, CONNMGR_VAR_AAT_CONN_STATUS, "Status");
	return 0;
}

static struct action connmgr_actions[] = {
	[CONNMGR_CMD_GETCURRENTCONNECTIONIDS] =	{"GetCurrentConnectionIDs", get_current_conn_ids},
	[CONNMGR_CMD_SETCURRENTCONNECTIONINFO] ={"GetCurrentConnectionInfo", get_current_conn_info},
	[CONNMGR_CMD_GETPROTOCOLINFO] =		{"GetProtocolInfo", get_protocol_info},
	[CONNMGR_CMD_PREPAREFORCONNECTION] =	{"PrepareForConnection", prepare_for_connection}, /* optional */
	//[CONNMGR_CMD_CONNECTIONCOMPLETE] =	{"ConnectionComplete", NULL},	/* optional */
	[CONNMGR_CMD_COUNT] =			{NULL, NULL}
};

struct service *upnp_connmgr_get_service(void) {
	static struct service connmgr_service_ = {
		.service_mutex =        &connmgr_mutex,
		.service_id =		CONNMGR_SERVICE_ID,
		.service_type =		CONNMGR_TYPE,
		.scpd_url =		CONNMGR_SCPD_URL,
		.control_url =		CONNMGR_CONTROL_URL,
		.event_url =		CONNMGR_EVENT_URL,
		.event_xml_ns =         NULL,  // we never send change events.
		.actions =		connmgr_actions,
		.action_arguments =     argument_list,
		.variable_container =   NULL, // initialized below
		.last_change =          NULL,
		.command_count =        CONNMGR_CMD_COUNT,
	};

	static struct var_meta connmgr_var_meta[] = {
		{ CONNMGR_VAR_SRC_PROTO_INFO, "SourceProtocolInfo", "",
		  EV_YES, DATATYPE_STRING, NULL, NULL },
		{ CONNMGR_VAR_SINK_PROTO_INFO, "SinkProtocolInfo", "http-get:*:audio/mpeg:*",
		  EV_YES, DATATYPE_STRING, NULL, NULL },
		{ CONNMGR_VAR_CUR_CONN_IDS, "CurrentConnectionIDs", "0",
		  EV_YES, DATATYPE_STRING, NULL, NULL },

		{ CONNMGR_VAR_AAT_CONN_STATUS,"A_ARG_TYPE_ConnectionStatus", "Unknown",
		  EV_NO, DATATYPE_STRING, connstatus_values, NULL },
		{ CONNMGR_VAR_AAT_CONN_MGR, "A_ARG_TYPE_ConnectionManager", "/",
		  EV_NO, DATATYPE_STRING, NULL, NULL },
		{ CONNMGR_VAR_AAT_DIR, "A_ARG_TYPE_Direction", "Input",
		  EV_NO, DATATYPE_STRING, direction_values, NULL },
		{ CONNMGR_VAR_AAT_PROTO_INFO, "A_ARG_TYPE_ProtocolInfo", ":::",
		  EV_NO, DATATYPE_STRING, NULL, NULL },
		{ CONNMGR_VAR_AAT_CONN_ID, "A_ARG_TYPE_ConnectionID", "-1",
		  EV_NO, DATATYPE_I4, NULL, NULL },
		{ CONNMGR_VAR_AAT_AVT_ID, "A_ARG_TYPE_AVTransportID", "0",
		  EV_NO, DATATYPE_I4, NULL, NULL },
		{ CONNMGR_VAR_AAT_RCS_ID, "A_ARG_TYPE_RcsID", "0",
		  EV_NO, DATATYPE_I4, NULL, NULL },

		{ CONNMGR_VAR_COUNT, NULL, NULL, EV_NO, DATATYPE_UNKNOWN, NULL, NULL }
	};

	if (connmgr_service_.variable_container == NULL) {
		connmgr_service_.variable_container
			= VariableContainer_new(CONNMGR_VAR_COUNT,
						connmgr_var_meta);
		// no changes expected; no collector.
	}
	return &connmgr_service_;
}
