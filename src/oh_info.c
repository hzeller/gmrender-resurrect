/* oh_info.c - OpenHome Info service routines.
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

#include "oh_info.h"

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

#include "upnp.h"
#include "upnp_device.h"
#include "output.h"
#include "oh_info.h"

#define INFO_TYPE "urn:av-openhome-org:service:Info:1"
#define INFO_SERVICE_ID "urn:av-openhome:serviceId:Info"

#define INFO_SCPD_URL "/upnp/openhomeinfoSCPD.xml"
#define INFO_CONTROL_URL "/upnp/control/openhomeinfo1"
#define INFO_EVENT_URL "/upnp/event/openhomeinfo1"

static const gint64 one_sec_unit = 1000000000LL;

typedef enum {
	INFO_VAR_TRACK_COUNT,
	INFO_VAR_DETAILS_COUNT,
	INFO_VAR_METATEXT_COUNT,
	INFO_VAR_URI,
	INFO_VAR_METADATA,
	INFO_VAR_DURATION,
	INFO_VAR_BIT_RATE,
	INFO_VAR_BIT_DEPTH,
	INFO_VAR_SAMPLE_RATE,
	INFO_VAR_LOSSLESS,
	INFO_VAR_CODEC_NAME,
	INFO_VAR_METATEXT,
	
	INFO_VAR_UNKNOWN,
	INFO_VAR_COUNT
} info_variable_t;

enum {
	INFO_CMD_COUNTERS,
	INFO_CMD_TRACK,
	INFO_CMD_DETAILS,
	INFO_CMD_METATEXT,

	INFO_CMD_UNKNOWN,
	INFO_CMD_COUNT
};

static const char *info_variable_names[] = {
	[INFO_VAR_TRACK_COUNT] = "TrackCount",
	[INFO_VAR_DETAILS_COUNT] = "DetailsCount",
	[INFO_VAR_METATEXT_COUNT] = "MetatextCount",
	[INFO_VAR_URI] = "Uri",
	[INFO_VAR_METADATA] = "Metadata",
	[INFO_VAR_DURATION] = "Duration",
	[INFO_VAR_BIT_RATE] = "BitRate",
	[INFO_VAR_BIT_DEPTH] = "BitDepth",
	[INFO_VAR_SAMPLE_RATE] = "SampleRate",
	[INFO_VAR_LOSSLESS] = "Lossless",
	[INFO_VAR_CODEC_NAME] = "CodecName",
	[INFO_VAR_METATEXT] = "Metatext",
	[INFO_VAR_UNKNOWN] = NULL,
};

static const char *info_default_values[] = {
	[INFO_VAR_TRACK_COUNT] = "0",
	[INFO_VAR_DETAILS_COUNT] = "0",
	[INFO_VAR_METATEXT_COUNT] = "0",
	[INFO_VAR_URI] = "",
	[INFO_VAR_METADATA] = "",
	[INFO_VAR_DURATION] = "0",
	[INFO_VAR_BIT_RATE] = "0",
	[INFO_VAR_BIT_DEPTH] = "0",
	[INFO_VAR_SAMPLE_RATE] = "0",
	[INFO_VAR_LOSSLESS] = "0",
	[INFO_VAR_CODEC_NAME] = "",
	[INFO_VAR_METATEXT] = "",
	[INFO_VAR_UNKNOWN] = NULL,
};


static struct var_meta info_var_meta[] = {
	[INFO_VAR_TRACK_COUNT] =		{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_DETAILS_COUNT] =		{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_METATEXT_COUNT] =		{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_URI] =				{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[INFO_VAR_METADATA] =			{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[INFO_VAR_DURATION] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_BIT_RATE] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_BIT_DEPTH] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_SAMPLE_RATE] =		{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[INFO_VAR_LOSSLESS] =			{ SENDEVENT_YES, DATATYPE_BOOLEAN, NULL, NULL },
	[INFO_VAR_CODEC_NAME] =			{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[INFO_VAR_METATEXT] =			{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[INFO_VAR_UNKNOWN] =			{ SENDEVENT_YES, DATATYPE_UNKNOWN, NULL, NULL },
};

static struct argument *arguments_counters[] = {
        & (struct argument) { "TrackCount", PARAM_DIR_OUT, INFO_VAR_TRACK_COUNT },
        & (struct argument) { "DetailsCount", PARAM_DIR_OUT, INFO_VAR_DETAILS_COUNT },
        & (struct argument) { "MetatextCount", PARAM_DIR_OUT, INFO_VAR_METATEXT_COUNT },
        NULL
};

static struct argument *arguments_track[] = {
        & (struct argument) { "Uri", PARAM_DIR_OUT, INFO_VAR_URI },
        & (struct argument) { "Metadata", PARAM_DIR_OUT, INFO_VAR_METADATA },
        NULL
};

static struct argument *arguments_details[] = {
        & (struct argument) { "Duration", PARAM_DIR_OUT, INFO_VAR_DURATION },
        & (struct argument) { "BitRate", PARAM_DIR_OUT, INFO_VAR_BIT_RATE },
        & (struct argument) { "BitDepth", PARAM_DIR_OUT, INFO_VAR_BIT_DEPTH },
        & (struct argument) { "SampleRate", PARAM_DIR_OUT, INFO_VAR_SAMPLE_RATE },
        & (struct argument) { "Lossless", PARAM_DIR_OUT, INFO_VAR_LOSSLESS },
        & (struct argument) { "CodecName", PARAM_DIR_OUT, INFO_VAR_CODEC_NAME },
        NULL
};

static struct argument *arguments_metatext[] = {
        & (struct argument) { "Metatext", PARAM_DIR_OUT, INFO_VAR_METATEXT },
        NULL
};

static struct argument **argument_list[] = {
	[INFO_CMD_COUNTERS] = arguments_counters,
	[INFO_CMD_TRACK] = arguments_track,
	[INFO_CMD_DETAILS] = arguments_details,
	[INFO_CMD_METATEXT] = arguments_metatext,
	
	[INFO_CMD_UNKNOWN] = NULL
};


extern struct service info_service_;
static variable_container_t *state_variables_ = NULL;

static uint32_t track_count = 0;
static uint32_t details_count = 0;
static uint32_t metatext_count = 0;

static ithread_mutex_t info_mutex;

static void service_lock(void)
{
	ithread_mutex_lock(&info_mutex);
	if (info_service_.var_change_collector) {
		UPnPVarChangeCollector_start(info_service_.var_change_collector);
	}
}

static void service_unlock(void)
{
	if (info_service_.var_change_collector) {
		UPnPVarChangeCollector_finish(info_service_.var_change_collector);
	}
	ithread_mutex_unlock(&info_mutex);
}


static int replace_var(info_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

static int replace_var_int(info_variable_t varnum, int new_value)
{
	char buf[32];
	sprintf(buf, "%d", new_value);
	return replace_var(varnum, buf);
}

static int replace_var_uint(info_variable_t varnum, unsigned int new_value)
{
	char buf[32];
	sprintf(buf, "%u", new_value);
	return replace_var(varnum, buf);
}

static int get_counters(struct action_event *event)
{
	upnp_append_variable(event, INFO_VAR_TRACK_COUNT, "TrackCount");
	upnp_append_variable(event, INFO_VAR_DETAILS_COUNT, "DetailsCount");
	upnp_append_variable(event, INFO_VAR_METATEXT_COUNT, "MetatextCount");
	return 0;
}

static int get_track(struct action_event *event)
{
	upnp_append_variable(event, INFO_VAR_URI, "Uri");
	upnp_append_variable(event, INFO_VAR_METADATA, "Metadata");
	return 0;
}

static int get_details(struct action_event *event)
{
	upnp_append_variable(event, INFO_VAR_DURATION, "Duration");
	upnp_append_variable(event, INFO_VAR_BIT_RATE, "BitRate");
	upnp_append_variable(event, INFO_VAR_BIT_DEPTH, "BitDepth");
	upnp_append_variable(event, INFO_VAR_SAMPLE_RATE, "SampleRate");
	upnp_append_variable(event, INFO_VAR_LOSSLESS, "Lossless");
	upnp_append_variable(event, INFO_VAR_CODEC_NAME, "CodecName");
	return 0;
}

static int get_metatext(struct action_event *event)
{
	upnp_append_variable(event, INFO_VAR_METATEXT, "Metatext");
	return 0;
}

static struct action info_actions[] = {
	[INFO_CMD_COUNTERS] = { "Counters", get_counters },
	[INFO_CMD_TRACK] = { "Track", get_track },
	[INFO_CMD_DETAILS] = { "Details", get_details },
	[INFO_CMD_METATEXT] = { "Metatext", get_metatext },
	[INFO_CMD_UNKNOWN] = {NULL, NULL}
};

static void update_counter_vars(void)
{
	replace_var_uint(INFO_VAR_TRACK_COUNT, track_count);
	replace_var_uint(INFO_VAR_DETAILS_COUNT, details_count);
	replace_var_uint(INFO_VAR_METATEXT_COUNT, metatext_count);
}

static void shared_meta_time_change(uint32_t total, uint32_t current)
{
	service_lock();
	int changed = 0;
	if (replace_var_uint(INFO_VAR_DURATION, total))
		changed = 1;
	if (changed) {
		details_count++;
		update_counter_vars();
	}
	service_unlock();
}

static void shared_meta_song_change(char *uri, char *meta)
{
	service_lock();
	if (replace_var(INFO_VAR_URI, uri)) {
		track_count++;
		metatext_count = 0;
		details_count = 0;
		update_counter_vars();
	}
	service_unlock();
}

static void shared_meta_meta_change(char *meta)
{
	service_lock();
	if (replace_var(INFO_VAR_METADATA, meta)) {
		track_count++;
		update_counter_vars();
	}
	service_unlock();
}

static void shared_meta_details_change(int channels, int bits, int rate)
{
	int changed;
	service_lock();
	changed = replace_var_int(INFO_VAR_BIT_DEPTH, bits);
	changed |= replace_var_int(INFO_VAR_SAMPLE_RATE, rate);
	changed |= replace_var_uint(INFO_VAR_DETAILS_COUNT, details_count);
	if (changed) {
		details_count++;
		update_counter_vars();
	}
	service_unlock();
}

struct service *oh_info_get_service(void) {
	if (info_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(INFO_VAR_COUNT,
					      &info_service_,
					      info_default_values);
		info_service_.variable_container = state_variables_;
	}
	return &info_service_;
}

void oh_info_init(struct upnp_device *device) {
	assert(info_service_.var_change_collector == NULL);
	info_service_.var_change_collector =
		UPnPVarChangeCollector_new(state_variables_, device,
					    INFO_SERVICE_ID);
	struct shared_metadata *sm = output_shared_metadata();
	if (sm != NULL) {
		shared_meta_details_add_listener(sm, shared_meta_details_change);
		shared_meta_meta_add_listener(sm, shared_meta_meta_change);
		shared_meta_song_add_listener(sm, shared_meta_song_change);
		shared_meta_time_add_listener(sm, shared_meta_time_change);
	}
}

struct service info_service_ = {
	.service_id =           INFO_SERVICE_ID,
	.service_type =         INFO_TYPE,
	.scpd_url =				INFO_SCPD_URL,
	.control_url =			INFO_CONTROL_URL,
	.event_url =			INFO_EVENT_URL,
	.actions =              info_actions,
	.action_arguments =     argument_list,
	.variable_names =       info_variable_names,
	.variable_container =   NULL,
	.var_change_collector =          NULL,
	.variable_meta =        info_var_meta,
	.variable_count =       INFO_VAR_UNKNOWN,
	.command_count =        INFO_CMD_UNKNOWN,
	.service_mutex =        &info_mutex
};

