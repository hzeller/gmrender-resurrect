/* oh_volume.c - OpenHome Volume service routines.
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

#include "oh_volume.h"

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

#define VOLUME_TYPE "urn:av-openhome-org:service:Volume:1"
#define VOLUME_SERVICE_ID "urn:av-openhome:serviceId:Volume"

#define VOLUME_SCPD_URL "/upnp/openhomevolumeSCPD.xml"
#define VOLUME_CONTROL_URL "/upnp/control/openhomevolume1"
#define VOLUME_EVENT_URL "/upnp/event/openhomevolume1"

typedef enum {
	VOLUME_VAR_VOLUME,
	VOLUME_VAR_BALANCE,
	VOLUME_VAR_FADE,
	VOLUME_VAR_MUTE,
	VOLUME_VAR_VOLUME_LIMIT,
	VOLUME_VAR_VOLUME_MAX,
	VOLUME_VAR_VOLUME_UNITY,
	VOLUME_VAR_VOLUME_STEPS,
	VOLUME_VAR_VOLUME_MILLI_DB_PER_STEP,
	VOLUME_VAR_BALANCE_MAX,
	VOLUME_VAR_FADE_MAX,
	
	VOLUME_VAR_UNKNOWN,
	VOLUME_VAR_COUNT
} volume_variable_t;

enum {
	VOLUME_CMD_VOLUME,
	VOLUME_CMD_SET_VOLUME,
	VOLUME_CMD_VOLUME_INC,
	VOLUME_CMD_VOLUME_DEC,
	VOLUME_CMD_VOLUME_LIMIT,
	VOLUME_CMD_BALANCE,
	VOLUME_CMD_SET_BALANCE,
	VOLUME_CMD_BALANCE_INC,
	VOLUME_CMD_BALANCE_DEC,
	VOLUME_CMD_FADE,
	VOLUME_CMD_SET_FADE,
	VOLUME_CMD_FADE_INC,
	VOLUME_CMD_FADE_DEC,
	VOLUME_CMD_MUTE,
	VOLUME_CMD_SET_MUTE,
	VOLUME_CMD_CHARACTERISTICS,

	VOLUME_CMD_UNKNOWN,
	VOLUME_CMD_COUNT
};

static const char *volume_variable_names[] = {
	[VOLUME_VAR_VOLUME] = "Volume",
	[VOLUME_VAR_BALANCE] = "Balance",
	[VOLUME_VAR_FADE] = "Fade",
	[VOLUME_VAR_MUTE] = "Mute",
	[VOLUME_VAR_VOLUME_LIMIT] = "VolumeLimit",
	[VOLUME_VAR_VOLUME_MAX] = "VolumeMax",
	[VOLUME_VAR_VOLUME_UNITY] = "VolumeUnity",
	[VOLUME_VAR_VOLUME_STEPS] = "VolumeSteps",
	[VOLUME_VAR_VOLUME_MILLI_DB_PER_STEP] = "VolumeMilliDbPerStep",
	[VOLUME_VAR_BALANCE_MAX] = "BalanceMax",
	[VOLUME_VAR_FADE_MAX] = "FadeMax",

	[VOLUME_VAR_UNKNOWN] = NULL,
};

static const char *volume_default_values[] = {
	[VOLUME_VAR_VOLUME] = "16",
	[VOLUME_VAR_BALANCE] = "0",
	[VOLUME_VAR_FADE] = "0",
	[VOLUME_VAR_MUTE] = "0",
	[VOLUME_VAR_VOLUME_LIMIT] = "16",
	[VOLUME_VAR_VOLUME_MAX] = "16",
	[VOLUME_VAR_VOLUME_UNITY] = "16",
	[VOLUME_VAR_VOLUME_STEPS] = "16",
	[VOLUME_VAR_VOLUME_MILLI_DB_PER_STEP] = "0",
	[VOLUME_VAR_BALANCE_MAX] = "0",
	[VOLUME_VAR_FADE_MAX] = "0",
	
	[VOLUME_VAR_UNKNOWN] = NULL,
};


static struct var_meta volume_var_meta[] = {
	[VOLUME_VAR_VOLUME] =					{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_BALANCE] =					{ SENDEVENT_YES, DATATYPE_I4, NULL, NULL },
	[VOLUME_VAR_FADE] =						{ SENDEVENT_YES, DATATYPE_I4, NULL, NULL },
	[VOLUME_VAR_MUTE] =						{ SENDEVENT_YES, DATATYPE_BOOLEAN, NULL, NULL },
	[VOLUME_VAR_VOLUME_LIMIT] =				{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_VOLUME_MAX] =				{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_VOLUME_UNITY] =				{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_VOLUME_STEPS] =				{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_VOLUME_MILLI_DB_PER_STEP] =	{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_BALANCE_MAX] =				{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },
	[VOLUME_VAR_FADE_MAX] =					{ SENDEVENT_YES, DATATYPE_UI4, NULL, NULL },

	[VOLUME_VAR_UNKNOWN] =					{ SENDEVENT_NO, DATATYPE_UNKNOWN, NULL, NULL },
};

static struct argument *arguments_char[] = {
        & (struct argument) { "VolumeMax", PARAM_DIR_OUT, VOLUME_VAR_VOLUME_MAX },
        & (struct argument) { "VolumeUnity", PARAM_DIR_OUT, VOLUME_VAR_VOLUME_UNITY },
        & (struct argument) { "VolumeSteps", PARAM_DIR_OUT, VOLUME_VAR_VOLUME_STEPS },
        & (struct argument) { "VolumeMilliDbPerStep", PARAM_DIR_OUT, VOLUME_VAR_VOLUME_MILLI_DB_PER_STEP },
        & (struct argument) { "BalanceMax", PARAM_DIR_OUT, VOLUME_VAR_BALANCE_MAX },
        & (struct argument) { "FadeMax", PARAM_DIR_OUT, VOLUME_VAR_FADE_MAX },
        NULL
};

static struct argument *arguments_volume[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, VOLUME_VAR_VOLUME },
        NULL
};

static struct argument *arguments_set_volume[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, VOLUME_VAR_VOLUME },
        NULL
};

static struct argument *arguments_balance[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, VOLUME_VAR_BALANCE },
        NULL
};

static struct argument *arguments_set_balance[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, VOLUME_VAR_BALANCE },
        NULL
};

static struct argument *arguments_fade[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, VOLUME_VAR_FADE },
        NULL
};

static struct argument *arguments_set_fade[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, VOLUME_VAR_FADE },
        NULL
};

static struct argument *arguments_mute[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, VOLUME_VAR_MUTE },
        NULL
};

static struct argument *arguments_set_mute[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, VOLUME_VAR_MUTE },
        NULL
};

static struct argument *arguments_volume_limit[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, VOLUME_VAR_VOLUME_LIMIT },
        NULL
};


static struct argument **argument_list[] = {
	[VOLUME_CMD_VOLUME] = arguments_volume,
	[VOLUME_CMD_SET_VOLUME] = arguments_set_volume,
	[VOLUME_CMD_VOLUME_INC] = NULL,
	[VOLUME_CMD_VOLUME_DEC] = NULL,
	[VOLUME_CMD_VOLUME_LIMIT] = arguments_volume_limit,
	[VOLUME_CMD_BALANCE] = arguments_balance,
	[VOLUME_CMD_SET_BALANCE] = arguments_set_balance,
	[VOLUME_CMD_BALANCE_INC] = NULL,
	[VOLUME_CMD_BALANCE_DEC] = NULL,
	[VOLUME_CMD_FADE] = arguments_fade,
	[VOLUME_CMD_SET_FADE] = arguments_set_fade,
	[VOLUME_CMD_FADE_INC] = NULL,
	[VOLUME_CMD_FADE_DEC] = NULL,
	[VOLUME_CMD_MUTE] = arguments_mute,
	[VOLUME_CMD_SET_MUTE] = arguments_set_mute,
	[VOLUME_CMD_CHARACTERISTICS] = arguments_char,
	
	[VOLUME_CMD_UNKNOWN] = NULL
};


extern struct service volume_service_;
static variable_container_t *state_variables_ = NULL;
static uint32_t current_volume = 16;

static ithread_mutex_t volume_mutex;

static void service_lock(void)
{
	ithread_mutex_lock(&volume_mutex);
	if (volume_service_.var_change_collector) {
		UPnPVarChangeCollector_start(volume_service_.var_change_collector);
	}
}

static void service_unlock(void)
{
	if (volume_service_.var_change_collector) {
		UPnPVarChangeCollector_finish(volume_service_.var_change_collector);
	}
	ithread_mutex_unlock(&volume_mutex);
}

static int replace_var(volume_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

static int replace_var_uint(volume_variable_t varnum, unsigned int new_value)
{
	char buf[32];
	sprintf(buf, "%u", new_value);
	return replace_var(varnum, buf);
}

static int no_action(struct action_event *event)
{
	return 0;
}

static int volume(struct action_event *event)
{
	upnp_append_variable(event, VOLUME_VAR_VOLUME, "Value");
	return 0;
}

static int volume_limit(struct action_event *event)
{
	upnp_append_variable(event, VOLUME_VAR_VOLUME_LIMIT, "Value");
	return 0;
}

static int mute(struct action_event *event)
{
	upnp_append_variable(event, VOLUME_VAR_MUTE, "Value");
	return 0;
}

static int zero_value(struct action_event *event)
{
	upnp_add_response(event, "Value", "0");
	return 0;
}

static int set_mute(struct action_event *event)
{
	char *value = upnp_get_string(event, "Value");
	if (value == NULL)
		return -1;
	service_lock();
	replace_var(VOLUME_VAR_MUTE, strcmp("True", value) ? "0" : "1");
	service_unlock();
	return 0;
}

static int characteristics(struct action_event *event)
{
	upnp_add_response(event, "VolumeMax", "16");
	upnp_add_response(event, "VolumeUnity", "16");
	upnp_add_response(event, "VolumeSteps", "16");
	upnp_add_response(event, "VolumeMilliDbPerStep", "0");
	upnp_add_response(event, "BalanceMax", "0");
	upnp_add_response(event, "FadeMax", "0");
	return 0;
}

static int volume_inc(struct action_event *event)
{
	service_lock();
	if (current_volume < 16) {
		current_volume++;
		replace_var_uint(VOLUME_VAR_VOLUME, current_volume);
		output_set_volume(current_volume / 16.0);
	}
	service_unlock();
	return 0;
}

static int volume_dec(struct action_event *event)
{
	service_lock();
	if (current_volume > 0) {
		current_volume--;
		replace_var_uint(VOLUME_VAR_VOLUME, current_volume);
		output_set_volume(current_volume / 16.0);
	}
	service_unlock();
	return 0;
}

static int set_volume(struct action_event *event)
{
	char *str = upnp_get_string(event, "Value");
	if (str == NULL)
		return -1;

	int vol = current_volume;
	sscanf(str, "%u", &vol);
	if (vol > 16) {
		upnp_set_error(event, 800, "Invalid volume");
	}
	free(str);
	service_lock();
	current_volume = vol;
	replace_var_uint(VOLUME_VAR_VOLUME, vol);
	output_set_volume(vol / 16.0);
	service_unlock();
	return 0;
}

static struct action volume_actions[] = {
	[VOLUME_CMD_VOLUME] = { "Volume", volume },
	[VOLUME_CMD_SET_VOLUME] = { "SetVolume", set_volume },
	[VOLUME_CMD_VOLUME_INC] = { "VolumeInc", volume_inc },
	[VOLUME_CMD_VOLUME_DEC] = { "VolumeDec", volume_dec },
	[VOLUME_CMD_VOLUME_LIMIT] = { "VolumeLimit", volume_limit },
	[VOLUME_CMD_BALANCE] = { "Balance", zero_value },
	[VOLUME_CMD_SET_BALANCE] = { "SetBalance", no_action },
	[VOLUME_CMD_BALANCE_INC] = { "BalanceInc", no_action },
	[VOLUME_CMD_BALANCE_DEC] = { "BalanceDec", no_action },
	[VOLUME_CMD_FADE] = { "Fade", zero_value },
	[VOLUME_CMD_SET_FADE] = { "SetFade", no_action },
	[VOLUME_CMD_FADE_INC] = { "FadeInc", no_action },
	[VOLUME_CMD_FADE_DEC] = { "FadeDec", no_action },
	[VOLUME_CMD_MUTE] = { "Mute", mute },
	[VOLUME_CMD_SET_MUTE] = { "SetMute", set_mute },
	[VOLUME_CMD_CHARACTERISTICS] = { "Characteristics", characteristics },
	[VOLUME_CMD_UNKNOWN] = {NULL, NULL}
};

struct service *oh_volume_get_service(void) {
	if (volume_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(VOLUME_VAR_COUNT,
						  &volume_service_,
					      volume_default_values);
		volume_service_.variable_container = state_variables_;
	}
	return &volume_service_;
}

void oh_volume_init(struct upnp_device *device) {
	assert(volume_service_.var_change_collector == NULL);
	volume_service_.var_change_collector =
		UPnPVarChangeCollector_new(state_variables_, 
			"",  /* const char *event_xml_namespace - not used, since we do not provide LastChange variable */
			device,
			VOLUME_SERVICE_ID);
}

struct service volume_service_ = {
	.service_id =           VOLUME_SERVICE_ID,
	.service_type =         VOLUME_TYPE,
	.scpd_url =				VOLUME_SCPD_URL,
	.control_url =			VOLUME_CONTROL_URL,
	.event_url =			VOLUME_EVENT_URL,
	.actions =              volume_actions,
	.action_arguments =     argument_list,
	.variable_names =       volume_variable_names,
	.variable_container =   NULL,
	.var_change_collector =          NULL,
	.variable_meta =        volume_var_meta,
	.variable_count =       VOLUME_VAR_UNKNOWN,
	.command_count =        VOLUME_CMD_UNKNOWN,
	.service_mutex =        &volume_mutex
};

