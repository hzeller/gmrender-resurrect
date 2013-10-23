/* upnp_control.c - UPnP RenderingControl routines
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

#include "upnp_control.h"

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>

#include <upnp/upnp.h>
#include <upnp/ithread.h>

#include "logging.h"
#include "webserver.h"
#include "upnp.h"
#include "upnp_device.h"
#include "output.h"
#include "xmlescape.h"
#include "variable-container.h"

#define CONTROL_TYPE "urn:schemas-upnp-org:service:RenderingControl:1"

// For some reason (predates me), this was explicitly commented out and
// set to the service type; were there clients that were confused about the
// right use of the service-ID ? Setting this back, let's see what happens.
#define CONTROL_SERVICE_ID "urn:upnp-org:serviceId:RenderingControl"
//#define CONTROL_SERVICE_ID CONTROL_TYPE
#define CONTROL_SCPD_URL "/upnp/rendercontrolSCPD.xml"
#define CONTROL_CONTROL_URL "/upnp/control/rendercontrol1"
#define CONTROL_EVENT_URL "/upnp/event/rendercontrol1"

// Namespace, see UPnP-av-RenderingControl-v3-Service-20101231.pdf page 19
#define CONTROL_EVENT_XML_NS "urn:schemas-upnp-org:metadata-1-0/RCS/"

typedef enum {
	CONTROL_VAR_G_GAIN,
	CONTROL_VAR_B_BLACK,
	CONTROL_VAR_VER_KEYSTONE,
	CONTROL_VAR_G_BLACK,
	CONTROL_VAR_VOLUME,
	CONTROL_VAR_LOUDNESS,
	CONTROL_VAR_AAT_INSTANCE_ID,
	CONTROL_VAR_R_GAIN,
	CONTROL_VAR_COLOR_TEMP,
	CONTROL_VAR_SHARPNESS,
	CONTROL_VAR_AAT_PRESET_NAME,
	CONTROL_VAR_R_BLACK,
	CONTROL_VAR_B_GAIN,
	CONTROL_VAR_MUTE,
	CONTROL_VAR_LAST_CHANGE,
	CONTROL_VAR_AAT_CHANNEL,
	CONTROL_VAR_HOR_KEYSTONE,
	CONTROL_VAR_VOLUME_DB,
	CONTROL_VAR_PRESET_NAME_LIST,
	CONTROL_VAR_CONTRAST,
	CONTROL_VAR_BRIGHTNESS,
	CONTROL_VAR_UNKNOWN,
	CONTROL_VAR_COUNT
} control_variable_t;

typedef enum {
	CONTROL_CMD_GET_BLUE_BLACK,
	CONTROL_CMD_GET_BLUE_GAIN,
	CONTROL_CMD_GET_BRIGHTNESS,
	CONTROL_CMD_GET_COLOR_TEMP,
	CONTROL_CMD_GET_CONTRAST,
	CONTROL_CMD_GET_GREEN_BLACK,
	CONTROL_CMD_GET_GREEN_GAIN,
	CONTROL_CMD_GET_HOR_KEYSTONE,
	CONTROL_CMD_GET_LOUDNESS,
	CONTROL_CMD_GET_MUTE,
	CONTROL_CMD_GET_RED_BLACK,
	CONTROL_CMD_GET_RED_GAIN,
	CONTROL_CMD_GET_SHARPNESS,
	CONTROL_CMD_GET_VERT_KEYSTONE,
	CONTROL_CMD_GET_VOL,
	CONTROL_CMD_GET_VOL_DB,
	CONTROL_CMD_GET_VOL_DBRANGE,
	CONTROL_CMD_LIST_PRESETS,      
	//CONTROL_CMD_SELECT_PRESET,
	//CONTROL_CMD_SET_BLUE_BLACK,
	//CONTROL_CMD_SET_BLUE_GAIN,
	//CONTROL_CMD_SET_BRIGHTNESS,
	//CONTROL_CMD_SET_COLOR_TEMP,
	//CONTROL_CMD_SET_CONTRAST,
	//CONTROL_CMD_SET_GREEN_BLACK,
	//CONTROL_CMD_SET_GREEN_GAIN,
	//CONTROL_CMD_SET_HOR_KEYSTONE,
	//CONTROL_CMD_SET_LOUDNESS,       
	CONTROL_CMD_SET_MUTE,
	//CONTROL_CMD_SET_RED_BLACK,
	//CONTROL_CMD_SET_RED_GAIN,
	//CONTROL_CMD_SET_SHARPNESS,
	//CONTROL_CMD_SET_VERT_KEYSTONE,
	CONTROL_CMD_SET_VOL,
	CONTROL_CMD_SET_VOL_DB,
	CONTROL_CMD_UNKNOWN,
	CONTROL_CMD_COUNT
} control_cmd;

static struct action control_actions[];

static const char *control_variable_names[] = {
	[CONTROL_VAR_LAST_CHANGE] = "LastChange",
	[CONTROL_VAR_PRESET_NAME_LIST] = "PresetNameList",
	[CONTROL_VAR_AAT_CHANNEL] = "A_ARG_TYPE_Channel",
	[CONTROL_VAR_AAT_INSTANCE_ID] = "A_ARG_TYPE_InstanceID",
	[CONTROL_VAR_AAT_PRESET_NAME] = "A_ARG_TYPE_PresetName",
	[CONTROL_VAR_BRIGHTNESS] = "Brightness",
	[CONTROL_VAR_CONTRAST] = "Contrast",
	[CONTROL_VAR_SHARPNESS] = "Sharpness",
	[CONTROL_VAR_R_GAIN] = "RedVideoGain",
	[CONTROL_VAR_G_GAIN] = "GreenVideoGain",
	[CONTROL_VAR_B_GAIN] = "BlueVideoGain",
	[CONTROL_VAR_R_BLACK] = "RedVideoBlackLevel",
	[CONTROL_VAR_G_BLACK] = "GreenVideoBlackLevel",
	[CONTROL_VAR_B_BLACK] = "BlueVideoBlackLevel",
	[CONTROL_VAR_COLOR_TEMP] = "ColorTemperature",
	[CONTROL_VAR_HOR_KEYSTONE] = "HorizontalKeystone",
	[CONTROL_VAR_VER_KEYSTONE] = "VerticalKeystone",
	[CONTROL_VAR_MUTE] = "Mute",
	[CONTROL_VAR_VOLUME] = "Volume",
	[CONTROL_VAR_VOLUME_DB] = "VolumeDB",
	[CONTROL_VAR_LOUDNESS] = "Loudness",
	[CONTROL_VAR_UNKNOWN] = NULL
};

static const char *aat_presetnames[] =
{
	"FactoryDefaults",
	"InstallationDefaults",
	"Vendor defined",
	NULL
};

static const char *aat_channels[] =
{
	"Master",
	"LF",
	"RF",
	//"CF",
	//"LFE",
	//"LS",
	//"RS",
	//"LFC",
	//"RFC",
	//"SD",
	//"SL",
	//"SR",
	//"T",
	//"B",
	NULL
};

// We split our volume range into two ranges with different slope.
// The first half goes from min_db ... mid_db, the second half
// from mid_db .. max_db.
static const float vol_min_db = -60.0;
static const float vol_mid_db = -20.0;
static const float vol_max_db = 0.0;
static const int vol_mid_point = 50;  // volume_range.max / 2

// Note, some players don't read the range and assume 0..100. So better leave
// it like this.
static struct param_range volume_range = { 0, 100, 1 };
static struct param_range volume_db_range = { -60 * 256, 0, 0 };  // volume_min_db


// The following are not really relevant for a sound renderer.
static struct param_range brightness_range = { 0, 100, 1 };
static struct param_range contrast_range = { 0, 100, 1 };
static struct param_range sharpness_range = { 0, 100, 1 };
static struct param_range vid_gain_range = { 0, 100, 1 };
static struct param_range vid_black_range = { 0, 100, 1 };
static struct param_range colortemp_range = { 0, 65535, 1 };
static struct param_range keystone_range = { -32768, 32767, 1 };

static struct var_meta control_var_meta[] = {
	[CONTROL_VAR_LAST_CHANGE] =		{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[CONTROL_VAR_PRESET_NAME_LIST] =	{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[CONTROL_VAR_AAT_CHANNEL] =		{ SENDEVENT_NO, DATATYPE_STRING, aat_channels, NULL },
	[CONTROL_VAR_AAT_INSTANCE_ID] =		{ SENDEVENT_NO, DATATYPE_UI4, NULL, NULL },
	[CONTROL_VAR_AAT_PRESET_NAME] =		{ SENDEVENT_NO, DATATYPE_STRING, aat_presetnames, NULL },
	[CONTROL_VAR_BRIGHTNESS] =		{ SENDEVENT_NO, DATATYPE_UI2, NULL, &brightness_range },
	[CONTROL_VAR_CONTRAST] =		{ SENDEVENT_NO, DATATYPE_UI2, NULL, &contrast_range },
	[CONTROL_VAR_SHARPNESS] =		{ SENDEVENT_NO, DATATYPE_UI2, NULL, &sharpness_range },
	[CONTROL_VAR_R_GAIN] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_gain_range },
	[CONTROL_VAR_G_GAIN] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_gain_range },
	[CONTROL_VAR_B_GAIN] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_gain_range },
	[CONTROL_VAR_R_BLACK] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_black_range },
	[CONTROL_VAR_G_BLACK] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_black_range },
	[CONTROL_VAR_B_BLACK] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &vid_black_range },
	[CONTROL_VAR_COLOR_TEMP] =		{ SENDEVENT_NO, DATATYPE_UI2, NULL, &colortemp_range },
	[CONTROL_VAR_HOR_KEYSTONE] =		{ SENDEVENT_NO, DATATYPE_I2, NULL, &keystone_range },
	[CONTROL_VAR_VER_KEYSTONE] =		{ SENDEVENT_NO, DATATYPE_I2, NULL, &keystone_range },
	[CONTROL_VAR_MUTE] =			{ SENDEVENT_NO, DATATYPE_BOOLEAN, NULL, NULL },
	[CONTROL_VAR_VOLUME] =			{ SENDEVENT_NO, DATATYPE_UI2, NULL, &volume_range },
	[CONTROL_VAR_VOLUME_DB] =		{ SENDEVENT_NO, DATATYPE_I2, NULL, &volume_db_range },
	[CONTROL_VAR_LOUDNESS] =		{ SENDEVENT_NO, DATATYPE_BOOLEAN, NULL, NULL },
	[CONTROL_VAR_UNKNOWN] =			{ SENDEVENT_NO, DATATYPE_UNKNOWN, NULL, NULL }
};

static const char *control_default_values[] = {
	[CONTROL_VAR_LAST_CHANGE] = "<Event xmlns = \"urn:schemas-upnp-org:metadata-1-0/RCS/\"/>",
	[CONTROL_VAR_PRESET_NAME_LIST] = "",
	[CONTROL_VAR_AAT_CHANNEL] = "",
	[CONTROL_VAR_AAT_INSTANCE_ID] = "0",
	[CONTROL_VAR_AAT_PRESET_NAME] = "",
	[CONTROL_VAR_BRIGHTNESS] = "0",
	[CONTROL_VAR_CONTRAST] = "0",
	[CONTROL_VAR_SHARPNESS] = "0",
	[CONTROL_VAR_R_GAIN] = "0",
	[CONTROL_VAR_G_GAIN] = "0",
	[CONTROL_VAR_B_GAIN] = "0",
	[CONTROL_VAR_R_BLACK] = "0",
	[CONTROL_VAR_G_BLACK] = "0",
	[CONTROL_VAR_B_BLACK] = "0",
	[CONTROL_VAR_COLOR_TEMP] = "0",
	[CONTROL_VAR_HOR_KEYSTONE] = "0",
	[CONTROL_VAR_VER_KEYSTONE] = "0",
	[CONTROL_VAR_MUTE] = "0",
	[CONTROL_VAR_VOLUME] = "0",
	[CONTROL_VAR_VOLUME_DB] = "0",
	[CONTROL_VAR_LOUDNESS] = "0",
	[CONTROL_VAR_UNKNOWN] = NULL
};

extern struct service control_service_;   // Defined below.
static variable_container_t *state_variables_ = NULL;

static ithread_mutex_t control_mutex;

static void service_lock(void)
{
	ithread_mutex_lock(&control_mutex);
	if (control_service_.last_change) {
		UPnPLastChangeCollector_start(control_service_.last_change);
	}
}

static void service_unlock(void)
{
	if (control_service_.last_change) {
		UPnPLastChangeCollector_finish(control_service_.last_change);
	}
	ithread_mutex_unlock(&control_mutex);
}

static struct argument *arguments_list_presets[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentPresetNameList", PARAM_DIR_OUT, CONTROL_VAR_PRESET_NAME_LIST },
	NULL
};
// static struct argument *arguments_select_preset[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "PresetName", PARAM_DIR_IN, CONTROL_VAR_AAT_PRESET_NAME },
// 	NULL
// };
static struct argument *arguments_get_brightness[] = {        
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentBrightness", PARAM_DIR_OUT, CONTROL_VAR_BRIGHTNESS },
	NULL
};
// static struct argument *arguments_set_brightness[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredBrightness", PARAM_DIR_IN, CONTROL_VAR_BRIGHTNESS },
// 	NULL
// };
static struct argument *arguments_get_contrast[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentContrast", PARAM_DIR_OUT, CONTROL_VAR_CONTRAST },
	NULL
};
// static struct argument *arguments_set_contrast[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredContrast", PARAM_DIR_IN, CONTROL_VAR_CONTRAST },
// 	NULL
// };
static struct argument *arguments_get_sharpness[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentSharpness", PARAM_DIR_OUT, CONTROL_VAR_SHARPNESS },
	NULL
};
// static struct argument *arguments_set_sharpness[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredSharpness", PARAM_DIR_IN, CONTROL_VAR_SHARPNESS },
// 	NULL
// };
static struct argument *arguments_get_red_gain[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentRedVideoGain", PARAM_DIR_OUT, CONTROL_VAR_R_GAIN },
	NULL
};
// static struct argument *arguments_set_red_gain[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredRedVideoGain", PARAM_DIR_IN, CONTROL_VAR_R_GAIN },
// 	NULL
// };
static struct argument *arguments_get_green_gain[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentGreenVideoGain", PARAM_DIR_OUT, CONTROL_VAR_G_GAIN },
	NULL
};
// static struct argument *arguments_set_green_gain[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredGreenVideoGain", PARAM_DIR_IN, CONTROL_VAR_G_GAIN },
// 	NULL
// };
static struct argument *arguments_get_blue_gain[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentBlueVideoGain", PARAM_DIR_OUT, CONTROL_VAR_B_GAIN },
	NULL
};
// static struct argument *arguments_set_blue_gain[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredBlueVideoGain", PARAM_DIR_IN, CONTROL_VAR_B_GAIN },
// 	NULL
// };
static struct argument *arguments_get_red_black[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentRedVideoBlackLevel", PARAM_DIR_OUT, CONTROL_VAR_R_BLACK },
	NULL
};
// static struct argument *arguments_set_red_black[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredRedVideoBlackLevel", PARAM_DIR_IN, CONTROL_VAR_R_BLACK },
// 	NULL
// };
static struct argument *arguments_get_green_black[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentGreenVideoBlackLevel", PARAM_DIR_OUT, CONTROL_VAR_G_BLACK },
	NULL
};
// static struct argument *arguments_set_green_black[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredGreenVideoBlackLevel", PARAM_DIR_IN, CONTROL_VAR_G_BLACK },
// 	NULL
// };
static struct argument *arguments_get_blue_black[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentBlueVideoBlackLevel", PARAM_DIR_OUT, CONTROL_VAR_B_BLACK },
	NULL
};
// static struct argument *arguments_set_blue_black[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredBlueVideoBlackLevel", PARAM_DIR_IN, CONTROL_VAR_B_BLACK },
// 	NULL
// };
static struct argument *arguments_get_color_temp[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentColorTemperature", PARAM_DIR_OUT, CONTROL_VAR_COLOR_TEMP },
	NULL
};
// static struct argument *arguments_set_color_temp[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredColorTemperature", PARAM_DIR_IN, CONTROL_VAR_COLOR_TEMP },
// 	NULL
// };
static struct argument *arguments_get_hor_keystone[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentHorizontalKeystone", PARAM_DIR_OUT, CONTROL_VAR_HOR_KEYSTONE },
	NULL
};
// static struct argument *arguments_set_hor_keystone[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredHorizontalKeystone", PARAM_DIR_IN, CONTROL_VAR_HOR_KEYSTONE },
// 	NULL
// };
static struct argument *arguments_get_vert_keystone[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "CurrentVerticalKeystone", PARAM_DIR_OUT, CONTROL_VAR_VER_KEYSTONE },
	NULL
};
// static struct argument *arguments_set_vert_keystone[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "DesiredVerticalKeystone", PARAM_DIR_IN, CONTROL_VAR_VER_KEYSTONE },
// 	NULL
// };
static struct argument *arguments_get_mute[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "CurrentMute", PARAM_DIR_OUT, CONTROL_VAR_MUTE },
	NULL
};
static struct argument *arguments_set_mute[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "DesiredMute", PARAM_DIR_IN, CONTROL_VAR_MUTE },
	NULL
};
static struct argument *arguments_get_vol[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "CurrentVolume", PARAM_DIR_OUT, CONTROL_VAR_VOLUME },
	NULL
};
static struct argument *arguments_set_vol[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "DesiredVolume", PARAM_DIR_IN, CONTROL_VAR_VOLUME },
	NULL
};
static struct argument *arguments_get_vol_db[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "CurrentVolume", PARAM_DIR_OUT, CONTROL_VAR_VOLUME_DB },
	NULL
};
static struct argument *arguments_set_vol_db[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "DesiredVolume", PARAM_DIR_IN, CONTROL_VAR_VOLUME_DB },
	NULL
};
static struct argument *arguments_get_vol_dbrange[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "MinValue", PARAM_DIR_OUT, CONTROL_VAR_VOLUME_DB },
	& (struct argument) { "MaxValue", PARAM_DIR_OUT, CONTROL_VAR_VOLUME_DB },
	NULL
};
static struct argument *arguments_get_loudness[] = {
	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
	& (struct argument) { "CurrentLoudness", PARAM_DIR_OUT, CONTROL_VAR_LOUDNESS },
	NULL
};
// static struct argument *arguments_set_loudness[] = {
// 	& (struct argument) { "InstanceID", PARAM_DIR_IN, CONTROL_VAR_AAT_INSTANCE_ID },
// 	& (struct argument) { "Channel", PARAM_DIR_IN, CONTROL_VAR_AAT_CHANNEL },
// 	& (struct argument) { "DesiredLoudness", PARAM_DIR_IN, CONTROL_VAR_LOUDNESS },
// 	NULL
// };


static struct argument **argument_list[] = {
	[CONTROL_CMD_LIST_PRESETS] =        	arguments_list_presets,
	//[CONTROL_CMD_SELECT_PRESET] =       	arguments_select_preset, 
	[CONTROL_CMD_GET_BRIGHTNESS] =      	arguments_get_brightness,        
	//[CONTROL_CMD_SET_BRIGHTNESS] =      	arguments_set_brightness,
	[CONTROL_CMD_GET_CONTRAST] =        	arguments_get_contrast,
	//[CONTROL_CMD_SET_CONTRAST] =        	arguments_set_contrast,
	[CONTROL_CMD_GET_SHARPNESS] =       	arguments_get_sharpness,
	//[CONTROL_CMD_SET_SHARPNESS] =       	arguments_set_sharpness,
	[CONTROL_CMD_GET_RED_GAIN] =        	arguments_get_red_gain,
	//[CONTROL_CMD_SET_RED_GAIN] =        	arguments_set_red_gain,
	[CONTROL_CMD_GET_GREEN_GAIN] =      	arguments_get_green_gain,
	//[CONTROL_CMD_SET_GREEN_GAIN] =      	arguments_set_green_gain,
	[CONTROL_CMD_GET_BLUE_GAIN] =       	arguments_get_blue_gain,
	//[CONTROL_CMD_SET_BLUE_GAIN] =       	arguments_set_blue_gain,
	[CONTROL_CMD_GET_RED_BLACK] =       	arguments_get_red_black,
	//[CONTROL_CMD_SET_RED_BLACK] =       	arguments_set_red_black,
	[CONTROL_CMD_GET_GREEN_BLACK] =     	arguments_get_green_black,
	//[CONTROL_CMD_SET_GREEN_BLACK] =     	arguments_set_green_black,
	[CONTROL_CMD_GET_BLUE_BLACK] =      	arguments_get_blue_black,
	//[CONTROL_CMD_SET_BLUE_BLACK] =      	arguments_set_blue_black,
	[CONTROL_CMD_GET_COLOR_TEMP] =      	arguments_get_color_temp,
	//[CONTROL_CMD_SET_COLOR_TEMP] =      	arguments_set_color_temp,
	[CONTROL_CMD_GET_HOR_KEYSTONE] =    	arguments_get_hor_keystone,
	//[CONTROL_CMD_SET_HOR_KEYSTONE] =    	arguments_set_hor_keystone,
	[CONTROL_CMD_GET_VERT_KEYSTONE] =   	arguments_get_vert_keystone,
	//[CONTROL_CMD_SET_VERT_KEYSTONE] =   	arguments_set_vert_keystone,
	[CONTROL_CMD_GET_MUTE] =            	arguments_get_mute,
	[CONTROL_CMD_SET_MUTE] =            	arguments_set_mute,
	[CONTROL_CMD_GET_VOL] =             	arguments_get_vol,
	[CONTROL_CMD_SET_VOL] =             	arguments_set_vol,
	[CONTROL_CMD_GET_VOL_DB] =          	arguments_get_vol_db,
	[CONTROL_CMD_SET_VOL_DB] =          	arguments_set_vol_db,
	[CONTROL_CMD_GET_VOL_DBRANGE] =     	arguments_get_vol_dbrange,
	[CONTROL_CMD_GET_LOUDNESS] =        	arguments_get_loudness,
	//[CONTROL_CMD_SET_LOUDNESS] =        	arguments_set_loudness,
	[CONTROL_CMD_UNKNOWN] =			NULL
};


// Replace given variable without sending an state-change event.
static void replace_var(control_variable_t varnum, const char *new_value) {
	VariableContainer_change(state_variables_, varnum, new_value);
}

static void change_volume(const char *volume, const char *db_volume) {
	replace_var(CONTROL_VAR_VOLUME, volume);
	replace_var(CONTROL_VAR_VOLUME_DB, db_volume);
}

static int cmd_obtain_variable(struct action_event *event,
			       control_variable_t varnum,
			       const char *paramname)
{
	char *instance = upnp_get_string(event, "InstanceID");
	if (instance == NULL) {
		return -1;
	}
	Log_info("control", "%s: %s for instance %s\n",
		 __FUNCTION__, paramname, instance);
	free(instance);   // we don't care about that value for now.

	upnp_append_variable(event, varnum, paramname);
	return 0;
}

static int list_presets(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_PRESET_NAME_LIST,
				   "CurrentPresetNameList");
}

static int get_brightness(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_BRIGHTNESS,
				   "CurrentBrightness");
}

static int get_contrast(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_CONTRAST,
				   "CurrentContrast");
}

static int get_sharpness(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_SHARPNESS,
				   "CurrentSharpness");
}

static int get_red_videogain(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_R_GAIN,
				   "CurrentRedVideoGain");
}

static int get_green_videogain(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_G_GAIN,
				   "CurrentGreenVideoGain");
}

static int get_blue_videogain(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_B_GAIN,
				   "CurrentBlueVideoGain");
}

static int get_red_videoblacklevel(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_R_BLACK,
				   "CurrentRedVideoBlackLevel");
}

static int get_green_videoblacklevel(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_G_BLACK,
				   "CurrentGreenVideoBlackLevel");
}

static int get_blue_videoblacklevel(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_B_BLACK,
				   "CurrentBlueVideoBlackLevel");
}

static int get_colortemperature(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_COLOR_TEMP,
				   "CurrentColorTemperature");
}

static int get_horizontal_keystone(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_HOR_KEYSTONE,
				   "CurrentHorizontalKeystone");
}

static int get_vertical_keystone(struct action_event *event)
{
	return cmd_obtain_variable(event, CONTROL_VAR_VER_KEYSTONE,
				   "CurrentVerticalKeystone");
}

static int get_mute(struct action_event *event)
{
	/* FIXME - Channel */
	return cmd_obtain_variable(event, CONTROL_VAR_MUTE, "CurrentMute");
}

static void set_mute_toggle(int do_mute) {
	replace_var(CONTROL_VAR_MUTE, do_mute ? "1" : "0");
	output_set_mute(do_mute);
}

static int set_mute(struct action_event *event) {
	const char *value = upnp_get_string(event, "DesiredMute");
	service_lock();
	const int do_mute = atoi(value);
	set_mute_toggle(do_mute);
	replace_var(CONTROL_VAR_MUTE, do_mute ? "1" : "0");
	service_unlock();
	return 0;
}

static int get_volume(struct action_event *event)
{
	/* FIXME - Channel */
	return cmd_obtain_variable(event, CONTROL_VAR_VOLUME,
				   "CurrentVolume");
}

static float volume_level_to_decibel(int volume) {
	if (volume < volume_range.min) volume = volume_range.min;
	if (volume > volume_range.max) volume = volume_range.max;
	if (volume < volume_range.max / 2) {
		return vol_min_db
			+ (vol_mid_db - vol_min_db) / vol_mid_point * volume;
	}
	else {
		const int range = volume_range.max - vol_mid_point;
		return vol_mid_db
			+ ((vol_max_db - vol_mid_db) / range
			   * (volume - vol_mid_point));
	}
}

static int volume_decibel_to_level(float decibel) {
	if (decibel < vol_min_db) return volume_range.min;
	if (decibel > vol_max_db) return volume_range.max;
	if (decibel < vol_mid_db) {
		return (decibel - vol_min_db) * vol_mid_point / (vol_mid_db - vol_min_db);
	}
	else {
		const int range = volume_range.max - vol_mid_point;
		return (decibel - vol_mid_db) * range / (vol_max_db - vol_mid_db) + vol_mid_point;
	}
}

// Change volume variables from the given decibel. Quantize value according to
// our ranges.
static float change_volume_decibel(float raw_decibel) {
	int volume_level = volume_decibel_to_level(raw_decibel);
	// Since we quantize it to the level, lets calculate the
	// actual level.
	float decibel = volume_level_to_decibel(volume_level);

	char volume[10];
	snprintf(volume, sizeof(volume), "%d", volume_level);
	char db_volume[10];
	snprintf(db_volume, sizeof(db_volume), "%d", (int) (256 * decibel));

	Log_info("control", "Setting volume-db to %.2fdb == #%d",
		decibel, volume_level);

	change_volume(volume, db_volume);
	return decibel;
}

static int set_volume_db(struct action_event *event) {
	const char *str_decibel_in = upnp_get_string(event, "DesiredVolume");
	service_lock();
	float raw_decibel_in = atof(str_decibel_in);
	float decibel = change_volume_decibel(raw_decibel_in);

	output_set_volume(exp(decibel / 20 * log(10)));
	service_unlock();

	return 0;
}

static int set_volume(struct action_event *event) {
	const char *volume = upnp_get_string(event, "DesiredVolume");
	service_lock();
	int volume_level = atoi(volume);  // range 0..100
	if (volume_level < volume_range.min) volume_level = volume_range.min;
	if (volume_level > volume_range.max) volume_level = volume_range.max;
	const float decibel = volume_level_to_decibel(volume_level);

	char db_volume[10];
	snprintf(db_volume, sizeof(db_volume), "%d", (int) (256 * decibel));

	const double fraction = exp(decibel / 20 * log(10));

	change_volume(volume, db_volume);
	output_set_volume(fraction);
	set_mute_toggle(volume_level == 0);
	service_unlock();

	return 0;
}

static int get_volume_db(struct action_event *event)
{
	/* FIXME - Channel */
	return cmd_obtain_variable(event, CONTROL_VAR_VOLUME_DB,
				   "CurrentVolumeDB");
}

static int get_volume_dbrange(struct action_event *event) {
	// Ignoring instanceID and Channel
	char minval[16];
	snprintf(minval, sizeof(minval), "%lld", volume_db_range.min);
	upnp_add_response(event, "MinValue", minval);
	upnp_add_response(event, "MaxValue", "0");
	return 0;
}

static int get_loudness(struct action_event *event)
{
	/* FIXME - Channel */
	return cmd_obtain_variable(event, CONTROL_VAR_LOUDNESS,
				   "CurrentLoudness");
}


static struct action control_actions[] = {
	[CONTROL_CMD_LIST_PRESETS] =        	{"ListPresets", list_presets},
	//[CONTROL_CMD_SELECT_PRESET] =       	{"SelectPreset", NULL},
	[CONTROL_CMD_GET_BRIGHTNESS] =      	{"GetBrightness", get_brightness}, /* optional */
	//[CONTROL_CMD_SET_BRIGHTNESS] =      	{"SetBrightness", NULL}, /* optional */
	[CONTROL_CMD_GET_CONTRAST] =        	{"GetContrast", get_contrast}, /* optional */
	//[CONTROL_CMD_SET_CONTRAST] =        	{"SetContrast", NULL}, /* optional */
	[CONTROL_CMD_GET_SHARPNESS] =       	{"GetSharpness", get_sharpness}, /* optional */
	//[CONTROL_CMD_SET_SHARPNESS] =       	{"SetSharpness", NULL}, /* optional */
	[CONTROL_CMD_GET_RED_GAIN] =        	{"GetRedVideoGain", get_red_videogain}, /* optional */
	//[CONTROL_CMD_SET_RED_GAIN] =        	{"SetRedVideoGain", NULL}, /* optional */
	[CONTROL_CMD_GET_GREEN_GAIN] =      	{"GetGreenVideoGain", get_green_videogain}, /* optional */
	//[CONTROL_CMD_SET_GREEN_GAIN] =      	{"SetGreenVideoGain", NULL}, /* optional */
	[CONTROL_CMD_GET_BLUE_GAIN] =       	{"GetBlueVideoGain", get_blue_videogain}, /* optional */
	//[CONTROL_CMD_SET_BLUE_GAIN] =       	{"SetBlueVideoGain", NULL}, /* optional */
	[CONTROL_CMD_GET_RED_BLACK] =       	{"GetRedVideoBlackLevel", get_red_videoblacklevel}, /* optional */
	//[CONTROL_CMD_SET_RED_BLACK] =       	{"SetRedVideoBlackLevel", NULL}, /* optional */
	[CONTROL_CMD_GET_GREEN_BLACK] =     	{"GetGreenVideoBlackLevel", get_green_videoblacklevel}, /* optional */
	//[CONTROL_CMD_SET_GREEN_BLACK] =     	{"SetGreenVideoBlackLevel", NULL}, /* optional */
	[CONTROL_CMD_GET_BLUE_BLACK] =      	{"GetBlueVideoBlackLevel", get_blue_videoblacklevel}, /* optional */
	//[CONTROL_CMD_SET_BLUE_BLACK] =      	{"SetBlueVideoBlackLevel", NULL}, /* optional */
	[CONTROL_CMD_GET_COLOR_TEMP] =      	{"GetColorTemperature", get_colortemperature}, /* optional */
	//[CONTROL_CMD_SET_COLOR_TEMP] =      	{"SetColorTemperature", NULL}, /* optional */
	[CONTROL_CMD_GET_HOR_KEYSTONE] =    	{"GetHorizontalKeystone", get_horizontal_keystone}, /* optional */
	//[CONTROL_CMD_SET_HOR_KEYSTONE] =    	{"SetHorizontalKeystone", NULL}, /* optional */
	[CONTROL_CMD_GET_VERT_KEYSTONE] =   	{"GetVerticalKeystone", get_vertical_keystone}, /* optional */
	//[CONTROL_CMD_SET_VERT_KEYSTONE] =   	{"SetVerticalKeystone", NULL}, /* optional */
	[CONTROL_CMD_GET_MUTE] =            	{"GetMute", get_mute}, /* optional */
	[CONTROL_CMD_SET_MUTE] =            	{"SetMute", set_mute}, /* optional */
	[CONTROL_CMD_GET_VOL] =             	{"GetVolume", get_volume}, /* optional */
	[CONTROL_CMD_SET_VOL] =             	{"SetVolume", set_volume}, /* optional */
	[CONTROL_CMD_GET_VOL_DB] =          	{"GetVolumeDB", get_volume_db}, /* optional */
	[CONTROL_CMD_SET_VOL_DB] =          	{"SetVolumeDB", set_volume_db}, /* optional */
	[CONTROL_CMD_GET_VOL_DBRANGE] =     	{"GetVolumeDBRange", get_volume_dbrange}, /* optional */
	[CONTROL_CMD_GET_LOUDNESS] =        	{"GetLoudness", get_loudness}, /* optional */
	//[CONTROL_CMD_SET_LOUDNESS] =        	{"SetLoudness", NULL}, /* optional */
	[CONTROL_CMD_UNKNOWN] =			{NULL, NULL}
};

struct service *upnp_control_get_service(void) {
	if (control_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(CONTROL_VAR_COUNT,
					      control_variable_names,
					      control_default_values);
		control_service_.variable_container = state_variables_;
	}

	return &control_service_;
}

void upnp_control_init(struct upnp_device *device) {
	upnp_control_get_service();

	// Set initial volume.
	float volume_fraction = 0;
	if (output_get_volume(&volume_fraction) == 0) {
		Log_info("control", "Output inital volume is %f; setting "
			 "control variables accordingly.", volume_fraction);
		change_volume_decibel(20 * log(volume_fraction) / log(10));
	}

	assert(control_service_.last_change == NULL);
	control_service_.last_change =
		UPnPLastChangeCollector_new(state_variables_, CONTROL_EVENT_XML_NS,
					    device,
					    CONTROL_SERVICE_ID);
	// According to UPnP-av-RenderingControl-v3-Service-20101231.pdf, 2.3.1
	// page 51, the A_ARG_TYPE* variables are not evented.
	UPnPLastChangeCollector_add_ignore(control_service_.last_change,
					   CONTROL_VAR_AAT_CHANNEL);
	UPnPLastChangeCollector_add_ignore(control_service_.last_change,
					   CONTROL_VAR_AAT_INSTANCE_ID);
	UPnPLastChangeCollector_add_ignore(control_service_.last_change,
					   CONTROL_VAR_AAT_PRESET_NAME);
}

void upnp_control_register_variable_listener(variable_change_listener_t cb,
					     void *userdata) {
	VariableContainer_register_callback(state_variables_, cb, userdata);
}

struct service control_service_ = {
	.service_id =	       CONTROL_SERVICE_ID,
	.service_type =	       CONTROL_TYPE,
        .scpd_url =            CONTROL_SCPD_URL,
        .control_url =         CONTROL_CONTROL_URL,
        .event_url =           CONTROL_EVENT_URL,
	.event_xml_ns =        CONTROL_EVENT_XML_NS,
	.actions =	       control_actions,
	.action_arguments =    argument_list,
	.variable_names =      control_variable_names,
	.variable_container =  NULL,  // set later.
	.last_change =         NULL,
	.variable_meta =       control_var_meta,
	.variable_count =      CONTROL_VAR_UNKNOWN,
	.command_count =       CONTROL_CMD_UNKNOWN,
	.service_mutex =       &control_mutex
};
