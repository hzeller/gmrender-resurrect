// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* upnp_transport.c - UPnP AVTransport routines
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
#include "upnp_transport.h"

#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ithread.h>
#include <upnp.h>

#include "output.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"
#include "xmlescape.h"

#define TRANSPORT_TYPE "urn:schemas-upnp-org:service:AVTransport:1"
#define TRANSPORT_SERVICE_ID "urn:upnp-org:serviceId:AVTransport"

#define TRANSPORT_SCPD_URL "/upnp/rendertransportSCPD.xml"
#define TRANSPORT_CONTROL_URL "/upnp/control/rendertransport1"
#define TRANSPORT_EVENT_URL "/upnp/event/rendertransport1"

// Namespace, see UPnP-av-AVTransport-v3-Service-20101231.pdf page 15
#define TRANSPORT_EVENT_XML_NS "urn:schemas-upnp-org:metadata-1-0/AVT/"

enum {
  TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS,
  TRANSPORT_CMD_GETDEVICECAPABILITIES,
  TRANSPORT_CMD_GETMEDIAINFO,
  TRANSPORT_CMD_GETPOSITIONINFO,
  TRANSPORT_CMD_GETTRANSPORTINFO,
  TRANSPORT_CMD_GETTRANSPORTSETTINGS,
  TRANSPORT_CMD_PAUSE,
  TRANSPORT_CMD_PLAY,
  TRANSPORT_CMD_SEEK,
  TRANSPORT_CMD_SETAVTRANSPORTURI,
  TRANSPORT_CMD_STOP,
  TRANSPORT_CMD_SETNEXTAVTRANSPORTURI,

  // Not implemented
  // TRANSPORT_CMD_NEXT,
  // TRANSPORT_CMD_PREVIOUS,
  // TRANSPORT_CMD_SETPLAYMODE,
  // TRANSPORT_CMD_RECORD,
  // TRANSPORT_CMD_SETRECORDQUALITYMODE,

  TRANSPORT_CMD_COUNT
};

enum UPNPTransportError {
  UPNP_TRANSPORT_E_TRANSITION_NA = 701,
  UPNP_TRANSPORT_E_NO_CONTENTS = 702,
  UPNP_TRANSPORT_E_READ_ERROR = 703,
  UPNP_TRANSPORT_E_PLAY_FORMAT_NS = 704,
  UPNP_TRANSPORT_E_TRANSPORT_LOCKED = 705,
  UPNP_TRANSPORT_E_WRITE_ERROR = 706,
  UPNP_TRANSPORT_E_REC_MEDIA_WP = 707,
  UPNP_TRANSPORT_E_REC_FORMAT_NS = 708,
  UPNP_TRANSPORT_E_REC_MEDIA_FULL = 709,
  UPNP_TRANSPORT_E_SEEKMODE_NS = 710,
  UPNP_TRANSPORT_E_ILL_SEEKTARGET = 711,
  UPNP_TRANSPORT_E_PLAYMODE_NS = 712,
  UPNP_TRANSPORT_E_RECQUAL_NS = 713,
  UPNP_TRANSPORT_E_ILLEGAL_MIME = 714,
  UPNP_TRANSPORT_E_CONTENT_BUSY = 715,
  UPNP_TRANSPORT_E_RES_NOT_FOUND = 716,
  UPNP_TRANSPORT_E_PLAYSPEED_NS = 717,
  UPNP_TRANSPORT_E_INVALID_IID = 718,
};

static const char kZeroTime[] = "0:00:00";

enum class TransportState {
  STOPPED,
  PLAYING,
  TRANSITIONING,    /* optional */
  PAUSED_PLAYBACK,  /* optional */
  PAUSED_RECORDING, /* optional */
  RECORDING,        /* optional */
  NO_MEDIA_PRESENT  /* optional */
};

static const char *transport_state_xml_name[] = {
  "STOPPED",
  "PLAYING",
  "TRANSITIONING",
  "PAUSED_PLAYBACK",
  "PAUSED_RECORDING",
  "RECORDING",
  "NO_MEDIA_PRESENT",
  NULL
};

static const char *TransportStateName(TransportState s) {
  return transport_state_xml_name[static_cast<int>(s)];
}

static const char *transport_stati[] = {"OK", "ERROR_OCCURRED",
                                        " vendor-defined ", NULL};
static const char *media[] = {"UNKNOWN",
                              "DV",
                              "MINI-DV",
                              "VHS",
                              "W-VHS",
                              "S-VHS",
                              "D-VHS",
                              "VHSC",
                              "VIDEO8",
                              "HI8",
                              "CD-ROM",
                              "CD-DA",
                              "CD-R",
                              "CD-RW",
                              "VIDEO-CD",
                              "SACD",
                              "MD-AUDIO",
                              "MD-PICTURE",
                              "DVD-ROM",
                              "DVD-VIDEO",
                              "DVD-R",
                              "DVD+RW",
                              "DVD-RW",
                              "DVD-RAM",
                              "DVD-AUDIO",
                              "DAT",
                              "LD",
                              "HDD",
                              "MICRO-MV",
                              "NETWORK",
                              "NONE",
                              "NOT_IMPLEMENTED",
                              " vendor-defined ",
                              NULL};

static const char *playmodi[] = {"NORMAL",
                                 //"SHUFFLE",
                                 //"REPEAT_ONE",
                                 "REPEAT_ALL",
                                 //"RANDOM",
                                 //"DIRECT_1",
                                 "INTRO", NULL};

static const char *playspeeds[] = {"1", " vendor-defined ", NULL};

static const char *rec_write_stati[] = {"WRITABLE",        "PROTECTED",
                                        "NOT_WRITABLE",    "UNKNOWN",
                                        "NOT_IMPLEMENTED", NULL};

static const char *rec_quality_modi[] = {"0:EP",
                                         "1:LP",
                                         "2:SP",
                                         "0:BASIC",
                                         "1:MEDIUM",
                                         "2:HIGH",
                                         "NOT_IMPLEMENTED",
                                         " vendor-defined ",
                                         NULL};

static const char *aat_seekmodi[] = {"ABS_TIME",   "REL_TIME", "ABS_COUNT",
                                     "REL_COUNT",  "TRACK_NR", "CHANNEL_FREQ",
                                     "TAPE-INDEX", "FRAME",    NULL};

static struct param_range track_range = {0, 4294967295LL, 1};

static struct param_range track_nr_range = {0, 4294967295LL, 0};

typedef enum {
  TRANSPORT_VAR_TRANSPORT_STATUS,
  TRANSPORT_VAR_NEXT_AV_URI,
  TRANSPORT_VAR_NEXT_AV_URI_META,
  TRANSPORT_VAR_CUR_TRACK_META,
  TRANSPORT_VAR_REL_CTR_POS,
  TRANSPORT_VAR_AAT_INSTANCE_ID,
  TRANSPORT_VAR_AAT_SEEK_TARGET,
  TRANSPORT_VAR_PLAY_MEDIUM,
  TRANSPORT_VAR_REL_TIME_POS,
  TRANSPORT_VAR_REC_MEDIA,
  TRANSPORT_VAR_CUR_PLAY_MODE,
  TRANSPORT_VAR_TRANSPORT_PLAY_SPEED,
  TRANSPORT_VAR_PLAY_MEDIA,
  TRANSPORT_VAR_ABS_TIME_POS,
  TRANSPORT_VAR_CUR_TRACK,
  TRANSPORT_VAR_CUR_TRACK_URI,
  TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS,
  TRANSPORT_VAR_NR_TRACKS,
  TRANSPORT_VAR_AV_URI,
  TRANSPORT_VAR_ABS_CTR_POS,
  TRANSPORT_VAR_CUR_REC_QUAL_MODE,
  TRANSPORT_VAR_CUR_MEDIA_DUR,
  TRANSPORT_VAR_AAT_SEEK_MODE,
  TRANSPORT_VAR_AV_URI_META,
  TRANSPORT_VAR_REC_MEDIUM,
  TRANSPORT_VAR_REC_MEDIUM_WR_STATUS,
  TRANSPORT_VAR_LAST_CHANGE,
  TRANSPORT_VAR_CUR_TRACK_DUR,
  TRANSPORT_VAR_TRANSPORT_STATE,
  TRANSPORT_VAR_POS_REC_QUAL_MODE,
  TRANSPORT_VAR_COUNT
} transport_variable_t;

static struct argument arguments_setavtransporturi[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"CurrentURI", ParamDir::kIn, TRANSPORT_VAR_AV_URI},
    {"CurrentURIMetaData", ParamDir::kIn, TRANSPORT_VAR_AV_URI_META},
    {NULL}};

static struct argument arguments_setnextavtransporturi[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"NextURI", ParamDir::kIn, TRANSPORT_VAR_NEXT_AV_URI},
    {"NextURIMetaData", ParamDir::kIn, TRANSPORT_VAR_NEXT_AV_URI_META},
    {NULL}};

static struct argument arguments_getmediainfo[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"NrTracks", ParamDir::kOut, TRANSPORT_VAR_NR_TRACKS},
    {"MediaDuration", ParamDir::kOut, TRANSPORT_VAR_CUR_MEDIA_DUR},
    {"CurrentURI", ParamDir::kOut, TRANSPORT_VAR_AV_URI},
    {"CurrentURIMetaData", ParamDir::kOut, TRANSPORT_VAR_AV_URI_META},
    {"NextURI", ParamDir::kOut, TRANSPORT_VAR_NEXT_AV_URI},
    {"NextURIMetaData", ParamDir::kOut, TRANSPORT_VAR_NEXT_AV_URI_META},
    {"PlayMedium", ParamDir::kOut, TRANSPORT_VAR_PLAY_MEDIUM},
    {"RecordMedium", ParamDir::kOut, TRANSPORT_VAR_REC_MEDIUM},
    {"WriteStatus", ParamDir::kOut, TRANSPORT_VAR_REC_MEDIUM_WR_STATUS},
    {NULL}};

static struct argument arguments_gettransportinfo[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"CurrentTransportState", ParamDir::kOut, TRANSPORT_VAR_TRANSPORT_STATE},
    {"CurrentTransportStatus", ParamDir::kOut, TRANSPORT_VAR_TRANSPORT_STATUS},
    {"CurrentSpeed", ParamDir::kOut, TRANSPORT_VAR_TRANSPORT_PLAY_SPEED},
    {NULL}};

static struct argument arguments_getpositioninfo[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"Track", ParamDir::kOut, TRANSPORT_VAR_CUR_TRACK},
    {"TrackDuration", ParamDir::kOut, TRANSPORT_VAR_CUR_TRACK_DUR},
    {"TrackMetaData", ParamDir::kOut, TRANSPORT_VAR_CUR_TRACK_META},
    {"TrackURI", ParamDir::kOut, TRANSPORT_VAR_CUR_TRACK_URI},
    {"RelTime", ParamDir::kOut, TRANSPORT_VAR_REL_TIME_POS},
    {"AbsTime", ParamDir::kOut, TRANSPORT_VAR_ABS_TIME_POS},
    {"RelCount", ParamDir::kOut, TRANSPORT_VAR_REL_CTR_POS},
    {"AbsCount", ParamDir::kOut, TRANSPORT_VAR_ABS_CTR_POS},
    {NULL}};

static struct argument arguments_getdevicecapabilities[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"PlayMedia", ParamDir::kOut, TRANSPORT_VAR_PLAY_MEDIA},
    {"RecMedia", ParamDir::kOut, TRANSPORT_VAR_REC_MEDIA},
    {"RecQualityModes", ParamDir::kOut, TRANSPORT_VAR_POS_REC_QUAL_MODE},
    {NULL}};

static struct argument arguments_gettransportsettings[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"PlayMode", ParamDir::kOut, TRANSPORT_VAR_CUR_PLAY_MODE},
    {"RecQualityMode", ParamDir::kOut, TRANSPORT_VAR_CUR_REC_QUAL_MODE},
    {NULL}};

static struct argument arguments_stop[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID}, {NULL}};
static struct argument arguments_play[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"Speed", ParamDir::kIn, TRANSPORT_VAR_TRANSPORT_PLAY_SPEED},
    {NULL}};
static struct argument arguments_pause[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID}, {NULL}};
// static struct argument arguments_record[] = {
//        { "InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	{ NULL }
//};

static struct argument arguments_seek[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"Unit", ParamDir::kIn, TRANSPORT_VAR_AAT_SEEK_MODE},
    {"Target", ParamDir::kIn, TRANSPORT_VAR_AAT_SEEK_TARGET},
    {NULL}};
// static struct argument arguments_next[] = {
//        { "InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	{ NULL }
//};
// static struct argument arguments_previous[] = {
//        { "InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	{ NULL }
//};
// static struct argument arguments_setplaymode[] = {
//        { "InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID },
//        { "NewPlayMode", ParamDir::kIn, TRANSPORT_VAR_CUR_PLAY_MODE },
//	{ NULL }
//};
// static struct argument arguments_setrecordqualitymode[] = {
//        { "InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID },
//        { "NewRecordQualityMode", ParamDir::kIn,
//        TRANSPORT_VAR_CUR_REC_QUAL_MODE },
//	{ NULL }
//};
static struct argument arguments_getcurrenttransportactions[] = {
    {"InstanceID", ParamDir::kIn, TRANSPORT_VAR_AAT_INSTANCE_ID},
    {"Actions", ParamDir::kOut, TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS},
    {NULL}};

static struct argument *argument_list[] = {
    [TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS] =
        arguments_getcurrenttransportactions,
    [TRANSPORT_CMD_GETDEVICECAPABILITIES] = arguments_getdevicecapabilities,
    [TRANSPORT_CMD_GETMEDIAINFO] = arguments_getmediainfo,
    [TRANSPORT_CMD_GETPOSITIONINFO] = arguments_getpositioninfo,
    [TRANSPORT_CMD_GETTRANSPORTINFO] = arguments_gettransportinfo,
    [TRANSPORT_CMD_GETTRANSPORTSETTINGS] = arguments_gettransportsettings,
    [TRANSPORT_CMD_PAUSE] = arguments_pause,
    [TRANSPORT_CMD_PLAY] = arguments_play,
    [TRANSPORT_CMD_SEEK] = arguments_seek,
    [TRANSPORT_CMD_SETAVTRANSPORTURI] = arguments_setavtransporturi,
    [TRANSPORT_CMD_STOP] = arguments_stop,

    [TRANSPORT_CMD_SETNEXTAVTRANSPORTURI] = arguments_setnextavtransporturi,

    //[TRANSPORT_CMD_RECORD] =                    arguments_record,
    //[TRANSPORT_CMD_NEXT] =                      arguments_next,
    //[TRANSPORT_CMD_PREVIOUS] =                  arguments_previous,
    //[TRANSPORT_CMD_SETPLAYMODE] =               arguments_setplaymode,
    //[TRANSPORT_CMD_SETRECORDQUALITYMODE] = arguments_setrecordqualitymode,
    [TRANSPORT_CMD_COUNT] = NULL};

// Our 'instance' variables.
static TransportState transport_state_ = TransportState::STOPPED;
static VariableContainer *state_variables_ = NULL;

/* protects transport_values, and service-specific state */

static ithread_mutex_t transport_mutex;

static void service_lock(void) {
  ithread_mutex_lock(&transport_mutex);

  auto collector = upnp_transport_get_service()->last_change;
  if (collector) collector->Start();
}

static void service_unlock(void) {
  auto collector = upnp_transport_get_service()->last_change;
  if (collector) collector->Finish();
  ithread_mutex_unlock(&transport_mutex);
}

static char has_instance_id(struct action_event *event) {
  const char *const value = upnp_get_string(event, "InstanceID");
  if (value == NULL) {
    upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS, "Missing InstanceID");
  }
  return value != NULL;
}

static int get_media_info(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  upnp_append_variable(event, TRANSPORT_VAR_NR_TRACKS, "NrTracks");
  upnp_append_variable(event, TRANSPORT_VAR_CUR_MEDIA_DUR, "MediaDuration");
  upnp_append_variable(event, TRANSPORT_VAR_AV_URI, "CurrentURI");
  upnp_append_variable(event, TRANSPORT_VAR_AV_URI_META, "CurrentURIMetaData");
  upnp_append_variable(event, TRANSPORT_VAR_NEXT_AV_URI, "NextURI");
  upnp_append_variable(event, TRANSPORT_VAR_NEXT_AV_URI_META,
                       "NextURIMetaData");
  upnp_append_variable(event, TRANSPORT_VAR_REC_MEDIA, "PlayMedium");
  upnp_append_variable(event, TRANSPORT_VAR_REC_MEDIUM, "RecordMedium");
  upnp_append_variable(event, TRANSPORT_VAR_REC_MEDIUM_WR_STATUS,
                       "WriteStatus");
  return 0;
}

// Transport uri always comes in uri/meta pairs. Set these and also the related
// track uri/meta variables.
// Returns 1, if this meta-data likely needs to be updated while the stream
// is playing (e.g. radio broadcast).
static int replace_transport_uri_and_meta(const char *uri, const char *meta) {
  state_variables_->Set(TRANSPORT_VAR_AV_URI, uri);
  state_variables_->Set(TRANSPORT_VAR_AV_URI_META, meta);

  // This influences as well the tracks. If there is a non-empty URI,
  // we have exactly one track.
  const char *tracks = (uri != NULL && strlen(uri) > 0) ? "1" : "0";
  state_variables_->Set(TRANSPORT_VAR_NR_TRACKS, tracks);

  // We only really want to send back meta data if we didn't get anything
  // useful or if this is an audio item.
  const int requires_stream_meta_callback =
      (strlen(meta) == 0) || strstr(meta, "object.item.audioItem");
  return requires_stream_meta_callback;
}

// Similar to replace_transport_uri_and_meta() above, but current values.
static void replace_current_uri_and_meta(const char *uri, const char *meta) {
  const char *tracks = (uri != NULL && strlen(uri) > 0) ? "1" : "0";
  state_variables_->Set(TRANSPORT_VAR_CUR_TRACK, tracks);
  state_variables_->Set(TRANSPORT_VAR_CUR_TRACK_URI, uri);
  state_variables_->Set(TRANSPORT_VAR_CUR_TRACK_META, meta);
}

static void change_transport_state(TransportState new_state) {
  transport_state_ = new_state;
  if (!state_variables_->Set(TRANSPORT_VAR_TRANSPORT_STATE,
                             TransportStateName(new_state))) {
    return;  // no change.
  }
  const char *available_actions = NULL;
  switch (new_state) {
    case TransportState::STOPPED:
      if (state_variables_->Get(TRANSPORT_VAR_AV_URI).empty()) {
        available_actions = "PLAY";
      } else {
        available_actions = "PLAY,SEEK";
      }
      break;
    case TransportState::PLAYING:
      available_actions = "PAUSE,STOP,SEEK";
      break;
    case TransportState::PAUSED_PLAYBACK:
      available_actions = "PLAY,STOP,SEEK";
      break;
    case TransportState::TRANSITIONING:
    case TransportState::PAUSED_RECORDING:
    case TransportState::RECORDING:
    case TransportState::NO_MEDIA_PRESENT:
      // We should not switch to this state.
      break;
  }
  if (available_actions) {
    state_variables_->Set(TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS, available_actions);
  }
}

// Callback from our output if the song meta data changed.
static void update_meta_from_stream(const TrackMetadata &meta) {
  if (meta.title().empty()) return;

  auto original_xml = state_variables_->Get(TRANSPORT_VAR_AV_URI_META);
  const std::string didl = meta.ToDIDL(original_xml);
  service_lock();
  state_variables_->Set(TRANSPORT_VAR_AV_URI_META, didl);
  state_variables_->Set(TRANSPORT_VAR_CUR_TRACK_META, didl);
  service_unlock();
}

/* UPnP action handlers */

static int set_avtransport_uri(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }
  const char *uri = upnp_get_string(event, "CurrentURI");
  if (uri == NULL) {
    return -1;
  }

  service_lock();
  const char *meta = upnp_get_string(event, "CurrentURIMetaData");
  // Transport URI/Meta set now, current URI/Meta when it starts playing.
  replace_transport_uri_and_meta(uri, meta);

  if (transport_state_ == TransportState::PLAYING) {
    // Uh, wrong state.
    // Usually, this should not be called while we are PLAYING, only
    // STOPPED or PAUSED. But if actually some controller sets this
    // while playing, probably the best is to update the current
    // current URI/Meta as well to reflect the state best.
    replace_current_uri_and_meta(uri, meta);
  }

  Output::SetUri(uri);
  service_unlock();

  return 0;
}

static int set_next_avtransport_uri(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  const char *next_uri = upnp_get_string(event, "NextURI");
  if (next_uri == NULL) {
    return -1;
  }

  int rc = 0;
  service_lock();

  Output::SetNextUri(next_uri);
  state_variables_->Set(TRANSPORT_VAR_NEXT_AV_URI, next_uri);

  const char *next_uri_meta = upnp_get_string(event, "NextURIMetaData");
  if (next_uri_meta == NULL) {
    rc = -1;
  } else {
    state_variables_->Set(TRANSPORT_VAR_NEXT_AV_URI_META, next_uri_meta);
  }

  service_unlock();

  return rc;
}

static int get_transport_info(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  upnp_append_variable(event, TRANSPORT_VAR_TRANSPORT_STATE,
                       "CurrentTransportState");
  upnp_append_variable(event, TRANSPORT_VAR_TRANSPORT_STATUS,
                       "CurrentTransportStatus");
  upnp_append_variable(event, TRANSPORT_VAR_TRANSPORT_PLAY_SPEED,
                       "CurrentSpeed");
  return 0;
}

static int get_current_transportactions(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  upnp_append_variable(event, TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS, "Actions");
  return 0;
}

static int get_transport_settings(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }
  // TODO: what variables to add ?
  return 0;
}

// Print UPnP formatted time into given buffer. time given in nanoseconds.
static int divide_leave_remainder(gint64 *val, gint64 divisor) {
  int result = *val / divisor;
  *val %= divisor;
  return result;
}
static void print_upnp_time(char *result, size_t size, gint64 t) {
  const gint64 one_sec = 1000000000LL;  // units are in nanoseconds.
  const int hour = divide_leave_remainder(&t, 3600LL * one_sec);
  const int minute = divide_leave_remainder(&t, 60LL * one_sec);
  const int second = divide_leave_remainder(&t, one_sec);
  snprintf(result, size, "%d:%02d:%02d", hour, minute, second);
}

static gint64 parse_upnp_time(const char *time_string) {
  int hour = 0;
  int minute = 0;
  int second = 0;
  sscanf(time_string, "%d:%02d:%02d", &hour, &minute, &second);
  const gint64 seconds = (hour * 3600 + minute * 60 + second);
  const gint64 one_sec_unit = 1000000000LL;
  return one_sec_unit * seconds;
}

// We constantly update the track time to event about it to our clients.
static void *thread_update_track_time(void *userdata) {
  (void)userdata;
  const gint64 one_sec_unit = 1000000000LL;
  char tbuf[32];
  gint64 last_duration = -1, last_position = -1;
  for (;;) {
    usleep(500000);  // 500ms
    service_lock();
    gint64 duration, position;
    const int pos_result = Output::GetPosition(&duration, &position);
    if (pos_result == 0) {
      if (duration != last_duration) {
        print_upnp_time(tbuf, sizeof(tbuf), duration);
        state_variables_->Set(TRANSPORT_VAR_CUR_TRACK_DUR, tbuf);
        last_duration = duration;
      }
      if (position / one_sec_unit != last_position) {
        print_upnp_time(tbuf, sizeof(tbuf), position);
        state_variables_->Set(TRANSPORT_VAR_REL_TIME_POS, tbuf);
        last_position = position / one_sec_unit;
      }
    }
    service_unlock();
  }
  return NULL;  // not reached.
}

static int get_position_info(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK, "Track");
  upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_DUR, "TrackDuration");
  upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_META, "TrackMetaData");
  upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_URI, "TrackURI");
  upnp_append_variable(event, TRANSPORT_VAR_REL_TIME_POS, "RelTime");
  upnp_append_variable(event, TRANSPORT_VAR_ABS_TIME_POS, "AbsTime");
  upnp_append_variable(event, TRANSPORT_VAR_REL_CTR_POS, "RelCount");
  upnp_append_variable(event, TRANSPORT_VAR_ABS_CTR_POS, "AbsCount");

  return 0;
}

static int get_device_caps(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }
  // TODO: implement ?
  return 0;
}

static int stop(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  service_lock();
  switch (transport_state_) {
    case TransportState::STOPPED:
      // nothing to change.
      break;
    case TransportState::PLAYING:
    case TransportState::TRANSITIONING:
    case TransportState::PAUSED_RECORDING:
    case TransportState::RECORDING:
    case TransportState::PAUSED_PLAYBACK:
      Output::Stop();
      change_transport_state(TransportState::STOPPED);
      break;

    case TransportState::NO_MEDIA_PRESENT:
      /* action not allowed in these states - error 701 */
      upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
                     "Transition to STOP not allowed; allowed=%s",
                     state_variables_
                     ->Get(TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS).c_str());

      break;
  }
  service_unlock();

  return 0;
}

static void inform_play_transition_from_output(Output::State state) {
  service_lock();
  switch (state) {
    case Output::State::kPlaybackStopped:
      replace_transport_uri_and_meta("", "");
      replace_current_uri_and_meta("", "");
      change_transport_state(TransportState::STOPPED);
      break;

    case Output::State::kStartedNextStream: {
      auto av_uri = state_variables_->Get(TRANSPORT_VAR_NEXT_AV_URI);
      auto av_meta = state_variables_->Get(TRANSPORT_VAR_NEXT_AV_URI_META);
      replace_transport_uri_and_meta(av_uri.c_str(), av_meta.c_str());
      replace_current_uri_and_meta(av_uri.c_str(), av_meta.c_str());
      state_variables_->Set(TRANSPORT_VAR_NEXT_AV_URI, "");
      state_variables_->Set(TRANSPORT_VAR_NEXT_AV_URI_META, "");
      break;
    }
  }
  service_unlock();
}

Output::PlaybackCallback upnp_transport_get_transition_callback(void) {
  return &inform_play_transition_from_output;
}

Output::MetadataCallback upnp_transport_get_metadata_callback(void) {
  return &update_meta_from_stream;
}

static int play(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  int rc = 0;
  service_lock();
  switch (transport_state_) {
    case TransportState::PLAYING:
      // Nothing to change.
      break;

    case TransportState::STOPPED:
      // If we were stopped before, we start a new song now. So just
      // set the time to zero now; otherwise we will see the old
      // value of the previous song until it updates some fractions
      // of a second later.
      state_variables_->Set(TRANSPORT_VAR_REL_TIME_POS, kZeroTime);

      /* >>> fall through */

    case TransportState::PAUSED_PLAYBACK:
      if (Output::Play()) {
        upnp_set_error(event, 704, "Playing failed");
        rc = -1;
      } else {
        change_transport_state(TransportState::PLAYING);
        auto av_uri = state_variables_->Get(TRANSPORT_VAR_AV_URI);
        auto av_meta = state_variables_->Get(TRANSPORT_VAR_AV_URI_META);
        replace_current_uri_and_meta(av_uri.c_str(), av_meta.c_str());
      }
      break;

    case TransportState::NO_MEDIA_PRESENT:
    case TransportState::TRANSITIONING:
    case TransportState::PAUSED_RECORDING:
    case TransportState::RECORDING:
      /* action not allowed in these states - error 701 */
      upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
                     "Transition to PLAY not allowed; allowed=%s",
                     state_variables_
                     ->Get(TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS).c_str());
      rc = -1;
      break;
  }
  service_unlock();

  return rc;
}

static int pause_stream(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  int rc = 0;
  service_lock();
  switch (transport_state_) {
    case TransportState::PAUSED_PLAYBACK:
      // Nothing to change.
      break;

    case TransportState::PLAYING:
      if (Output::Pause()) {
        upnp_set_error(event, 704, "Pause failed");
        rc = -1;
      } else {
        change_transport_state(TransportState::PAUSED_PLAYBACK);
      }
      break;

    default:
      /* action not allowed in these states - error 701 */
      upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
                     "Transition to PAUSE not allowed; allowed=%s",
                     state_variables_
                     ->Get(TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS).c_str());
      rc = -1;
  }
  service_unlock();

  return rc;
}

static int seek(struct action_event *event) {
  if (!has_instance_id(event)) {
    return -1;
  }

  const char *unit = upnp_get_string(event, "Unit");
  if (strcmp(unit, "REL_TIME") == 0) {
    // This is the only thing we support right now.
    const char *target = upnp_get_string(event, "Target");
    gint64 nanos = parse_upnp_time(target);
    service_lock();
    if (Output::Seek(nanos) == 0) {
      // TODO(hzeller): Seeking might take some time,
      // pretend to already be there. Should we go into
      // TRANSITION mode ?
      // (gstreamer will go into PAUSE, then PLAYING)
      state_variables_->Set(TRANSPORT_VAR_REL_TIME_POS, target);
    }
    service_unlock();
  }

  return 0;
}

static struct action transport_actions[] = {
    [TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS] = {"GetCurrentTransportActions",
                                                  get_current_transportactions},
    [TRANSPORT_CMD_GETDEVICECAPABILITIES] = {"GetDeviceCapabilities",
                                             get_device_caps},
    [TRANSPORT_CMD_GETMEDIAINFO] = {"GetMediaInfo", get_media_info},
    [TRANSPORT_CMD_GETPOSITIONINFO] = {"GetPositionInfo", get_position_info},
    [TRANSPORT_CMD_GETTRANSPORTINFO] = {"GetTransportInfo", get_transport_info},
    [TRANSPORT_CMD_GETTRANSPORTSETTINGS] = {"GetTransportSettings",
                                            get_transport_settings},
    [TRANSPORT_CMD_PAUSE] = {"Pause", pause_stream},
    [TRANSPORT_CMD_PLAY] = {"Play", play},
    [TRANSPORT_CMD_SEEK] = {"Seek", seek},
    [TRANSPORT_CMD_SETAVTRANSPORTURI] = {"SetAVTransportURI",
                                         set_avtransport_uri}, /* RC9800i */
    [TRANSPORT_CMD_STOP] = {"Stop", stop},
    [TRANSPORT_CMD_SETNEXTAVTRANSPORTURI] = {"SetNextAVTransportURI",
                                             set_next_avtransport_uri},

    //[TRANSPORT_CMD_RECORD] =                    {"Record", NULL},	/* optional
    //*/ [TRANSPORT_CMD_NEXT] =                      {"Next", next},
    //[TRANSPORT_CMD_PREVIOUS] =                  {"Previous", previous},
    //[TRANSPORT_CMD_SETPLAYMODE] =               {"SetPlayMode", NULL},	/*
    //optional */ [TRANSPORT_CMD_SETRECORDQUALITYMODE] =
    //{"SetRecordQualityMode", NULL},	/* optional */

    [TRANSPORT_CMD_COUNT] = {NULL, NULL}};

struct service *upnp_transport_get_service(void) {
  static struct service transport_service_ = {
      .service_mutex = &transport_mutex,
      .service_id = TRANSPORT_SERVICE_ID,
      .service_type = TRANSPORT_TYPE,
      .scpd_url = TRANSPORT_SCPD_URL,
      .control_url = TRANSPORT_CONTROL_URL,
      .event_url = TRANSPORT_EVENT_URL,
      .event_xml_ns = TRANSPORT_EVENT_XML_NS,
      .actions = transport_actions,
      .action_arguments = argument_list,
      .variable_container = NULL,  // set later.
      .last_change = NULL,
      .command_count = TRANSPORT_CMD_COUNT,
  };

  static struct var_meta transport_var_meta[] = {
      {TRANSPORT_VAR_TRANSPORT_STATE, "TransportState", "STOPPED", Eventing::kNo,
       DataType::kString, transport_state_xml_name, NULL},
      {TRANSPORT_VAR_TRANSPORT_STATUS, "TransportStatus", "OK", Eventing::kNo,
       DataType::kString, transport_stati, NULL},
      {TRANSPORT_VAR_PLAY_MEDIUM, "PlaybackStorageMedium", "UNKNOWN", Eventing::kNo,
       DataType::kString, media, NULL},
      {TRANSPORT_VAR_REC_MEDIUM, "RecordStorageMedium", "NOT_IMPLEMENTED",
       Eventing::kNo, DataType::kString, media, NULL},
      {TRANSPORT_VAR_PLAY_MEDIA, "PossiblePlaybackStorageMedia",
       "NETWORK,UNKNOWN", Eventing::kNo, DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_REC_MEDIA, "PossibleRecordStorageMedia", "NOT_IMPLEMENTED",
       Eventing::kNo, DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_CUR_PLAY_MODE, "CurrentPlayMode", "NORMAL", Eventing::kNo,
       DataType::kString, playmodi, NULL},
      {TRANSPORT_VAR_TRANSPORT_PLAY_SPEED, "TransportPlaySpeed", "1", Eventing::kNo,
       DataType::kString, playspeeds, NULL},
      {TRANSPORT_VAR_REC_MEDIUM_WR_STATUS, "RecordMediumWriteStatus",
       "NOT_IMPLEMENTED", Eventing::kNo, DataType::kString, rec_write_stati, NULL},
      {TRANSPORT_VAR_CUR_REC_QUAL_MODE, "CurrentRecordQualityMode",
       "NOT_IMPLEMENTED", Eventing::kNo, DataType::kString, rec_quality_modi, NULL},
      {TRANSPORT_VAR_POS_REC_QUAL_MODE, "PossibleRecordQualityModes",
       "NOT_IMPLEMENTED", Eventing::kNo, DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_NR_TRACKS, "NumberOfTracks", "0", Eventing::kNo, DataType::kUint4,
       NULL, &track_nr_range}, /* no step */
      {TRANSPORT_VAR_CUR_TRACK, "CurrentTrack", "0", Eventing::kNo, DataType::kUint4, NULL,
       &track_range},
      {TRANSPORT_VAR_CUR_TRACK_DUR, "CurrentTrackDuration", kZeroTime, Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_CUR_MEDIA_DUR, "CurrentMediaDuration", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_CUR_TRACK_META, "CurrentTrackMetaData", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_CUR_TRACK_URI, "CurrentTrackURI", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_AV_URI, "AVTransportURI", "", Eventing::kNo, DataType::kString, NULL,
       NULL},
      {TRANSPORT_VAR_AV_URI_META, "AVTransportURIMetaData", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_NEXT_AV_URI, "NextAVTransportURI", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_NEXT_AV_URI_META, "NextAVTransportURIMetaData", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_REL_TIME_POS, "RelativeTimePosition", kZeroTime, Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_ABS_TIME_POS, "AbsoluteTimePosition", "NOT_IMPLEMENTED",
       Eventing::kNo, DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_REL_CTR_POS, "RelativeCounterPosition", "2147483647",
       Eventing::kNo, DataType::kInt4, NULL, NULL},
      {TRANSPORT_VAR_ABS_CTR_POS, "AbsoluteCounterPosition", "2147483647",
       Eventing::kNo, DataType::kInt4, NULL, NULL},
      {TRANSPORT_VAR_LAST_CHANGE, "LastChange",
       "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/AVT/\"/>", Eventing::kYes,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_AAT_SEEK_MODE, "A_ARG_TYPE_SeekMode", "TRACK_NR", Eventing::kNo,
       DataType::kString, aat_seekmodi, NULL},
      {TRANSPORT_VAR_AAT_SEEK_TARGET, "A_ARG_TYPE_SeekTarget", "", Eventing::kNo,
       DataType::kString, NULL, NULL},
      {TRANSPORT_VAR_AAT_INSTANCE_ID, "A_ARG_TYPE_InstanceID", "0", Eventing::kNo,
       DataType::kUint4, NULL, NULL},
      {TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS, "CurrentTransportActions", "PLAY",
       Eventing::kNo, DataType::kString, NULL, NULL},

      {TRANSPORT_VAR_COUNT, NULL, NULL, Eventing::kNo, DataType::kUnknown, NULL, NULL}};

  if (transport_service_.variable_container == NULL) {
    state_variables_ =
      new VariableContainer(TRANSPORT_VAR_COUNT, transport_var_meta);
    transport_service_.variable_container = state_variables_;
  }
  return &transport_service_;
}

void upnp_transport_init(struct upnp_device *device) {
  struct service *service = upnp_transport_get_service();
  assert(service->last_change == NULL);
  service->last_change = new UPnPLastChangeCollector(
      service->variable_container, TRANSPORT_EVENT_XML_NS, device,
      TRANSPORT_SERVICE_ID);
  // Times and counters should not be evented. We only change REL_TIME
  // right now anyway (AVTransport-v1 document, 2.3.1 Event Model)
  service->last_change->AddIgnore(TRANSPORT_VAR_REL_TIME_POS);
  service->last_change->AddIgnore(TRANSPORT_VAR_ABS_TIME_POS);
  service->last_change->AddIgnore(TRANSPORT_VAR_REL_CTR_POS);
  service->last_change->AddIgnore(TRANSPORT_VAR_ABS_CTR_POS);
  pthread_t thread;
  pthread_create(&thread, NULL, thread_update_track_time, NULL);
}

void upnp_transport_register_variable_listener(
  const VariableContainer::ChangeListener &listener) {
  state_variables_->RegisterCallback(listener);
}
