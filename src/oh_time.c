/* oh_time.c - OpenHome Info service routines.
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
#include "oh_time.h"

#define TIME_TYPE "urn:av-openhome-org:service:Time:1"
#define TIME_SERVICE_ID "urn:av-openhome:serviceId:Time"

#define TIME_SCPD_URL "/upnp/openhometimeSCPD.xml"
#define TIME_CONTROL_URL "/upnp/control/openhometime1"
#define TIME_EVENT_URL "/upnp/event/openhometime1"

static const gint64 one_sec_unit = 1000000000LL;

typedef enum {
	TIME_VAR_TRACK_COUNT,
	TIME_VAR_DURATION,
	TIME_VAR_SECONDS,
	
	TIME_VAR_LAST_CHANGE,
	TIME_VAR_UNKNOWN,
	TIME_VAR_COUNT
} time_variable_t;

enum {
	TIME_CMD_TIME,

	TIME_CMD_UNKNOWN,
	TIME_CMD_COUNT
};

static const char *time_variable_names[] = {
	[TIME_VAR_TRACK_COUNT] = "TrackCount",
	[TIME_VAR_DURATION] = "Duration",
	[TIME_VAR_SECONDS] = "Seconds",
	[TIME_VAR_LAST_CHANGE] = "LastChange",
	[TIME_VAR_UNKNOWN] = NULL,
};

static const char *time_default_values[] = {
	[TIME_VAR_TRACK_COUNT] = "0",
	[TIME_VAR_DURATION] = "0",
	[TIME_VAR_SECONDS] = "0",
	[TIME_VAR_LAST_CHANGE] = "",
	[TIME_VAR_UNKNOWN] = NULL,
};


static struct var_meta time_var_meta[] = {
	[TIME_VAR_TRACK_COUNT] =		{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[TIME_VAR_DURATION] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[TIME_VAR_SECONDS] =			{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[TIME_VAR_LAST_CHANGE] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TIME_VAR_UNKNOWN] =			{ SENDEVENT_NO, DATATYPE_UNKNOWN, NULL, NULL },
};

static struct argument *arguments_time[] = {
        & (struct argument) { "TrackCount", PARAM_DIR_OUT, TIME_VAR_TRACK_COUNT },
        & (struct argument) { "Duration", PARAM_DIR_OUT, TIME_VAR_DURATION },
        & (struct argument) { "Seconds", PARAM_DIR_OUT, TIME_VAR_SECONDS },
        NULL
};

static struct argument **argument_list[] = {
	[TIME_CMD_TIME] = arguments_time,
	
	[TIME_CMD_UNKNOWN] = NULL
};


extern struct service time_service_;
static variable_container_t *state_variables_ = NULL;

static uint32_t track_count = 0;
static char *last_uri;

static ithread_mutex_t time_mutex;

static void service_lock(void)
{
	ithread_mutex_lock(&time_mutex);
}

static void service_unlock(void)
{
	ithread_mutex_unlock(&time_mutex);
}

static int replace_var(time_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

static int replace_var_uint(time_variable_t varnum, unsigned int new_value)
{
	char buf[32];
	sprintf(buf, "%u", new_value);
	return replace_var(varnum, buf);
}

static int get_time(struct action_event *event)
{
	upnp_append_variable(event, TIME_VAR_TRACK_COUNT, "TrackCount");
	upnp_append_variable(event, TIME_VAR_DURATION, "Duration");
	upnp_append_variable(event, TIME_VAR_SECONDS, "Seconds");
	return 0;
}

static struct action time_actions[] = {
	[TIME_CMD_TIME] = { "Time", get_time },
	[TIME_CMD_UNKNOWN] = {NULL, NULL}
};

static void shared_meta_time_change(uint32_t total, uint32_t current)
{
	service_lock();
	replace_var_uint(TIME_VAR_DURATION, total);
	replace_var_uint(TIME_VAR_SECONDS, current);
	service_unlock();
}

static void shared_meta_song_change(char *uri, char *meta)
{
	service_lock();
	if (uri == NULL || strcmp(uri, last_uri)) {
		track_count++;
		replace_var_uint(TIME_VAR_TRACK_COUNT, track_count);
	}
	service_unlock();
}

struct service *oh_time_get_service(void) {
	if (time_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(TIME_VAR_COUNT,
					      time_variable_names,
					      time_default_values);
		time_service_.variable_container = state_variables_;
	}
	return &time_service_;
}

void oh_time_init(struct upnp_device *device) {
	assert(time_service_.last_change == NULL);
	time_service_.last_change =
		UPnPLastChangeCollector_new(state_variables_, device,
					    TIME_SERVICE_ID);
	struct shared_metadata *sm = output_shared_metadata();
	if (sm != NULL) {
		shared_meta_song_add_listener(sm, shared_meta_song_change);
		shared_meta_time_add_listener(sm, shared_meta_time_change);
	}
	last_uri = strdup("");
}

struct service time_service_ = {
	.service_id =           TIME_SERVICE_ID,
	.service_type =         TIME_TYPE,
	.scpd_url =				TIME_SCPD_URL,
	.control_url =			TIME_CONTROL_URL,
	.event_url =			TIME_EVENT_URL,
	.actions =              time_actions,
	.action_arguments =     argument_list,
	.variable_names =       time_variable_names,
	.variable_container =   NULL,
	.last_change =          NULL,
	.variable_meta =        time_var_meta,
	.variable_count =       TIME_VAR_UNKNOWN,
	.command_count =        TIME_CMD_UNKNOWN,
	.service_mutex =        &time_mutex
};

