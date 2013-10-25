/* oh_playlist.c - OpenHome Playlist routines.
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 * Copyright (C) 2013 Andrey Demenev
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

#include "oh_playlist.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <glib.h>

#include <upnp/upnp.h>
#include <upnp/ithread.h>

#include "output.h"
#include "upnp.h"
#include "upnp_device.h"
#include "variable-container.h"
#include "xmlescape.h"
#include "xmldoc.h"
#include "oh_playlist.h"
#include "playlist.h"
#include "mime_types.h"

#define PLAYLIST_TYPE "urn:av-openhome-org:service:Playlist:1"
#define PLAYLIST_SERVICE_ID "urn:av-openhome:serviceId:Playlist"

#define PLAYLIST_SCPD_URL "/upnp/openhomeplaylistSCPD.xml"
#define PLAYLIST_CONTROL_URL "/upnp/control/openhomeplaylist1"
#define PLAYLIST_EVENT_URL "/upnp/event/openhomeplaylist1"

#define TRACKS_MAX 1000
#define XSTR(s) STR(s)
#define STR(s) #s
#define LIST_SIZE_INCREMENT 50

static void update_playlist(void);
static const gint64 one_sec_unit = 1000000000LL;

typedef enum {
	PLAYLIST_VAR_TRANSPORT_STATE,
	PLAYLIST_VAR_REPEAT,
	PLAYLIST_VAR_SHUFFLE,
	PLAYLIST_VAR_ID_ARRRAY,
	PLAYLIST_VAR_ID,
	PLAYLIST_VAR_TRACKS_MAX,
	PLAYLIST_VAR_PROTOCOL_INFO,
	PLAYLIST_VAR_INDEX,
	PLAYLIST_VAR_RELATIVE,
	PLAYLIST_VAR_ABSOLUTE,
	PLAYLIST_VAR_ID_LIST,
	PLAYLIST_VAR_TRACK_LIST,
	PLAYLIST_VAR_URI,
	PLAYLIST_VAR_METADATA,
	PLAYLIST_VAR_ID_ARRAY_TOKEN,
	PLAYLIST_VAR_ID_ARRAY_CHANGED,
	
	PLAYLIST_VAR_UNKNOWN,
	PLAYLIST_VAR_COUNT
} playlist_variable_t;

enum {
	PLAYLIST_CMD_PLAY,
	PLAYLIST_CMD_PAUSE,
	PLAYLIST_CMD_STOP,
	PLAYLIST_CMD_NEXT,
	PLAYLIST_CMD_PREVIOUS,
	PLAYLIST_CMD_SET_REPEAT,
	PLAYLIST_CMD_REPEAT,
	PLAYLIST_CMD_SET_SHUFFLE,
	PLAYLIST_CMD_SHUFFLE,
	PLAYLIST_CMD_SEEK_SECOND_ABSOLUTE,
	PLAYLIST_CMD_SEEK_SECOND_RELATIVE,
	PLAYLIST_CMD_SEEK_ID,
	PLAYLIST_CMD_SEEK_INDEX,
	PLAYLIST_CMD_TRANSPORT_STATE,
	PLAYLIST_CMD_ID,
	PLAYLIST_CMD_READ,
	PLAYLIST_CMD_READ_LIST,
	PLAYLIST_CMD_INSERT,
	PLAYLIST_CMD_DELETE_ID,
	PLAYLIST_CMD_DELETE_ALL,
	PLAYLIST_CMD_TRACKS_MAX,
	PLAYLIST_CMD_ID_ARRAY,
	PLAYLIST_CMD_ID_ARRAY_CHANGED,
	PLAYLIST_CMD_PROTOCOL_INFO,

	PLAYLIST_CMD_UNKNOWN,
	PLAYLIST_CMD_COUNT
};

static const char *playlist_variable_names[] = {
	[PLAYLIST_VAR_TRANSPORT_STATE] = "TransportState",
	[PLAYLIST_VAR_REPEAT] = "Repeat",
	[PLAYLIST_VAR_SHUFFLE] = "Shuffle",
	[PLAYLIST_VAR_ID_ARRRAY] = "IdArray",
	[PLAYLIST_VAR_ID] = "Id",
	[PLAYLIST_VAR_TRACKS_MAX] = "TracksMax",
	[PLAYLIST_VAR_PROTOCOL_INFO] = "ProtocolInfo",
	[PLAYLIST_VAR_INDEX] = "Index",
	[PLAYLIST_VAR_RELATIVE] = "Relative",
	[PLAYLIST_VAR_ABSOLUTE] = "Absolute",
	[PLAYLIST_VAR_ID_LIST] = "IdList",
	[PLAYLIST_VAR_TRACK_LIST] = "TrackList",
	[PLAYLIST_VAR_URI] = "Uri",
	[PLAYLIST_VAR_METADATA] = "Metadata",
	[PLAYLIST_VAR_ID_ARRAY_TOKEN] = "IdArrayToken",
	[PLAYLIST_VAR_ID_ARRAY_CHANGED] = "IdArrayChanged",
	[PLAYLIST_VAR_UNKNOWN] = NULL,
};

static const char kZeroTime[] = "0:00:00";

static const char *playlist_default_values[] = {
	[PLAYLIST_VAR_TRANSPORT_STATE] = "Stopped",
	[PLAYLIST_VAR_REPEAT] = "0",
	[PLAYLIST_VAR_SHUFFLE] = "0",
	[PLAYLIST_VAR_ID_ARRRAY] = "",
	[PLAYLIST_VAR_ID] = "0",
	[PLAYLIST_VAR_TRACKS_MAX] = XSTR(TRACKS_MAX),
	[PLAYLIST_VAR_PROTOCOL_INFO] = "",
	[PLAYLIST_VAR_INDEX] = "",
	[PLAYLIST_VAR_RELATIVE] = "",
	[PLAYLIST_VAR_ABSOLUTE] = "",
	[PLAYLIST_VAR_ID_LIST] = "",
	[PLAYLIST_VAR_TRACK_LIST] = "",
	[PLAYLIST_VAR_URI] = "",
	[PLAYLIST_VAR_METADATA] = "",
	[PLAYLIST_VAR_ID_ARRAY_TOKEN] = "",
	[PLAYLIST_VAR_ID_ARRAY_CHANGED] = "",
	[PLAYLIST_VAR_UNKNOWN] = NULL,
};

enum playlist_state {
	PLAYLIST_STOPPED,
	PLAYLIST_PLAYING,
	PLAYLIST_PAUSED,
};

static const char *playlist_states[] = {
	"Stopped",
	"Playing",
	"Paused",
	"Buffering",
	NULL
};

static struct param_range id_range = {
	0,
	4294967295LL,
	1
};


static struct var_meta player_var_meta[] = {
	[PLAYLIST_VAR_TRANSPORT_STATE] =    { SENDEVENT_YES, DATATYPE_STRING, playlist_states, NULL },
	[PLAYLIST_VAR_REPEAT] =				{ SENDEVENT_YES, DATATYPE_BOOLEAN, NULL, NULL },
	[PLAYLIST_VAR_SHUFFLE] =			{ SENDEVENT_YES, DATATYPE_BOOLEAN, NULL, NULL },
	[PLAYLIST_VAR_ID_ARRRAY] =			{ SENDEVENT_YES, DATATYPE_BASE64, NULL, NULL },
	[PLAYLIST_VAR_ID] =					{ SENDEVENT_YES, DATATYPE_UI4, NULL, &id_range },
	[PLAYLIST_VAR_TRACKS_MAX] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[PLAYLIST_VAR_PROTOCOL_INFO] =		{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[PLAYLIST_VAR_INDEX] =				{ SENDEVENT_NO,  DATATYPE_UI4, NULL, NULL },
	[PLAYLIST_VAR_RELATIVE] =			{ SENDEVENT_NO,  DATATYPE_I4, NULL, NULL },
	[PLAYLIST_VAR_ABSOLUTE] =			{ SENDEVENT_NO,  DATATYPE_UI4, NULL, NULL },
	[PLAYLIST_VAR_ID_LIST] =			{ SENDEVENT_NO,  DATATYPE_STRING, NULL, NULL },
	[PLAYLIST_VAR_TRACK_LIST] =			{ SENDEVENT_NO,  DATATYPE_STRING, NULL, NULL },
	[PLAYLIST_VAR_URI] =				{ SENDEVENT_NO,  DATATYPE_STRING, NULL, NULL },
	[PLAYLIST_VAR_METADATA] =			{ SENDEVENT_NO,  DATATYPE_STRING, NULL, NULL },
	[PLAYLIST_VAR_ID_ARRAY_TOKEN] =		{ SENDEVENT_NO,  DATATYPE_UI4, NULL, NULL },
	[PLAYLIST_VAR_ID_ARRAY_CHANGED] =	{ SENDEVENT_NO,  DATATYPE_BOOLEAN, NULL, NULL },
	
	[PLAYLIST_VAR_UNKNOWN] =			{ SENDEVENT_NO, DATATYPE_UNKNOWN, NULL, NULL }
};

static struct argument *arguments_setrepeat[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_REPEAT },
        NULL
};

static struct argument *arguments_repeat[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_REPEAT },
        NULL
};

static struct argument *arguments_setshuffle[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_SHUFFLE },
        NULL
};


static struct argument *arguments_shuffle[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_SHUFFLE },
        NULL
};

static struct argument *arguments_seekabsolute[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_ABSOLUTE },
        NULL
};

static struct argument *arguments_seekrelative[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_RELATIVE },
        NULL
};

static struct argument *arguments_seekid[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_ID },
        NULL
};

static struct argument *arguments_seekindex[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_INDEX },
        NULL
};

static struct argument *arguments_transportstate[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_TRANSPORT_STATE },
        NULL
};

static struct argument *arguments_id[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_ID },
        NULL
};

static struct argument *arguments_read[] = {
        & (struct argument) { "Id", PARAM_DIR_IN, PLAYLIST_VAR_ID },
        & (struct argument) { "Uri", PARAM_DIR_OUT, PLAYLIST_VAR_URI },
        & (struct argument) { "Metadata", PARAM_DIR_OUT, PLAYLIST_VAR_METADATA },
        NULL
};


static struct argument *arguments_readlist[] = {
        & (struct argument) { "IdList", PARAM_DIR_IN, PLAYLIST_VAR_ID_LIST },
        & (struct argument) { "TrackList", PARAM_DIR_OUT, PLAYLIST_VAR_TRACK_LIST },
        NULL
};

static struct argument *arguments_insert[] = {
        & (struct argument) { "AfterId", PARAM_DIR_IN, PLAYLIST_VAR_ID },
        & (struct argument) { "Uri", PARAM_DIR_IN, PLAYLIST_VAR_URI },
        & (struct argument) { "Metadata", PARAM_DIR_IN, PLAYLIST_VAR_METADATA },
        & (struct argument) { "NewId", PARAM_DIR_OUT, PLAYLIST_VAR_ID },
        NULL
};

static struct argument *arguments_deleteid[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PLAYLIST_VAR_ID },
        NULL
};

static struct argument *arguments_tracksmax[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_TRACKS_MAX },
        NULL
};

static struct argument *arguments_idarray[] = {
        & (struct argument) { "Token", PARAM_DIR_OUT, PLAYLIST_VAR_ID_ARRAY_TOKEN },
        & (struct argument) { "Array", PARAM_DIR_OUT, PLAYLIST_VAR_ID_ARRRAY },
        NULL
};

static struct argument *arguments_idarraychanged[] = {
        & (struct argument) { "Token", PARAM_DIR_IN, PLAYLIST_VAR_ID_ARRAY_TOKEN },
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_ID_ARRAY_CHANGED },
        NULL
};

static struct argument *arguments_protocolinfo[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PLAYLIST_VAR_PROTOCOL_INFO },
        NULL
};



static struct argument **argument_list[] = {
	[PLAYLIST_CMD_PLAY] = NULL,
	[PLAYLIST_CMD_PAUSE] = NULL,
	[PLAYLIST_CMD_STOP] = NULL,
	[PLAYLIST_CMD_NEXT] = NULL,
	[PLAYLIST_CMD_PREVIOUS] = NULL,
	[PLAYLIST_CMD_SET_REPEAT] = arguments_setrepeat,
	[PLAYLIST_CMD_REPEAT] = arguments_repeat,
	[PLAYLIST_CMD_SET_SHUFFLE] = arguments_setshuffle,
	[PLAYLIST_CMD_SHUFFLE] = arguments_shuffle,
	[PLAYLIST_CMD_SEEK_SECOND_ABSOLUTE] = arguments_seekabsolute,
	[PLAYLIST_CMD_SEEK_SECOND_RELATIVE] = arguments_seekrelative,
	[PLAYLIST_CMD_SEEK_ID] = arguments_seekid,
	[PLAYLIST_CMD_SEEK_INDEX] = arguments_seekindex,
	[PLAYLIST_CMD_TRANSPORT_STATE] = arguments_transportstate,
	[PLAYLIST_CMD_ID] = arguments_id,
	[PLAYLIST_CMD_READ] = arguments_read,
	[PLAYLIST_CMD_READ_LIST] = arguments_readlist,
	[PLAYLIST_CMD_INSERT] = arguments_insert,
	[PLAYLIST_CMD_DELETE_ID] = arguments_deleteid,
	[PLAYLIST_CMD_DELETE_ALL] = NULL,
	[PLAYLIST_CMD_TRACKS_MAX] = arguments_tracksmax,
	[PLAYLIST_CMD_ID_ARRAY] = arguments_idarray,
	[PLAYLIST_CMD_ID_ARRAY_CHANGED] = arguments_idarraychanged,
	[PLAYLIST_CMD_PROTOCOL_INFO] = arguments_protocolinfo,
	
	[PLAYLIST_CMD_UNKNOWN] = NULL
};


// Our 'instance' variables.

static struct playlist *playlist;

static enum playlist_state playlist_state_ = PLAYLIST_STOPPED;
extern struct service playlist_service_;   // Defined below.
static variable_container_t *state_variables_ = NULL;

/* protects transport_values, and service-specific state */

static ithread_mutex_t playlist_mutex;

static void inform_play_transition_from_output(enum PlayFeedback fb);

static void service_lock(void)
{
	ithread_mutex_lock(&playlist_mutex);
	if (playlist_service_.var_change_collector) {
		UPnPVarChangeCollector_start(playlist_service_.var_change_collector);
	}
}

static void service_unlock(void)
{
	if (playlist_service_.var_change_collector) {
		UPnPVarChangeCollector_finish(playlist_service_.var_change_collector);
	}
	ithread_mutex_unlock(&playlist_mutex);
}



// Replace given variable without sending an state-change event.
static int replace_var(playlist_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

/*
static const char *get_var(playlist_variable_t varnum) {
	return VariableContainer_get(state_variables_, varnum, NULL);
}
*/

static void change_playlist_state(enum playlist_state new_state)
{
	assert(new_state >= PLAYLIST_STOPPED && new_state <= PLAYLIST_PAUSED);
	playlist_state_ = new_state;
	replace_var(PLAYLIST_VAR_TRANSPORT_STATE, playlist_states[new_state]);
}


static void playlist_list_change(struct playlist *list)
{
	update_playlist();
}

static void playlist_current_change(struct playlist *list, playlist_id_t id, int index, int automatic)
{
	char *uri;
	if (!playlist_get(playlist, id, &uri, NULL)) {
		output_set_uri(uri, NULL);
		if (!automatic && playlist_state_ != PLAYLIST_STOPPED) {
			if (output_play(&inform_play_transition_from_output))  {
				change_playlist_state(PLAYLIST_STOPPED);
			} else {
				change_playlist_state(PLAYLIST_PLAYING);
			}
		}
	} else {
		output_stop();
		change_playlist_state(PLAYLIST_STOPPED);
	}
	char buf[32];
	sprintf(buf, "%u", id);
	replace_var(PLAYLIST_VAR_ID, buf);
}

static void playlist_current_remove(struct playlist *list)
{
	output_stop();
	change_playlist_state(PLAYLIST_STOPPED);
}

static void playlist_next_change(struct playlist *list, playlist_id_t id, int index)
{
	char *uri;
	if (playlist_get(playlist, id, &uri, NULL)) {
		output_set_next_uri(NULL);
	} else {
		output_set_next_uri(uri);
	}
}

static void update_suffle_state(int state)
{
	service_lock();
	replace_var(PLAYLIST_VAR_SHUFFLE, state ? "1" : "0");
	service_unlock();
}

static void update_repeat_state(int state)
{
	service_lock();
	replace_var(PLAYLIST_VAR_REPEAT, state ? "1" : "0");
	service_unlock();
}


static void update_playlist(void)
{
	int size = playlist_get_size(playlist);
	if (size > 0) {
		// Andrey Demenev: I do not know if we can rely on system-supplied
		// endianness conversion
		playlist_id_t *orig_ids = playlist_get_ids(playlist);
		playlist_id_t *swapped_ids = NULL;
		int test = 1;
		if (*((char*)&test) == 1) { // little endian
			swapped_ids = malloc(sizeof(playlist_id_t) * size);
			for (int i = 0; i < size; i++) {
				playlist_id_t id = orig_ids[i];
				swapped_ids[i] = ((id>>24)&0xff) 
					|((id<<8)&0xff0000) 
					|((id>>8)&0xff00)
					|((id<<24)&0xff000000);
			}
		}
		const guchar *data = (const guchar*)(swapped_ids == NULL ? orig_ids : swapped_ids);
		gchar *encoded = g_base64_encode(data, sizeof(playlist_id_t) * size);
		replace_var(PLAYLIST_VAR_ID_ARRRAY, encoded);
		g_free(encoded);
		if (swapped_ids != NULL)
			free(swapped_ids);
	} else {
		replace_var(PLAYLIST_VAR_ID_ARRRAY, "");
	}
}

static int delete_all(struct action_event *event)
{
	service_lock();
	playlist_clear(playlist);
	output_stop();
	change_playlist_state(PLAYLIST_STOPPED);
	service_unlock();
	return 0;
}

static int id_array(struct action_event *event)
{
	service_lock();
	char buf[32];
	sprintf(buf, "%u", playlist_get_token(playlist));
	upnp_add_response(event, "Token", buf);
	service_unlock();
	upnp_append_variable(event, PLAYLIST_VAR_ID_ARRRAY, "Array");
	return 0;
}

static void generate_protocol_info(void)
{
	int bufsize = 0;
	char *buf, *p;
	buf = malloc(bufsize);
	p = buf;
	assert(buf != NULL);

	struct mime_type *entry;
	int offset;
	for (entry = get_supported_mime_types(); entry; entry = entry->next) {
		bufsize += strlen(entry->mime_type) + 1 + 8 + 3 + 2;
		offset = p - buf;
		buf = realloc(buf, bufsize);
		assert(buf != NULL);
		p = buf;
		p += offset;
		strncpy(p, "http-get:*:", 11);
		p += 11;
		strncpy(p, entry->mime_type, strlen(entry->mime_type));
		p += strlen(entry->mime_type);
		strncpy(p, ":*,", 3);
		p += 3;
	}
	if (p > buf) {
		p--;
		*p = '\0';
	}
	*p = '\0';

	replace_var(PLAYLIST_VAR_PROTOCOL_INFO, buf);
	free(buf);
}


static void inform_play_transition_from_output(enum PlayFeedback fb)
{
	service_lock();
	switch (fb) {
	case PLAY_STOPPED:
		change_playlist_state(PLAYLIST_STOPPED);
		break;

	case PLAY_STARTED_NEXT_STREAM:
		playlist_next(playlist, 1);
		break;
	}
	service_unlock();
}


static int pause_stream(struct action_event *event)
{
	int rc = 0;
	service_lock();
	if (playlist_state_ == PLAYLIST_PLAYING) {
		if (output_pause()) {
			upnp_set_error(event, 800, "Pause failed");
			rc = -1;
		} else {
			change_playlist_state(PLAYLIST_PAUSED);
		}
	}
	service_unlock();
	return rc;
}

static int play_next(struct action_event *event)
{
	service_lock();
	playlist_next(playlist, 0);
	service_unlock();
	return 0;
}

static int play_prev(struct action_event *event)
{
	service_lock();
	playlist_prev(playlist);
	service_unlock();
	return 0;
}

static int stop(struct action_event *event)
{
	service_lock();
	if (playlist_state_ != PLAYLIST_STOPPED) {
		output_stop();
		change_playlist_state(PLAYLIST_STOPPED);
	}
	service_unlock();
	return 0;
}

static int play(struct action_event *event)
{
	int rc = 0;
	service_lock();
	if (playlist_current_id(playlist) > 0) {
		if (output_play(&inform_play_transition_from_output)) {
			upnp_set_error(event, 800, "Playing failed");
			change_playlist_state(PLAYLIST_STOPPED);
			rc = -1;
		} else {
			change_playlist_state(PLAYLIST_PLAYING);
		}
	}
	service_unlock();
	return rc;
}

static int protocol_info(struct action_event *event)
{
	upnp_append_variable(event, PLAYLIST_VAR_PROTOCOL_INFO, "Value");
	return 0;
}

static int id_array_changed(struct action_event *event)
{
	char *token_str = upnp_get_string(event, "Token");
	if (token_str == NULL)
		return -1;

	int t = -1;
	sscanf(token_str, "%d", &t);
	free(token_str);
	service_lock();
	upnp_add_response(event, "Value", t == playlist_get_token(playlist) ? "0" : "1");
	service_unlock();
	return 0;
}

static int seek_absolute(struct action_event *event)
{
	char *offset_str = upnp_get_string(event, "Value");
	if (offset_str == NULL)
		return -1;

	uint32_t offset = 0;
	sscanf(offset_str, "%u", &offset);
	free(offset_str);
	service_lock();
	output_seek(one_sec_unit * offset);
	service_unlock();
	return 0;
}

static int seek_relative(struct action_event *event)
{
	char *offset_str = upnp_get_string(event, "Value");
	if (offset_str == NULL)
		return -1;
	int32_t offset = 0;
	sscanf(offset_str, "%d", &offset);
	free(offset_str);
	
	service_lock();
	gint64 duration, position;
	const int pos_result = output_get_position(&duration, &position);
	if (pos_result == 0) {
		output_seek(position + one_sec_unit * offset);

	}
	service_unlock();
	return 0;
}

static int set_shuffle(struct action_event *event)
{
	char *value = upnp_get_string(event, "Value");
	if (value == NULL)
		return -1;
	update_suffle_state( strcmp("True", value) ? 0 : 1);
	free(value);
	return 0;
}

static int shuffle(struct action_event *event)
{
	upnp_append_variable(event, PLAYLIST_VAR_SHUFFLE, "Value");
	return 0;
}

static int tracks_max(struct action_event *event)
{
	upnp_add_response(event, "Value", XSTR(TRACKS_MAX));
	return 0;
}

static int set_repeat(struct action_event *event)
{
	char *value = upnp_get_string(event, "Value");
	if (value == NULL)
		return -1;
	update_repeat_state( strcmp("True", value) ? 0 : 1);
	free(value);
	return 0;
}

static int repeat(struct action_event *event)
{
	upnp_append_variable(event, PLAYLIST_VAR_REPEAT, "Value");
	return 0;
}

static int read_entry_list(struct action_event *event)
{
	char *id_list = upnp_get_string(event, "IdList");
	if (id_list == NULL)
		return -1;

	char *start, *end;
	
	struct xmldoc *doc;
	doc = xmldoc_new();
	struct xmlelement *top;
	top = xmldoc_new_topelement(doc, "TrackList", NULL);

	service_lock();
	start = id_list;
	while (*start) {
		while (*start == ' ')
			start++;
		errno = 0;
		long int id = strtol(start, &end, 10);
		if ((errno == ERANGE && (id == LONG_MAX || id == LONG_MIN)) || (errno != 0 && id == 0))
			break;
		if (id < 1 || id > INT_MAX)
			break;
		char *uri, *meta;
		if (!playlist_get(playlist, id, &uri, &meta)) {
			struct xmlelement *parent = xmlelement_new(doc, "Entry");
			add_value_element_int(doc, parent, "Id", id);
			add_value_element(doc, parent, "Uri", uri);
			add_value_element(doc, parent, "Metadata", meta);
			xmlelement_add_element(doc, top, parent);
		}
		start = end;
	}
	service_unlock();
	free(id_list);
    char *result = xmldoc_tostring(doc);
	xmldoc_free(doc);
	upnp_add_response(event, "TrackList", result);
	free(result);
	return 0;
}

static int seek_id(struct action_event *event)
{
	int rc = 0;
	char *id_str = upnp_get_string(event, "Value");
	if (id_str == NULL) {
		return -1;
	}
	int id = -1;
	sscanf(id_str, "%d", &id);
	free(id_str);
	service_lock();
	output_stop();
	if (playlist_set_current_id(playlist, id, 0)) {
		rc = -1;
		upnp_set_error(event, 800, "Seek error");
	}
	if (output_play(&inform_play_transition_from_output)) {
		upnp_set_error(event, 800, "Playing failed");
		change_playlist_state(PLAYLIST_STOPPED);
		rc = -1;
	} else {
		change_playlist_state(PLAYLIST_PLAYING);
	}
	service_unlock();
	return rc;
}

static int seek_index(struct action_event *event)
{
	int rc = 0;
	char *idx_str = upnp_get_string(event, "Value");
	if (idx_str == NULL) {
		return -1;
	}
	int idx = -1;
	sscanf(idx_str, "%d", &idx);
	free(idx_str);
	if (playlist_set_current_index(playlist, idx, 0)) {
		rc = -1;
		upnp_set_error(event, 800, "Seek error");
	}
	/*
	if (output_play(&inform_play_transition_from_output)) {
		upnp_set_error(event, 800, "Playing failed");
		change_playlist_state(PLAYLIST_STOPPED);
		rc = -1;
	} else {
		change_playlist_state(PLAYLIST_PLAYING);
	}
	*/
	return rc;
}

static int transport_state(struct action_event *event)
{
	upnp_append_variable(event, PLAYLIST_VAR_TRANSPORT_STATE, "Value");
	return 0;
}

static int id_get(struct action_event *event)
{
	upnp_append_variable(event, PLAYLIST_VAR_ID, "Value");
	return 0;
}

static int read_entry(struct action_event *event)
{
	char *id_str = upnp_get_string(event, "Id");
	if (id_str == NULL) {
		return -1;
	}
	int id = -1;
	sscanf(id_str, "%d", &id);
	free(id_str);
	service_lock();
	char *uri, *meta;
	if (playlist_get(playlist, id, &uri, &meta)) {
		service_unlock();
		upnp_set_error(event, 800, "Invalid Id");
		return -1;
	}
	upnp_add_response(event, "Uri", uri);
	upnp_add_response(event, "Metadata", meta);
	service_unlock();
	return 0;
}


static int delete_id(struct action_event *event)
{
	char *id_str = upnp_get_string(event, "Value");
	if (id_str == NULL)
		return -1;

	int id = 0; 
	sscanf(id_str, "%d", &id);
	free(id_str);
	service_lock();
	playlist_remove(playlist, id);
	service_unlock();
	return 0;
}

static int insert(struct action_event *event)
{
	int rc = 0;
	char *after_id_str = upnp_get_string(event, "AfterId");
	if (after_id_str == NULL)
		return -1;

	int id = 0; 
	sscanf(after_id_str, "%d", &id);
	free(after_id_str);
		
	char *uri = upnp_get_string(event, "Uri");
	if (uri == NULL) {
		return -1;
	}
	char *meta = upnp_get_string(event, "Metadata");
	if (meta == NULL) {
		free(uri);
		return -1;
	}
	service_lock();
	playlist_id_t new_id = 1;
	if (playlist_add(playlist, id, uri, meta, &new_id)) {
		upnp_set_error(event, 800, "Invalid AfterId");
		rc = -1;
	}

	if (rc == 0) {
		char buf[32];
		sprintf(buf, "%u", new_id);
		upnp_add_response(event, "NewId", buf);
	}
	service_unlock();
	return rc;
}


static struct action playlist_actions[] = {
	[PLAYLIST_CMD_PLAY] = { "Play", play },
	[PLAYLIST_CMD_PAUSE] = { "Pause", pause_stream },
	[PLAYLIST_CMD_STOP] = { "Stop", stop },
	[PLAYLIST_CMD_NEXT] = { "Next", play_next },
	[PLAYLIST_CMD_PREVIOUS] = { "Previous", play_prev },
	[PLAYLIST_CMD_SET_REPEAT] = { "SetRepeat", set_repeat },
	[PLAYLIST_CMD_REPEAT] = { "Repeat", repeat },
	[PLAYLIST_CMD_SET_SHUFFLE] = { "SetShuffle", set_shuffle },
	[PLAYLIST_CMD_SHUFFLE] = { "Shuffle", shuffle },
	[PLAYLIST_CMD_SEEK_SECOND_ABSOLUTE] = { "SeekSecondAbsolute", seek_absolute },
	[PLAYLIST_CMD_SEEK_SECOND_RELATIVE] = { "SeekSecondRelative", seek_relative },
	[PLAYLIST_CMD_SEEK_ID] = { "SeekId", seek_id },
	[PLAYLIST_CMD_SEEK_INDEX] = { "SeekIndex", seek_index },
	[PLAYLIST_CMD_TRANSPORT_STATE] = { "TransportState", transport_state },
	[PLAYLIST_CMD_ID] = { "Id", id_get },
	[PLAYLIST_CMD_READ] = { "Read", read_entry },
	[PLAYLIST_CMD_READ_LIST] = { "ReadList", read_entry_list },
	[PLAYLIST_CMD_INSERT] = { "Insert", insert },
	[PLAYLIST_CMD_DELETE_ID] = { "DeleteId",  delete_id },
	[PLAYLIST_CMD_DELETE_ALL] = { "DeleteAll", delete_all },
	[PLAYLIST_CMD_TRACKS_MAX] = { "TracksMax", tracks_max },
	[PLAYLIST_CMD_ID_ARRAY] = { "IdArray", id_array },
	[PLAYLIST_CMD_ID_ARRAY_CHANGED] = { "IdArrayChanged", id_array_changed },
	[PLAYLIST_CMD_PROTOCOL_INFO] = { "ProtocolInfo", protocol_info },

	[PLAYLIST_CMD_UNKNOWN] =                  {NULL, NULL}
};

struct service *oh_playlist_get_service(void) {
	if (playlist_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(PLAYLIST_VAR_COUNT,
						  &playlist_service_,
					      playlist_default_values);
		playlist_service_.variable_container = state_variables_;
	}
	return &playlist_service_;
}

void oh_playlist_init(struct upnp_device *device) {
	assert(playlist_service_.var_change_collector == NULL);
	playlist_service_.var_change_collector =
		UPnPVarChangeCollector_new(state_variables_, device,
					    PLAYLIST_SERVICE_ID);

	playlist = playlist_create();
	playlist_set_list_change_listener(playlist, playlist_list_change);
	playlist_set_current_change_listener(playlist, playlist_current_change);
	playlist_set_current_remove_listener(playlist, playlist_current_remove);
	playlist_set_next_change_listener(playlist, playlist_next_change);
	generate_protocol_info();

	//pthread_t thread;
	//pthread_create(&thread, NULL, thread_update_track_time, NULL);
}

void oh_playlist_register_variable_listener(variable_change_listener_t cb,
					       void *userdata) {
	VariableContainer_register_callback(state_variables_, cb, userdata);
}




struct service playlist_service_ = {
	.service_id =           PLAYLIST_SERVICE_ID,
	.service_type =         PLAYLIST_TYPE,
	.scpd_url =		PLAYLIST_SCPD_URL,
	.control_url =		PLAYLIST_CONTROL_URL,
	.event_url =		PLAYLIST_EVENT_URL,
	.actions =              playlist_actions,
	.action_arguments =     argument_list,
	.variable_names =       playlist_variable_names,
	.variable_container =   NULL, // set later.
	.var_change_collector =          NULL,
	.variable_meta =        player_var_meta,
	.variable_count =       PLAYLIST_VAR_UNKNOWN,
	.command_count =        PLAYLIST_CMD_UNKNOWN,
	.service_mutex =        &playlist_mutex
};
