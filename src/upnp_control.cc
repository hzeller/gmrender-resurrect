// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* See feature_test_macros(7) */
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ithread.h>
#include <upnp.h>

#include "logging.h"
#include "output.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"
#include "webserver.h"
#include "xmlescape.h"

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
  // CONTROL_CMD_SELECT_PRESET,
  // CONTROL_CMD_SET_BLUE_BLACK,
  // CONTROL_CMD_SET_BLUE_GAIN,
  // CONTROL_CMD_SET_BRIGHTNESS,
  // CONTROL_CMD_SET_COLOR_TEMP,
  // CONTROL_CMD_SET_CONTRAST,
  // CONTROL_CMD_SET_GREEN_BLACK,
  // CONTROL_CMD_SET_GREEN_GAIN,
  // CONTROL_CMD_SET_HOR_KEYSTONE,
  // CONTROL_CMD_SET_LOUDNESS,
  CONTROL_CMD_SET_MUTE,
  // CONTROL_CMD_SET_RED_BLACK,
  // CONTROL_CMD_SET_RED_GAIN,
  // CONTROL_CMD_SET_SHARPNESS,
  // CONTROL_CMD_SET_VERT_KEYSTONE,
  CONTROL_CMD_SET_VOL,
  CONTROL_CMD_SET_VOL_DB,
  CONTROL_CMD_COUNT
} control_cmd;

static const char *aat_presetnames[] = {
    "FactoryDefaults", "InstallationDefaults", "Vendor defined", NULL};

static const char *aat_channels[] = {"Master", "LF", "RF",
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
                                     NULL};

// We split our volume range into two ranges with different slope.
// The first half goes from min_db ... mid_db, the second half
// from mid_db .. max_db.
static const float vol_min_db = -60.0;
static const float vol_mid_db = -20.0;
static const float vol_max_db = 0.0;
static const int vol_mid_point = 50;  // volume_range.max / 2

// Note, some players don't read the range and assume 0..100. So better leave
// it like this.
static struct param_range volume_range = {0, 100, 1};
static struct param_range volume_db_range = {-60 * 256, 0, 0};  // volume_min_db

// The following are not really relevant for a sound renderer.
static struct param_range brightness_range = {0, 100, 1};
static struct param_range contrast_range = {0, 100, 1};
static struct param_range sharpness_range = {0, 100, 1};
static struct param_range vid_gain_range = {0, 100, 1};
static struct param_range vid_black_range = {0, 100, 1};
static struct param_range colortemp_range = {0, 65535, 1};
static struct param_range keystone_range = {-32768, 32767, 1};

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
  CONTROL_VAR_COUNT
} control_variable_t;

static VariableContainer *state_variables_ = NULL;

static ithread_mutex_t control_mutex;

static void service_lock(void) {
  ithread_mutex_lock(&control_mutex);
  auto collector = upnp_control_get_service()->last_change;
  if (collector) collector->Start();
}

static void service_unlock(void) {
  auto collector = upnp_control_get_service()->last_change;
  if (collector) collector->Finish();
  ithread_mutex_unlock(&control_mutex);
}

static struct argument arguments_list_presets[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentPresetNameList", ParamDir::kOut, CONTROL_VAR_PRESET_NAME_LIST},
    {NULL}};
// static struct argument arguments_select_preset[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "PresetName", ParamDir::kIn, CONTROL_VAR_AAT_PRESET_NAME },
// 	{ NULL }
// };
static struct argument arguments_get_brightness[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentBrightness", ParamDir::kOut, CONTROL_VAR_BRIGHTNESS},
    {NULL}};
// static struct argument arguments_set_brightness[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredBrightness", ParamDir::kIn, CONTROL_VAR_BRIGHTNESS },
// 	{ NULL }
// };
static struct argument arguments_get_contrast[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentContrast", ParamDir::kOut, CONTROL_VAR_CONTRAST},
    {NULL}};
// static struct argument arguments_set_contrast[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredContrast", ParamDir::kIn, CONTROL_VAR_CONTRAST },
// 	{ NULL }
// };
static struct argument arguments_get_sharpness[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentSharpness", ParamDir::kOut, CONTROL_VAR_SHARPNESS},
    {NULL}};
// static struct argument arguments_set_sharpness[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredSharpness", ParamDir::kIn, CONTROL_VAR_SHARPNESS },
// 	{ NULL }
// };
static struct argument arguments_get_red_gain[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentRedVideoGain", ParamDir::kOut, CONTROL_VAR_R_GAIN},
    {NULL}};
// static struct argument arguments_set_red_gain[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredRedVideoGain", ParamDir::kIn, CONTROL_VAR_R_GAIN },
//      { NULL }
// };
static struct argument arguments_get_green_gain[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentGreenVideoGain", ParamDir::kOut, CONTROL_VAR_G_GAIN},
    {NULL}};
// static struct argument arguments_set_green_gain[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredGreenVideoGain", ParamDir::kIn, CONTROL_VAR_G_GAIN },
// 	{ NULL }
// };
static struct argument arguments_get_blue_gain[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentBlueVideoGain", ParamDir::kOut, CONTROL_VAR_B_GAIN},
    {NULL}};
// static struct argument arguments_set_blue_gain[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredBlueVideoGain", ParamDir::kIn, CONTROL_VAR_B_GAIN },
// 	{ NULL }
// };
static struct argument arguments_get_red_black[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentRedVideoBlackLevel", ParamDir::kOut, CONTROL_VAR_R_BLACK},
    {NULL}};
// static struct argument arguments_set_red_black[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredRedVideoBlackLevel", ParamDir::kIn, CONTROL_VAR_R_BLACK },
// 	{ NULL }
// };
static struct argument arguments_get_green_black[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentGreenVideoBlackLevel", ParamDir::kOut, CONTROL_VAR_G_BLACK},
    {NULL}};
// static struct argument arguments_set_green_black[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredGreenVideoBlackLevel", ParamDir::kIn, CONTROL_VAR_G_BLACK },
// 	{ NULL }
// };
static struct argument arguments_get_blue_black[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentBlueVideoBlackLevel", ParamDir::kOut, CONTROL_VAR_B_BLACK},
    {NULL}};
// static struct argument arguments_set_blue_black[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredBlueVideoBlackLevel", ParamDir::kIn, CONTROL_VAR_B_BLACK },
// 	{ NULL }
// };
static struct argument arguments_get_color_temp[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentColorTemperature", ParamDir::kOut, CONTROL_VAR_COLOR_TEMP},
    {NULL}};
// static struct argument arguments_set_color_temp[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredColorTemperature", ParamDir::kIn, CONTROL_VAR_COLOR_TEMP },
// 	{ NULL }
// };
static struct argument arguments_get_hor_keystone[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentHorizontalKeystone", ParamDir::kOut, CONTROL_VAR_HOR_KEYSTONE},
    {NULL}};
// static struct argument arguments_set_hor_keystone[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredHorizontalKeystone", ParamDir::kIn, CONTROL_VAR_HOR_KEYSTONE },
// 	{ NULL }
// };
static struct argument arguments_get_vert_keystone[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"CurrentVerticalKeystone", ParamDir::kOut, CONTROL_VAR_VER_KEYSTONE},
    {NULL}};
// static struct argument arguments_set_vert_keystone[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "DesiredVerticalKeystone", ParamDir::kIn, CONTROL_VAR_VER_KEYSTONE },
// 	{ NULL }
// };
static struct argument arguments_get_mute[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"CurrentMute", ParamDir::kOut, CONTROL_VAR_MUTE},
    {NULL}};
static struct argument arguments_set_mute[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"DesiredMute", ParamDir::kIn, CONTROL_VAR_MUTE},
    {NULL}};
static struct argument arguments_get_vol[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"CurrentVolume", ParamDir::kOut, CONTROL_VAR_VOLUME},
    {NULL}};
static struct argument arguments_set_vol[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"DesiredVolume", ParamDir::kIn, CONTROL_VAR_VOLUME},
    {NULL}};
static struct argument arguments_get_vol_db[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"CurrentVolume", ParamDir::kOut, CONTROL_VAR_VOLUME_DB},
    {NULL}};
static struct argument arguments_set_vol_db[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"DesiredVolume", ParamDir::kIn, CONTROL_VAR_VOLUME_DB},
    {NULL}};
static struct argument arguments_get_vol_dbrange[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"MinValue", ParamDir::kOut, CONTROL_VAR_VOLUME_DB},
    {"MaxValue", ParamDir::kOut, CONTROL_VAR_VOLUME_DB},
    {NULL}};
static struct argument arguments_get_loudness[] = {
    {"InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID},
    {"Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL},
    {"CurrentLoudness", ParamDir::kOut, CONTROL_VAR_LOUDNESS},
    {NULL}};
// static struct argument arguments_set_loudness[] = {
// 	{ "InstanceID", ParamDir::kIn, CONTROL_VAR_AAT_INSTANCE_ID },
// 	{ "Channel", ParamDir::kIn, CONTROL_VAR_AAT_CHANNEL },
// 	{ "DesiredLoudness", ParamDir::kIn, CONTROL_VAR_LOUDNESS },
// 	{ NULL }
// };

static struct argument *argument_list[] = {
    [CONTROL_CMD_GET_BLUE_BLACK] = arguments_get_blue_black,
    [CONTROL_CMD_GET_BLUE_GAIN] = arguments_get_blue_gain,
    [CONTROL_CMD_GET_BRIGHTNESS] = arguments_get_brightness,
    [CONTROL_CMD_GET_COLOR_TEMP] = arguments_get_color_temp,
    [CONTROL_CMD_GET_CONTRAST] = arguments_get_contrast,
    [CONTROL_CMD_GET_GREEN_BLACK] = arguments_get_green_black,
    [CONTROL_CMD_GET_GREEN_GAIN] = arguments_get_green_gain,
    [CONTROL_CMD_GET_HOR_KEYSTONE] = arguments_get_hor_keystone,
    [CONTROL_CMD_GET_LOUDNESS] = arguments_get_loudness,
    [CONTROL_CMD_GET_MUTE] = arguments_get_mute,
    [CONTROL_CMD_GET_RED_BLACK] = arguments_get_red_black,
    [CONTROL_CMD_GET_RED_GAIN] = arguments_get_red_gain,
    [CONTROL_CMD_GET_SHARPNESS] = arguments_get_sharpness,
    [CONTROL_CMD_GET_VERT_KEYSTONE] = arguments_get_vert_keystone,
    [CONTROL_CMD_GET_VOL] = arguments_get_vol,
    [CONTROL_CMD_GET_VOL_DB] = arguments_get_vol_db,
    [CONTROL_CMD_GET_VOL_DBRANGE] = arguments_get_vol_dbrange,
    [CONTROL_CMD_LIST_PRESETS] = arguments_list_presets,
    [CONTROL_CMD_SET_MUTE] = arguments_set_mute,
    [CONTROL_CMD_SET_VOL] = arguments_set_vol,
    [CONTROL_CMD_SET_VOL_DB] = arguments_set_vol_db,

    //[CONTROL_CMD_SELECT_PRESET] =       	arguments_select_preset,
    //[CONTROL_CMD_SET_BRIGHTNESS] =      	arguments_set_brightness,
    //[CONTROL_CMD_SET_CONTRAST] =        	arguments_set_contrast,
    //[CONTROL_CMD_SET_SHARPNESS] =       	arguments_set_sharpness,
    //[CONTROL_CMD_SET_RED_GAIN] =        	arguments_set_red_gain,
    //[CONTROL_CMD_SET_GREEN_GAIN] =      	arguments_set_green_gain,
    //[CONTROL_CMD_SET_BLUE_GAIN] =       	arguments_set_blue_gain,
    //[CONTROL_CMD_SET_RED_BLACK] =       	arguments_set_red_black,
    //[CONTROL_CMD_SET_GREEN_BLACK] =     	arguments_set_green_black,
    //[CONTROL_CMD_SET_BLUE_BLACK] =      	arguments_set_blue_black,
    //[CONTROL_CMD_SET_COLOR_TEMP] =      	arguments_set_color_temp,
    //[CONTROL_CMD_SET_HOR_KEYSTONE] =    	arguments_set_hor_keystone,
    //[CONTROL_CMD_SET_VERT_KEYSTONE] =   	arguments_set_vert_keystone,
    //[CONTROL_CMD_SET_LOUDNESS] =        	arguments_set_loudness,
    [CONTROL_CMD_COUNT] = NULL};

// Replace given variable without sending an state-change event.
static void replace_var(control_variable_t varnum, const char *new_value) {
  state_variables_->Set(varnum, new_value);
}

static void change_volume(const char *volume, const char *db_volume) {
  replace_var(CONTROL_VAR_VOLUME, volume);
  replace_var(CONTROL_VAR_VOLUME_DB, db_volume);
}

static int cmd_obtain_variable(struct action_event *event,
                               control_variable_t varnum,
                               const char *paramname) {
  const char *instance = upnp_get_string(event, "InstanceID");
  if (instance == NULL) {
    return -1;
  }
  Log_info("control", "%s: %s for instance %s\n", __FUNCTION__, paramname,
           instance);

  upnp_append_variable(event, varnum, paramname);
  return 0;
}

static int list_presets(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_PRESET_NAME_LIST,
                             "CurrentPresetNameList");
}

static int get_brightness(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_BRIGHTNESS,
                             "CurrentBrightness");
}

static int get_contrast(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_CONTRAST, "CurrentContrast");
}

static int get_sharpness(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_SHARPNESS, "CurrentSharpness");
}

static int get_red_videogain(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_R_GAIN, "CurrentRedVideoGain");
}

static int get_green_videogain(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_G_GAIN,
                             "CurrentGreenVideoGain");
}

static int get_blue_videogain(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_B_GAIN, "CurrentBlueVideoGain");
}

static int get_red_videoblacklevel(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_R_BLACK,
                             "CurrentRedVideoBlackLevel");
}

static int get_green_videoblacklevel(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_G_BLACK,
                             "CurrentGreenVideoBlackLevel");
}

static int get_blue_videoblacklevel(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_B_BLACK,
                             "CurrentBlueVideoBlackLevel");
}

static int get_colortemperature(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_COLOR_TEMP,
                             "CurrentColorTemperature");
}

static int get_horizontal_keystone(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_HOR_KEYSTONE,
                             "CurrentHorizontalKeystone");
}

static int get_vertical_keystone(struct action_event *event) {
  return cmd_obtain_variable(event, CONTROL_VAR_VER_KEYSTONE,
                             "CurrentVerticalKeystone");
}

static int get_mute(struct action_event *event) {
  /* FIXME - Channel */
  return cmd_obtain_variable(event, CONTROL_VAR_MUTE, "CurrentMute");
}

static void set_mute_toggle(int do_mute) {
  replace_var(CONTROL_VAR_MUTE, do_mute ? "1" : "0");
  Output::SetMute(do_mute);
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

static int get_volume(struct action_event *event) {
  /* FIXME - Channel */
  return cmd_obtain_variable(event, CONTROL_VAR_VOLUME, "CurrentVolume");
}

static float volume_level_to_decibel(int volume) {
  if (volume < volume_range.min) volume = volume_range.min;
  if (volume > volume_range.max) volume = volume_range.max;
  if (volume < volume_range.max / 2) {
    return vol_min_db + (vol_mid_db - vol_min_db) / vol_mid_point * volume;
  } else {
    const int range = volume_range.max - vol_mid_point;
    return vol_mid_db +
           ((vol_max_db - vol_mid_db) / range * (volume - vol_mid_point));
  }
}

static int volume_decibel_to_level(float decibel) {
  if (decibel < vol_min_db) return volume_range.min;
  if (decibel > vol_max_db) return volume_range.max;
  if (decibel < vol_mid_db) {
    return (decibel - vol_min_db) * vol_mid_point / (vol_mid_db - vol_min_db);
  } else {
    const int range = volume_range.max - vol_mid_point;
    return (decibel - vol_mid_db) * range / (vol_max_db - vol_mid_db) +
           vol_mid_point;
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
  snprintf(db_volume, sizeof(db_volume), "%d", (int)(256 * decibel));

  Log_info("control", "Setting volume-db to %.2fdb == #%d", decibel,
           volume_level);

  change_volume(volume, db_volume);
  return decibel;
}

static int set_volume_db(struct action_event *event) {
  const char *str_decibel_in = upnp_get_string(event, "DesiredVolume");
  service_lock();
  float raw_decibel_in = atof(str_decibel_in);
  float decibel = change_volume_decibel(raw_decibel_in);

  Output::SetVolume(exp(decibel / 20 * log(10)));
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
  snprintf(db_volume, sizeof(db_volume), "%d", (int)(256 * decibel));

  const double fraction = exp(decibel / 20 * log(10));

  change_volume(volume, db_volume);
  Output::SetVolume(fraction);
  set_mute_toggle(volume_level == 0);
  service_unlock();

  return 0;
}

static int get_volume_db(struct action_event *event) {
  /* FIXME - Channel */
  return cmd_obtain_variable(event, CONTROL_VAR_VOLUME_DB, "CurrentVolumeDB");
}

static int get_volume_dbrange(struct action_event *event) {
  // Ignoring instanceID and Channel
  char minval[16];
  snprintf(minval, sizeof(minval), "%lld", volume_db_range.min);
  upnp_add_response(event, "MinValue", minval);
  upnp_add_response(event, "MaxValue", "0");
  return 0;
}

static int get_loudness(struct action_event *event) {
  /* FIXME - Channel */
  return cmd_obtain_variable(event, CONTROL_VAR_LOUDNESS, "CurrentLoudness");
}

static struct action control_actions[] = {
    [CONTROL_CMD_GET_BLUE_BLACK] = {"GetBlueVideoBlackLevel",
                                    get_blue_videoblacklevel}, /* optional */
    [CONTROL_CMD_GET_BLUE_GAIN] = {"GetBlueVideoGain",
                                   get_blue_videogain}, /* optional */
    [CONTROL_CMD_GET_BRIGHTNESS] = {"GetBrightness",
                                    get_brightness}, /* optional */
    [CONTROL_CMD_GET_COLOR_TEMP] = {"GetColorTemperature",
                                    get_colortemperature},      /* optional */
    [CONTROL_CMD_GET_CONTRAST] = {"GetContrast", get_contrast}, /* optional */
    [CONTROL_CMD_GET_GREEN_BLACK] = {"GetGreenVideoBlackLevel",
                                     get_green_videoblacklevel}, /* optional */
    [CONTROL_CMD_GET_GREEN_GAIN] = {"GetGreenVideoGain",
                                    get_green_videogain}, /* optional */
    [CONTROL_CMD_GET_HOR_KEYSTONE] = {"GetHorizontalKeystone",
                                      get_horizontal_keystone}, /* optional */
    [CONTROL_CMD_GET_LOUDNESS] = {"GetLoudness", get_loudness}, /* optional */
    [CONTROL_CMD_GET_MUTE] = {"GetMute", get_mute},             /* optional */
    [CONTROL_CMD_GET_RED_BLACK] = {"GetRedVideoBlackLevel",
                                   get_red_videoblacklevel}, /* optional */
    [CONTROL_CMD_GET_RED_GAIN] = {"GetRedVideoGain",
                                  get_red_videogain}, /* optional */
    [CONTROL_CMD_GET_SHARPNESS] = {"GetSharpness",
                                   get_sharpness}, /* optional */
    [CONTROL_CMD_GET_VERT_KEYSTONE] = {"GetVerticalKeystone",
                                       get_vertical_keystone}, /* optional */
    [CONTROL_CMD_GET_VOL] = {"GetVolume", get_volume},         /* optional */
    [CONTROL_CMD_GET_VOL_DB] = {"GetVolumeDB", get_volume_db}, /* optional */
    [CONTROL_CMD_GET_VOL_DBRANGE] = {"GetVolumeDBRange",
                                     get_volume_dbrange}, /* optional */
    [CONTROL_CMD_LIST_PRESETS] = {"ListPresets", list_presets},
    [CONTROL_CMD_SET_MUTE] = {"SetMute", set_mute},            /* optional */
    [CONTROL_CMD_SET_VOL] = {"SetVolume", set_volume},         /* optional */
    [CONTROL_CMD_SET_VOL_DB] = {"SetVolumeDB", set_volume_db}, /* optional */

    //[CONTROL_CMD_SELECT_PRESET] =       	{"SelectPreset", NULL},
    //[CONTROL_CMD_SET_BRIGHTNESS] =      	{"SetBrightness", NULL}, /* optional
    //*/ [CONTROL_CMD_SET_CONTRAST] =        	{"SetContrast", NULL}, /*
    //optional */ [CONTROL_CMD_SET_SHARPNESS] =       	{"SetSharpness", NULL},
    ///* optional */
    //[CONTROL_CMD_SET_RED_GAIN] =        	{"SetRedVideoGain", NULL}, /*
    //optional */ [CONTROL_CMD_SET_GREEN_GAIN] =
    //{"SetGreenVideoGain", NULL}, /* optional */ [CONTROL_CMD_SET_BLUE_GAIN] =
    //{"SetBlueVideoGain", NULL}, /* optional */ [CONTROL_CMD_SET_RED_BLACK] =
    //{"SetRedVideoBlackLevel", NULL}, /* optional */
    //[CONTROL_CMD_SET_GREEN_BLACK] =     	{"SetGreenVideoBlackLevel", NULL},
    ///* optional */ [CONTROL_CMD_SET_BLUE_BLACK] =
    //{"SetBlueVideoBlackLevel", NULL}, /* optional */
    //[CONTROL_CMD_SET_COLOR_TEMP] =      	{"SetColorTemperature", NULL}, /*
    //optional */ [CONTROL_CMD_SET_HOR_KEYSTONE] = {"SetHorizontalKeystone",
    //NULL}, /* optional */ [CONTROL_CMD_SET_VERT_KEYSTONE] =
    //{"SetVerticalKeystone", NULL}, /* optional */ [CONTROL_CMD_SET_LOUDNESS] =
    //{"SetLoudness", NULL}, /* optional */
    [CONTROL_CMD_COUNT] = {NULL, NULL}};

struct service *upnp_control_get_service(void) {
  static struct service control_service_ = {
      .service_mutex = &control_mutex,
      .service_id = CONTROL_SERVICE_ID,
      .service_type = CONTROL_TYPE,
      .scpd_url = CONTROL_SCPD_URL,
      .control_url = CONTROL_CONTROL_URL,
      .event_url = CONTROL_EVENT_URL,
      .event_xml_ns = CONTROL_EVENT_XML_NS,
      .actions = control_actions,
      .action_arguments = argument_list,
      .variable_container = NULL,  // set later.
      .last_change = NULL,
      .command_count = CONTROL_CMD_COUNT,
  };

  static struct var_meta control_var_meta[] = {
      {CONTROL_VAR_LAST_CHANGE, "LastChange",
       "<Event xmlns = \"urn:schemas-upnp-org:metadata-1-0/RCS/\"/>", Eventing::kYes,
       DataType::kString, NULL, NULL},
      {CONTROL_VAR_PRESET_NAME_LIST, "PresetNameList", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {CONTROL_VAR_AAT_CHANNEL, "A_ARG_TYPE_Channel", "", Eventing::kNo,
       DataType::kString, aat_channels, NULL},
      {CONTROL_VAR_AAT_INSTANCE_ID, "A_ARG_TYPE_InstanceID", "0", Eventing::kNo,
       DataType::kUint4, NULL, NULL},
      {CONTROL_VAR_AAT_PRESET_NAME, "A_ARG_TYPE_PresetName", "", Eventing::kNo,
       DataType::kString, aat_presetnames, NULL},
      {CONTROL_VAR_BRIGHTNESS, "Brightness", "0", Eventing::kNo, DataType::kUint2, NULL,
       &brightness_range},
      {CONTROL_VAR_CONTRAST, "Contrast", "0", Eventing::kNo, DataType::kUint2, NULL,
       &contrast_range},
      {CONTROL_VAR_SHARPNESS, "Sharpness", "0", Eventing::kNo, DataType::kUint2, NULL,
       &sharpness_range},
      {CONTROL_VAR_R_GAIN, "RedVideoGain", "0", Eventing::kNo, DataType::kUint2, NULL,
       &vid_gain_range},
      {CONTROL_VAR_G_GAIN, "GreenVideoGain", "0", Eventing::kNo, DataType::kUint2, NULL,
       &vid_gain_range},
      {CONTROL_VAR_B_GAIN, "BlueVideoGain", "0", Eventing::kNo, DataType::kUint2, NULL,
       &vid_gain_range},
      {CONTROL_VAR_R_BLACK, "RedVideoBlackLevel", "0", Eventing::kNo, DataType::kUint2,
       NULL, &vid_black_range},
      {CONTROL_VAR_G_BLACK, "GreenVideoBlackLevel", "0", Eventing::kNo, DataType::kUint2,
       NULL, &vid_black_range},
      {CONTROL_VAR_B_BLACK, "BlueVideoBlackLevel", "0", Eventing::kNo, DataType::kUint2,
       NULL, &vid_black_range},
      {CONTROL_VAR_COLOR_TEMP, "ColorTemperature", "0", Eventing::kNo, DataType::kUint2,
       NULL, &colortemp_range},
      {CONTROL_VAR_HOR_KEYSTONE, "HorizontalKeystone", "0", Eventing::kNo, DataType::kInt2,
       NULL, &keystone_range},
      {CONTROL_VAR_VER_KEYSTONE, "VerticalKeystone", "0", Eventing::kNo, DataType::kInt2,
       NULL, &keystone_range},
      {CONTROL_VAR_MUTE, "Mute", "0", Eventing::kNo, DataType::kBoolean, NULL, NULL},
      {CONTROL_VAR_VOLUME, "Volume", "0", Eventing::kNo, DataType::kUint2, NULL,
       &volume_range},
      {CONTROL_VAR_VOLUME_DB, "VolumeDB", "0", Eventing::kNo, DataType::kInt2, NULL,
       &volume_db_range},
      {CONTROL_VAR_LOUDNESS, "Loudness", "0", Eventing::kNo, DataType::kBoolean, NULL,
       NULL},

      {CONTROL_VAR_COUNT, NULL, NULL, Eventing::kNo, DataType::kUnknown, NULL, NULL}};

  if (control_service_.variable_container == NULL) {
    state_variables_ = new VariableContainer(CONTROL_VAR_COUNT,
                                             control_var_meta);
    control_service_.variable_container = state_variables_;
  }

  return &control_service_;
}

void upnp_control_init(struct upnp_device *device) {
  struct service *service = upnp_control_get_service();

  // Set initial volume.
  float volume_fraction = 0;
  if (Output::GetVolume(volume_fraction) == 0) {
    Log_info("control",
             "Output initial volume is %f; setting "
             "control variables accordingly.",
             volume_fraction);
    change_volume_decibel(20 * log(volume_fraction) / log(10));
  }

  assert(service->last_change == NULL);
  service->last_change = new UPnPLastChangeCollector(
    service->variable_container, CONTROL_EVENT_XML_NS, device,
    CONTROL_SERVICE_ID);
  // According to UPnP-av-RenderingControl-v3-Service-20101231.pdf, 2.3.1
  // page 51, the A_ARG_TYPE* variables are not evented.
  service->last_change->AddIgnore(CONTROL_VAR_AAT_CHANNEL);
  service->last_change->AddIgnore(CONTROL_VAR_AAT_INSTANCE_ID);
  service->last_change->AddIgnore(CONTROL_VAR_AAT_PRESET_NAME);
}

void upnp_control_register_variable_listener(
  const VariableContainer::ChangeListener &listener) {
  state_variables_->RegisterCallback(listener);
}
