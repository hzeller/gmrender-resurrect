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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "upnp_transport.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <glib.h>

#ifdef HAVE_LIBUPNP
#include <upnp/upnp.h>
#include <upnp/ithread.h>
#endif

#include "logging.h"
#include "output.h"
#include "upnp.h"
#include "upnp_device.h"
#include "variable-container.h"
#include "xmlescape.h"

#define TRANSPORT_SERVICE "urn:schemas-upnp-org:service:AVTransport:1"
#define TRANSPORT_TYPE "urn:schemas-upnp-org:service:AVTransport:1"
#define TRANSPORT_SCPD_URL "/upnp/rendertransportSCPD.xml"
#define TRANSPORT_CONTROL_URL "/upnp/control/rendertransport1"
#define TRANSPORT_EVENT_URL "/upnp/event/rendertransport1"

enum {
	TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS,
	TRANSPORT_CMD_GETDEVICECAPABILITIES,
	TRANSPORT_CMD_GETMEDIAINFO,
	TRANSPORT_CMD_GETPOSITIONINFO,
	TRANSPORT_CMD_GETTRANSPORTINFO,
	TRANSPORT_CMD_GETTRANSPORTSETTINGS,
	//TRANSPORT_CMD_NEXT,
	TRANSPORT_CMD_PAUSE,
	TRANSPORT_CMD_PLAY,
	//TRANSPORT_CMD_PREVIOUS,
	TRANSPORT_CMD_SEEK,
	TRANSPORT_CMD_SETAVTRANSPORTURI,             
	//TRANSPORT_CMD_SETPLAYMODE,
	TRANSPORT_CMD_STOP,
	TRANSPORT_CMD_SETNEXTAVTRANSPORTURI,
	//TRANSPORT_CMD_RECORD,
	//TRANSPORT_CMD_SETRECORDQUALITYMODE,
	TRANSPORT_CMD_UNKNOWN,                   
	TRANSPORT_CMD_COUNT
};

enum UPNPTransportError {
	UPNP_TRANSPORT_E_TRANSITION_NA	= 701,
	UPNP_TRANSPORT_E_NO_CONTENTS	= 702,
	UPNP_TRANSPORT_E_READ_ERROR	= 703,
	UPNP_TRANSPORT_E_PLAY_FORMAT_NS	= 704,
	UPNP_TRANSPORT_E_TRANSPORT_LOCKED	= 705,
	UPNP_TRANSPORT_E_WRITE_ERROR	= 706,
	UPNP_TRANSPORT_E_REC_MEDIA_WP	= 707,
	UPNP_TRANSPORT_E_REC_FORMAT_NS	= 708,
	UPNP_TRANSPORT_E_REC_MEDIA_FULL	= 709,
	UPNP_TRANSPORT_E_SEEKMODE_NS	= 710,
	UPNP_TRANSPORT_E_ILL_SEEKTARGET	= 711,
	UPNP_TRANSPORT_E_PLAYMODE_NS	= 712,
	UPNP_TRANSPORT_E_RECQUAL_NS	= 713,
	UPNP_TRANSPORT_E_ILLEGAL_MIME	= 714,
	UPNP_TRANSPORT_E_CONTENT_BUSY	= 715,
	UPNP_TRANSPORT_E_RES_NOT_FOUND	= 716,
	UPNP_TRANSPORT_E_PLAYSPEED_NS	= 717,
	UPNP_TRANSPORT_E_INVALID_IID	= 718,
};

static const char *transport_variables[] = {
	[TRANSPORT_VAR_TRANSPORT_STATE] = "TransportState",
	[TRANSPORT_VAR_TRANSPORT_STATUS] = "TransportStatus",
	[TRANSPORT_VAR_PLAY_MEDIUM] = "PlaybackStorageMedium",
	[TRANSPORT_VAR_REC_MEDIUM] = "RecordStorageMedium",
	[TRANSPORT_VAR_PLAY_MEDIA] = "PossiblePlaybackStorageMedia",
	[TRANSPORT_VAR_REC_MEDIA] = "PossibleRecordStorageMedia",
	[TRANSPORT_VAR_CUR_PLAY_MODE] = "CurrentPlayMode",
	[TRANSPORT_VAR_TRANSPORT_PLAY_SPEED] = "TransportPlaySpeed",
	[TRANSPORT_VAR_REC_MEDIUM_WR_STATUS] = "RecordMediumWriteStatus",
	[TRANSPORT_VAR_CUR_REC_QUAL_MODE] = "CurrentRecordQualityMode",
	[TRANSPORT_VAR_POS_REC_QUAL_MODE] = "PossibleRecordQualityModes",
	[TRANSPORT_VAR_NR_TRACKS] = "NumberOfTracks",
	[TRANSPORT_VAR_CUR_TRACK] = "CurrentTrack",
	[TRANSPORT_VAR_CUR_TRACK_DUR] = "CurrentTrackDuration",
	[TRANSPORT_VAR_CUR_MEDIA_DUR] = "CurrentMediaDuration",
	[TRANSPORT_VAR_CUR_TRACK_META] = "CurrentTrackMetaData",
	[TRANSPORT_VAR_CUR_TRACK_URI] = "CurrentTrackURI",
	[TRANSPORT_VAR_AV_URI] = "AVTransportURI",
	[TRANSPORT_VAR_AV_URI_META] = "AVTransportURIMetaData",
	[TRANSPORT_VAR_NEXT_AV_URI] = "NextAVTransportURI",
	[TRANSPORT_VAR_NEXT_AV_URI_META] = "NextAVTransportURIMetaData",
	[TRANSPORT_VAR_REL_TIME_POS] = "RelativeTimePosition",
	[TRANSPORT_VAR_ABS_TIME_POS] = "AbsoluteTimePosition",
	[TRANSPORT_VAR_REL_CTR_POS] = "RelativeCounterPosition",
	[TRANSPORT_VAR_ABS_CTR_POS] = "AbsoluteCounterPosition",
	[TRANSPORT_VAR_LAST_CHANGE] = "LastChange",
	[TRANSPORT_VAR_AAT_SEEK_MODE] = "A_ARG_TYPE_SeekMode",
	[TRANSPORT_VAR_AAT_SEEK_TARGET] = "A_ARG_TYPE_SeekTarget",
	[TRANSPORT_VAR_AAT_INSTANCE_ID] = "A_ARG_TYPE_InstanceID",
	[TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS] = "CurrentTransportActions",
	[TRANSPORT_VAR_UNKNOWN] = NULL
};

static const char kZeroTime[] = "0:00:00.000";
static const char *default_transport_values[] = {
	[TRANSPORT_VAR_TRANSPORT_STATE] = "STOPPED",
	[TRANSPORT_VAR_TRANSPORT_STATUS] = "OK",
	[TRANSPORT_VAR_PLAY_MEDIUM] = "UNKNOWN",
	[TRANSPORT_VAR_REC_MEDIUM] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_PLAY_MEDIA] = "NETWORK,UNKNOWN",
	[TRANSPORT_VAR_REC_MEDIA] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_CUR_PLAY_MODE] = "NORMAL",
	[TRANSPORT_VAR_TRANSPORT_PLAY_SPEED] = "1",
	[TRANSPORT_VAR_REC_MEDIUM_WR_STATUS] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_CUR_REC_QUAL_MODE] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_POS_REC_QUAL_MODE] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_NR_TRACKS] = "0",
	[TRANSPORT_VAR_CUR_TRACK] = "0",
	[TRANSPORT_VAR_CUR_TRACK_DUR] = kZeroTime,
	[TRANSPORT_VAR_CUR_MEDIA_DUR] = "",
	[TRANSPORT_VAR_CUR_TRACK_META] = "",
	[TRANSPORT_VAR_CUR_TRACK_URI] = "",
	[TRANSPORT_VAR_AV_URI] = "",
	[TRANSPORT_VAR_AV_URI_META] = "",
	[TRANSPORT_VAR_NEXT_AV_URI] = "",
	[TRANSPORT_VAR_NEXT_AV_URI_META] = "",
	[TRANSPORT_VAR_REL_TIME_POS] = kZeroTime,
	[TRANSPORT_VAR_ABS_TIME_POS] = "NOT_IMPLEMENTED",
	[TRANSPORT_VAR_REL_CTR_POS] = "2147483647",
	[TRANSPORT_VAR_ABS_CTR_POS] = "2147483647",
        [TRANSPORT_VAR_LAST_CHANGE] = "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/AVT/\"/>",
	[TRANSPORT_VAR_AAT_SEEK_MODE] = "TRACK_NR",
	[TRANSPORT_VAR_AAT_SEEK_TARGET] = "",
	[TRANSPORT_VAR_AAT_INSTANCE_ID] = "0",
	[TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS] = "PLAY",
	[TRANSPORT_VAR_UNKNOWN] = NULL
};

enum transport_state {
	TRANSPORT_STOPPED,
	TRANSPORT_PLAYING,
	TRANSPORT_TRANSITIONING,	/* optional */
	TRANSPORT_PAUSED_PLAYBACK,	/* optional */
	TRANSPORT_PAUSED_RECORDING,	/* optional */
	TRANSPORT_RECORDING,	/* optional */
	TRANSPORT_NO_MEDIA_PRESENT	/* optional */
};

static const char *transport_states[] = {
	"STOPPED",
	"PLAYING",
	"TRANSITIONING",
	"PAUSED_PLAYBACK",
	"PAUSED_RECORDING",
	"RECORDING",
	"NO_MEDIA_PRESENT",
	NULL
};
static const char *transport_stati[] = {
	"OK",
	"ERROR_OCCURRED",
	" vendor-defined ",
	NULL
};
static const char *media[] = {
	"UNKNOWN",
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
	NULL
};

static const char *playmodi[] = {
	"NORMAL",
	//"SHUFFLE",
	//"REPEAT_ONE",
	"REPEAT_ALL",
	//"RANDOM",
	//"DIRECT_1",
	"INTRO",
	NULL
};

static const char *playspeeds[] = {
	"1",
	" vendor-defined ",
	NULL
};

static const char *rec_write_stati[] = {
	"WRITABLE",
	"PROTECTED",
	"NOT_WRITABLE",
	"UNKNOWN",
	"NOT_IMPLEMENTED",
	NULL
};

static const char *rec_quality_modi[] = {
	"0:EP",
	"1:LP",
	"2:SP",
	"0:BASIC",
	"1:MEDIUM",
	"2:HIGH",
	"NOT_IMPLEMENTED",
	" vendor-defined ",
	NULL
};

static const char *aat_seekmodi[] = {
	"ABS_TIME",
	"REL_TIME",
	"ABS_COUNT",
	"REL_COUNT",
	"TRACK_NR",
	"CHANNEL_FREQ",
	"TAPE-INDEX",
	"FRAME",
	NULL
};

static struct param_range track_range = {
	0,
	4294967295LL,
	1
};

static struct param_range track_nr_range = {
	0,
	4294967295LL,
	0
};

static struct var_meta transport_var_meta[] = {
	[TRANSPORT_VAR_TRANSPORT_STATE] =		{ SENDEVENT_NO, DATATYPE_STRING, transport_states, NULL },
	[TRANSPORT_VAR_TRANSPORT_STATUS] =		{ SENDEVENT_NO, DATATYPE_STRING, transport_stati, NULL },
	[TRANSPORT_VAR_PLAY_MEDIUM] =			{ SENDEVENT_NO, DATATYPE_STRING, media, NULL },
	[TRANSPORT_VAR_REC_MEDIUM] =			{ SENDEVENT_NO, DATATYPE_STRING, media, NULL },
	[TRANSPORT_VAR_PLAY_MEDIA] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_REC_MEDIA] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_CUR_PLAY_MODE] =			{ SENDEVENT_NO, DATATYPE_STRING, playmodi, NULL, "NORMAL" },
	[TRANSPORT_VAR_TRANSPORT_PLAY_SPEED] =		{ SENDEVENT_NO, DATATYPE_STRING, playspeeds, NULL },
	[TRANSPORT_VAR_REC_MEDIUM_WR_STATUS] =		{ SENDEVENT_NO, DATATYPE_STRING, rec_write_stati, NULL },
	[TRANSPORT_VAR_CUR_REC_QUAL_MODE] =		{ SENDEVENT_NO, DATATYPE_STRING, rec_quality_modi, NULL },
	[TRANSPORT_VAR_POS_REC_QUAL_MODE] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_NR_TRACKS] =			{ SENDEVENT_NO, DATATYPE_UI4, NULL, &track_nr_range }, /* no step */
	[TRANSPORT_VAR_CUR_TRACK] =			{ SENDEVENT_NO, DATATYPE_UI4, NULL, &track_range },
	[TRANSPORT_VAR_CUR_TRACK_DUR] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_CUR_MEDIA_DUR] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_CUR_TRACK_META] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_CUR_TRACK_URI] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_AV_URI] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_AV_URI_META] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_NEXT_AV_URI] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_NEXT_AV_URI_META] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_REL_TIME_POS] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_ABS_TIME_POS] =			{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_REL_CTR_POS] =			{ SENDEVENT_NO, DATATYPE_I4, NULL, NULL },
	[TRANSPORT_VAR_ABS_CTR_POS] =			{ SENDEVENT_NO, DATATYPE_I4, NULL, NULL },
	[TRANSPORT_VAR_LAST_CHANGE] =			{ SENDEVENT_YES, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_AAT_SEEK_MODE] =			{ SENDEVENT_NO, DATATYPE_STRING, aat_seekmodi, NULL },
	[TRANSPORT_VAR_AAT_SEEK_TARGET] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_AAT_INSTANCE_ID] =		{ SENDEVENT_NO, DATATYPE_UI4, NULL, NULL },
	[TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS] =		{ SENDEVENT_NO, DATATYPE_STRING, NULL, NULL },
	[TRANSPORT_VAR_UNKNOWN] =			{ SENDEVENT_NO, DATATYPE_UNKNOWN, NULL, NULL }
};	

static struct argument *arguments_setavtransporturi[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "CurrentURI", PARAM_DIR_IN, TRANSPORT_VAR_AV_URI },
        & (struct argument) { "CurrentURIMetaData", PARAM_DIR_IN, TRANSPORT_VAR_AV_URI_META },
        NULL
};

static struct argument *arguments_setnextavtransporturi[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "NextURI", PARAM_DIR_IN, TRANSPORT_VAR_NEXT_AV_URI },
        & (struct argument) { "NextURIMetaData", PARAM_DIR_IN, TRANSPORT_VAR_NEXT_AV_URI_META },
        NULL
};

static struct argument *arguments_getmediainfo[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "NrTracks", PARAM_DIR_OUT, TRANSPORT_VAR_NR_TRACKS },
        & (struct argument) { "MediaDuration", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_MEDIA_DUR },
        & (struct argument) { "CurrentURI", PARAM_DIR_OUT, TRANSPORT_VAR_AV_URI },
        & (struct argument) { "CurrentURIMetaData", PARAM_DIR_OUT, TRANSPORT_VAR_AV_URI_META },
        & (struct argument) { "NextURI", PARAM_DIR_OUT, TRANSPORT_VAR_NEXT_AV_URI },
        & (struct argument) { "NextURIMetaData", PARAM_DIR_OUT, TRANSPORT_VAR_NEXT_AV_URI_META },
        & (struct argument) { "PlayMedium", PARAM_DIR_OUT, TRANSPORT_VAR_PLAY_MEDIUM },
        & (struct argument) { "RecordMedium", PARAM_DIR_OUT, TRANSPORT_VAR_REC_MEDIUM },
        & (struct argument) { "WriteStatus", PARAM_DIR_OUT, TRANSPORT_VAR_REC_MEDIUM_WR_STATUS },
        NULL
};

static struct argument *arguments_gettransportinfo[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "CurrentTransportState", PARAM_DIR_OUT, TRANSPORT_VAR_TRANSPORT_STATE },
        & (struct argument) { "CurrentTransportStatus", PARAM_DIR_OUT, TRANSPORT_VAR_TRANSPORT_STATUS },
        & (struct argument) { "CurrentSpeed", PARAM_DIR_OUT, TRANSPORT_VAR_TRANSPORT_PLAY_SPEED },
        NULL
};

static struct argument *arguments_getpositioninfo[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "Track", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_TRACK },
        & (struct argument) { "TrackDuration", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_TRACK_DUR },
        & (struct argument) { "TrackMetaData", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_TRACK_META },
        & (struct argument) { "TrackURI", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_TRACK_URI },
        & (struct argument) { "RelTime", PARAM_DIR_OUT, TRANSPORT_VAR_REL_TIME_POS },
        & (struct argument) { "AbsTime", PARAM_DIR_OUT, TRANSPORT_VAR_ABS_TIME_POS },
        & (struct argument) { "RelCount", PARAM_DIR_OUT, TRANSPORT_VAR_REL_CTR_POS },
        & (struct argument) { "AbsCount", PARAM_DIR_OUT, TRANSPORT_VAR_ABS_CTR_POS },
        NULL
};

static struct argument *arguments_getdevicecapabilities[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "PlayMedia", PARAM_DIR_OUT, TRANSPORT_VAR_PLAY_MEDIA },
        & (struct argument) { "RecMedia", PARAM_DIR_OUT, TRANSPORT_VAR_REC_MEDIA },
        & (struct argument) { "RecQualityModes", PARAM_DIR_OUT, TRANSPORT_VAR_POS_REC_QUAL_MODE },
	NULL
};

static struct argument *arguments_gettransportsettings[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "PlayMode", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_PLAY_MODE },
        & (struct argument) { "RecQualityMode", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_REC_QUAL_MODE },
	NULL
};

static struct argument *arguments_stop[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
	NULL
};
static struct argument *arguments_play[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "Speed", PARAM_DIR_IN, TRANSPORT_VAR_TRANSPORT_PLAY_SPEED },
	NULL
};
static struct argument *arguments_pause[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
	NULL
};
//static struct argument *arguments_record[] = {
//        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	NULL
//};

static struct argument *arguments_seek[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "Unit", PARAM_DIR_IN, TRANSPORT_VAR_AAT_SEEK_MODE },
        & (struct argument) { "Target", PARAM_DIR_IN, TRANSPORT_VAR_AAT_SEEK_TARGET },
	NULL
};
//static struct argument *arguments_next[] = {
//        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	NULL
//};
//static struct argument *arguments_previous[] = {
//        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
//	NULL
//};
//static struct argument *arguments_setplaymode[] = {
//        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
//        & (struct argument) { "NewPlayMode", PARAM_DIR_IN, TRANSPORT_VAR_CUR_PLAY_MODE },
//	NULL
//};
//static struct argument *arguments_setrecordqualitymode[] = {
//        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
//        & (struct argument) { "NewRecordQualityMode", PARAM_DIR_IN, TRANSPORT_VAR_CUR_REC_QUAL_MODE },
//	NULL
//};
static struct argument *arguments_getcurrenttransportactions[] = {
        & (struct argument) { "InstanceID", PARAM_DIR_IN, TRANSPORT_VAR_AAT_INSTANCE_ID },
        & (struct argument) { "Actions", PARAM_DIR_OUT, TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS },
	NULL
};


static struct argument **argument_list[] = {
	[TRANSPORT_CMD_SETAVTRANSPORTURI] =         arguments_setavtransporturi,
	[TRANSPORT_CMD_GETDEVICECAPABILITIES] =     arguments_getdevicecapabilities,
	[TRANSPORT_CMD_GETMEDIAINFO] =              arguments_getmediainfo,
	[TRANSPORT_CMD_SETNEXTAVTRANSPORTURI] =     arguments_setnextavtransporturi,
	[TRANSPORT_CMD_GETTRANSPORTINFO] =          arguments_gettransportinfo,
	[TRANSPORT_CMD_GETPOSITIONINFO] =           arguments_getpositioninfo,
	[TRANSPORT_CMD_GETTRANSPORTSETTINGS] =      arguments_gettransportsettings,
	[TRANSPORT_CMD_STOP] =                      arguments_stop,
	[TRANSPORT_CMD_PLAY] =                      arguments_play,
	[TRANSPORT_CMD_PAUSE] =                     arguments_pause,
	//[TRANSPORT_CMD_RECORD] =                    arguments_record,
	[TRANSPORT_CMD_SEEK] =                      arguments_seek,
	//[TRANSPORT_CMD_NEXT] =                      arguments_next,
	//[TRANSPORT_CMD_PREVIOUS] =                  arguments_previous,
	//[TRANSPORT_CMD_SETPLAYMODE] =               arguments_setplaymode,
	//[TRANSPORT_CMD_SETRECORDQUALITYMODE] =      arguments_setrecordqualitymode,
	[TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS] = arguments_getcurrenttransportactions,
	[TRANSPORT_CMD_UNKNOWN] =	NULL
};


// Our 'instance' variables.
static enum transport_state transport_state_ = TRANSPORT_STOPPED;
extern struct service transport_service_;   // Defined below.
static variable_container_t *state_variables_ = NULL;
static upnp_last_change_collector_t *upnp_collector_ = NULL;
static const char **hack_variables_ = NULL;  // tmp access.

/* protects transport_values, and service-specific state */

#ifdef HAVE_LIBUPNP
static ithread_mutex_t transport_mutex;
#endif

static void service_lock(void)
{
#ifdef HAVE_LIBUPNP
	ithread_mutex_lock(&transport_mutex);
#endif
	if (upnp_collector_) {
		UPnPLastChangeCollector_start_transaction(upnp_collector_);
	}
}

static void service_unlock(void)
{
	if (upnp_collector_) {
		UPnPLastChangeCollector_commit(upnp_collector_);
	}
#ifdef HAVE_LIBUPNP
	ithread_mutex_unlock(&transport_mutex);
#endif
}


static int get_media_info(struct action_event *event)
{
	char *value;
	int rc;
	ENTER();

	value = upnp_get_string(event, "InstanceID");
	if (value == NULL) {
		rc = -1;
		goto out;
	}

	free(value);

	rc = upnp_append_variable(event, TRANSPORT_VAR_NR_TRACKS,
				  "NrTracks");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_MEDIA_DUR,
				  "MediaDuration");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_AV_URI,
				  "CurrentURI");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_AV_URI_META,
				  "CurrentURIMetaData");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_NEXT_AV_URI,
				  "NextURI");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_NEXT_AV_URI_META,
				  "NextURIMetaData");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_REC_MEDIA,
				  "PlayMedium");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_REC_MEDIUM,
				  "RecordMedium");
	if (rc)
		goto out;

	rc = upnp_append_variable(event,
				  TRANSPORT_VAR_REC_MEDIUM_WR_STATUS,
				  "WriteStatus");
	if (rc)
		goto out;

      out:
	return rc;
}

// Replace given variable without sending an state-change event.
static int replace_var(transport_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

// Transport uri always comes in uri/meta pairs. Set these and also the related
// track uri/meta variables.
// Returns 1, if this meta-data likely needs to be updated while the stream
// is playing (e.g. radio broadcast).
static int replace_transport_uri_and_meta(const char *uri, const char *meta) {
	replace_var(TRANSPORT_VAR_AV_URI, uri);
	replace_var(TRANSPORT_VAR_AV_URI_META, meta);

	// This influences as well the tracks. If there is a non-empty URI,
	// we have exactly one track.
	const char *tracks = (uri != NULL && strlen(uri) > 0) ? "1" : "0";
	replace_var(TRANSPORT_VAR_NR_TRACKS, tracks);
	replace_var(TRANSPORT_VAR_CUR_TRACK, tracks);

	// Also the current track URI mirrors the transport URI.
	replace_var(TRANSPORT_VAR_CUR_TRACK_URI, uri);
	replace_var(TRANSPORT_VAR_CUR_TRACK_META, meta);

	// We only really want to send back meta data if we didn't get anything
	// useful or if this is an audio item.
	const int requires_stream_meta_callback = (strlen(meta) == 0)
		|| strstr(meta, "object.item.audioItem");
	return requires_stream_meta_callback;
}

static void change_transport_state(enum transport_state new_state) {
	transport_state_ = new_state;
	assert(new_state >= TRANSPORT_STOPPED
	       && new_state < TRANSPORT_NO_MEDIA_PRESENT);
	if (!replace_var(TRANSPORT_VAR_TRANSPORT_STATE,
			 transport_states[new_state])) {
		return;  // no change.
	}
	const char *available_actions = NULL;
	switch (new_state) {
	case TRANSPORT_STOPPED:
		if (strlen(hack_variables_[TRANSPORT_VAR_AV_URI]) == 0) {
			available_actions = "PLAY";
		} else {
			available_actions = "PLAY,SEEK";
		}
		break;
	case TRANSPORT_PLAYING:
		available_actions = "PAUSE,STOP,SEEK";
		break;
	case TRANSPORT_PAUSED_PLAYBACK:
		available_actions = "PLAY,STOP,SEEK";
		break;
	case TRANSPORT_TRANSITIONING:
	case TRANSPORT_PAUSED_RECORDING:
	case TRANSPORT_RECORDING:
	case TRANSPORT_NO_MEDIA_PRESENT:
		// We should not switch to this state.
		break;
	}
	if (available_actions) {
		replace_var(TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS,
			    available_actions);
	}
}

static int obtain_instanceid(struct action_event *event, int *instance)
{
	char *value;
	int rc = 0;
	
	ENTER();

	value = upnp_get_string(event, "InstanceID");
	if (value == NULL) {
#ifdef HAVE_LIBUPNP
		upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
			       "Missing InstanceID");
#endif
		return -1;
	}
	free(value);

	// TODO - parse value, and store in *instance, if instance!=NULL

	LEAVE();

	return rc;
}

// Callback from our output if the song meta data changed.
static void update_meta_from_stream(const struct SongMetaData *meta) {
	if (meta->title == NULL || strlen(meta->title) == 0) {
		return;
	}
	const char *original_xml = hack_variables_[TRANSPORT_VAR_AV_URI_META];
	char *didl = SongMetaData_to_DIDL(meta, original_xml);
	service_lock();
	replace_var(TRANSPORT_VAR_AV_URI_META, didl);
	replace_var(TRANSPORT_VAR_CUR_TRACK_META, didl);
	service_unlock();
	free(didl);
}

/* UPnP action handlers */

static int set_avtransport_uri(struct action_event *event)
{
	int rc = 0;

	ENTER();

	if (obtain_instanceid(event, NULL)) {
		LEAVE();
		return -1;
	}
	char *uri = upnp_get_string(event, "CurrentURI");
	if (uri == NULL) {
		LEAVE();
		return -1;
	}

	service_lock();
	char *meta = upnp_get_string(event, "CurrentURIMetaData");
	int requires_meta_update = replace_transport_uri_and_meta(uri, meta);

	output_set_uri(uri, (requires_meta_update
			     ? update_meta_from_stream
			     : NULL));
	service_unlock();

	free(uri);
	free(meta);

	LEAVE();
	return rc;
}

static int set_next_avtransport_uri(struct action_event *event)
{
	int rc = 0;
	char *value;

	ENTER();

	if (obtain_instanceid(event, NULL)) {
		LEAVE();
		return -1;
	}

	value = upnp_get_string(event, "NextURI");
	if (value == NULL) {
		LEAVE();
		return -1;
	}

	service_lock();

	output_set_next_uri(value);
	replace_var(TRANSPORT_VAR_NEXT_AV_URI, value);
	free(value);

	value = upnp_get_string(event, "NextURIMetaData");
	if (value == NULL) {
		rc = -1;
	} else {
		replace_var(TRANSPORT_VAR_NEXT_AV_URI_META, value);
		free(value);
	}

	service_unlock();

	LEAVE();
	return rc;
}

static int get_transport_info(struct action_event *event)
{
	int rc;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
		goto out;
	}

	rc = upnp_append_variable(event, TRANSPORT_VAR_TRANSPORT_STATE,
				  "CurrentTransportState");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_TRANSPORT_STATUS,
				  "CurrentTransportStatus");
	if (rc)
		goto out;

	rc = upnp_append_variable(event,
				  TRANSPORT_VAR_TRANSPORT_PLAY_SPEED,
				  "CurrentSpeed");
	if (rc)
		goto out;

      out:
	LEAVE();
	return rc;
}

static int get_current_transportactions(struct action_event *event)
{
	int rc;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
		goto out;
	}

	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_TRANSPORT_ACTIONS,
				  "Actions");
	if (rc)
		goto out;
      out:
	LEAVE();
	return rc;
}

static int get_transport_settings(struct action_event *event)
{
	int rc = 0;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
		goto out;
	}

      out:
	LEAVE();
	return rc;
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
	const int milli_second = t / 1000000;
	snprintf(result, size, "%d:%02d:%02d.%03d", hour, minute, second,
		 milli_second / 100 * 100);  // quantize to 100
}

static gint64 parse_upnp_time(const char *time_string) {
	int hour = 0;
	int minute = 0;
	int second = 0;
	sscanf(time_string, "%d:%02d:%02d", &hour, &minute, &second);
	const gint64 seconds = (hour * 3600 + minute * 60 + second);
	const gint64 one_sec_unit = 1000000000LL;
	const gint64 nanos = one_sec_unit * seconds;
	printf("Parse time: '%s' -> %llds (%lld ns)\n", time_string,
	       (long long int) seconds, (long long int) nanos);
	return nanos;
}

// We constantly update the track time to event about it to our clients.
static void *thread_update_track_time(void *userdata) {
	const gint64 one_sec_unit = 1000000000LL;
	char tbuf[32];
	gint64 last_duration = -1, last_position = -1;
	for (;;) {
		usleep(500000);  // 500ms
		service_lock();
		gint64 duration, position;
		const int pos_result = output_get_position(&duration, &position);
		if (pos_result == 0) {
			if (duration != last_duration) {
				print_upnp_time(tbuf, sizeof(tbuf), duration);
				replace_var(TRANSPORT_VAR_CUR_TRACK_DUR, tbuf);
				last_duration = duration;
			}
			if (position / one_sec_unit != last_position) {
				print_upnp_time(tbuf, sizeof(tbuf), position);
				replace_var(TRANSPORT_VAR_REL_TIME_POS, tbuf);
				last_position = position / one_sec_unit;
			}
		}
		service_unlock();
	}
	return NULL;  // not reached.
}

static int get_position_info(struct action_event *event)
{
	int rc;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
		goto out;
	}
	
	// Variables are changed by update thread in parallel, so we need
	// the lock.
	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK, "Track");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_DUR,
				  "TrackDuration");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_META,
				  "TrackMetaData");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_CUR_TRACK_URI,
				  "TrackURI");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_REL_TIME_POS,
				  "RelTime");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_ABS_TIME_POS,
				  "AbsTime");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_REL_CTR_POS,
				  "RelCount");
	if (rc)
		goto out;

	rc = upnp_append_variable(event, TRANSPORT_VAR_ABS_CTR_POS,
				  "AbsCount");
	if (rc)
		goto out;

      out:
	LEAVE();
	return rc;
}

static int get_device_caps(struct action_event *event)
{
	int rc = 0;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
		goto out;
	}

      out:
	LEAVE();
	return rc;
}

static int stop(struct action_event *event)
{
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		return -1;
	}

	service_lock();
	switch (transport_state_) {
	case TRANSPORT_STOPPED:
		// nothing to change.
		break;
	case TRANSPORT_PLAYING:
	case TRANSPORT_TRANSITIONING:
	case TRANSPORT_PAUSED_RECORDING:
	case TRANSPORT_RECORDING:
	case TRANSPORT_PAUSED_PLAYBACK:
		output_stop();
		change_transport_state(TRANSPORT_STOPPED);
		// Set TransportPlaySpeed to '1'
		break;

	case TRANSPORT_NO_MEDIA_PRESENT:
		/* action not allowed in these states - error 701 */
		upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
			       "Transition not allowed");

		break;
	}
	service_unlock();

	LEAVE();

	return 0;
}

static void inform_done_playing(enum PlayFeedback fb) {
	service_lock();
	switch (fb) {
	case PLAY_STOPPED:
		replace_transport_uri_and_meta("", "");
		change_transport_state(TRANSPORT_STOPPED);
		break;
	case PLAY_STARTED_NEXT_STREAM:
		replace_transport_uri_and_meta(
			   hack_variables_[TRANSPORT_VAR_NEXT_AV_URI],
			   hack_variables_[TRANSPORT_VAR_NEXT_AV_URI_META]);
		replace_var(TRANSPORT_VAR_NEXT_AV_URI, "");
		replace_var(TRANSPORT_VAR_NEXT_AV_URI_META, "");
		break;
	}
	service_unlock();
}

static int play(struct action_event *event)
{
	int rc = 0;

	ENTER();

	if (obtain_instanceid(event, NULL)) {
		LEAVE();
		return -1;
	}

	service_lock();
	switch (transport_state_) {
	case TRANSPORT_PLAYING:
		// nothing to change.
		// TODO: Set TransportPlaySpeed to '1'
		break;
	case TRANSPORT_STOPPED:
		// If we were stopped before, we start a new song now. So just
		// set the time to zero now; otherwise we will see the old
		// value of the previous song until it updates some fractions
		// of a second later.
		replace_var(TRANSPORT_VAR_REL_TIME_POS, kZeroTime);
		/* fall through */

	case TRANSPORT_PAUSED_PLAYBACK:
		if (output_play(&inform_done_playing)) {
			upnp_set_error(event, 704, "Playing failed");
			rc = -1;
		} else {
			change_transport_state(TRANSPORT_PLAYING);
		}
		// Set TransportPlaySpeed to '1'
		break;

	case TRANSPORT_NO_MEDIA_PRESENT:
	case TRANSPORT_TRANSITIONING:
	case TRANSPORT_PAUSED_RECORDING:
	case TRANSPORT_RECORDING:
		/* action not allowed in these states - error 701 */
		upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
			       "Transition not allowed");
		rc = -1;

		break;
	}
	service_unlock();

	LEAVE();
	return rc;
}

static int pause_stream(struct action_event *event)
{
	int rc = 0;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		LEAVE();
		return -1;
	}

	service_lock();
	switch (transport_state_) {
        case TRANSPORT_PAUSED_PLAYBACK:
		// Nothing to change.
		break;

	case TRANSPORT_PLAYING:
		if (output_pause()) {
			upnp_set_error(event, 704, "Pause failed");
			rc = -1;
		} else {
			change_transport_state(TRANSPORT_PAUSED_PLAYBACK);
		}
		// Set TransportPlaySpeed to '1'
		break;

        default:
		/* action not allowed in these states - error 701 */
		upnp_set_error(event, UPNP_TRANSPORT_E_TRANSITION_NA,
			       "Transition not allowed");
		rc = -1;
        }
	service_unlock();

	LEAVE();

	return rc;
}

static int seek(struct action_event *event)
{
	int rc = 0;
	ENTER();

	if (obtain_instanceid(event, NULL)) {
		rc = -1;
	}

	char *unit = upnp_get_string(event, "Unit");
	if (strcmp(unit, "REL_TIME") == 0) {
		// This is the only thing we support right now.
		char *target = upnp_get_string(event, "Target");
		gint64 nanos = parse_upnp_time(target);
		service_lock();
		if (output_seek(nanos) == 0) {
			// TODO(hzeller): Seeking might take some time,
			// pretend to already be there. Should we go into
			// TRANSITION mode ?
			// (gstreamer will go into PAUSE, then PLAYING)
			replace_var(TRANSPORT_VAR_REL_TIME_POS, target);
		}
		service_unlock();
		free(target);
	}
	free(unit);

	LEAVE();

	return rc;
}

static struct action transport_actions[] = {
	[TRANSPORT_CMD_GETCURRENTTRANSPORTACTIONS] = {"GetCurrentTransportActions", get_current_transportactions},
	[TRANSPORT_CMD_GETDEVICECAPABILITIES] =     {"GetDeviceCapabilities", get_device_caps},
	[TRANSPORT_CMD_GETMEDIAINFO] =              {"GetMediaInfo", get_media_info},
	[TRANSPORT_CMD_SETAVTRANSPORTURI] =         {"SetAVTransportURI", set_avtransport_uri},	/* RC9800i */
	[TRANSPORT_CMD_SETNEXTAVTRANSPORTURI] =     {"SetNextAVTransportURI", set_next_avtransport_uri},
	[TRANSPORT_CMD_GETTRANSPORTINFO] =          {"GetTransportInfo", get_transport_info},
	[TRANSPORT_CMD_GETPOSITIONINFO] =           {"GetPositionInfo", get_position_info},
	[TRANSPORT_CMD_GETTRANSPORTSETTINGS] =      {"GetTransportSettings", get_transport_settings},
	[TRANSPORT_CMD_STOP] =                      {"Stop", stop},
	[TRANSPORT_CMD_PLAY] =                      {"Play", play},
	[TRANSPORT_CMD_PAUSE] =                     {"Pause", pause_stream},
	//[TRANSPORT_CMD_RECORD] =                    {"Record", NULL},	/* optional */
	[TRANSPORT_CMD_SEEK] =                      {"Seek", seek},
	//[TRANSPORT_CMD_NEXT] =                      {"Next", next},
	//[TRANSPORT_CMD_PREVIOUS] =                  {"Previous", previous},
	//[TRANSPORT_CMD_SETPLAYMODE] =               {"SetPlayMode", NULL},	/* optional */
	//[TRANSPORT_CMD_SETRECORDQUALITYMODE] =      {"SetRecordQualityMode", NULL},	/* optional */
	[TRANSPORT_CMD_UNKNOWN] =                  {NULL, NULL}
};

struct service *upnp_transport_get_service(void) {
	if (transport_service_.variable_values == NULL) {
		state_variables_ =
			VariableContainer_new(TRANSPORT_VAR_COUNT,
					      transport_variables,
					      default_transport_values);

		transport_service_.variable_values =
			VariableContainer_get_values_hack(state_variables_);
		transport_service_.variable_container = state_variables_;
		hack_variables_ = transport_service_.variable_values;	    
	}
	return &transport_service_;
}

void upnp_transport_init(struct upnp_device *device) {
	assert(upnp_collector_ == NULL);   // only initialize once.
	upnp_collector_ = UPnPLastChangeCollector_new(state_variables_,
						      TRANSPORT_VAR_LAST_CHANGE,
						      device, TRANSPORT_SERVICE);
	pthread_t thread;
	pthread_create(&thread, NULL, thread_update_track_time, NULL);
}

void upnp_transport_register_variable_listener(variable_change_listener_t cb,
					       void *userdata) {
	VariableContainer_register_callback(state_variables_, cb, userdata);
}

struct service transport_service_ = {
	.service_name =         TRANSPORT_SERVICE,
	.type =                 TRANSPORT_TYPE,
	.scpd_url =		TRANSPORT_SCPD_URL,
	.control_url =		TRANSPORT_CONTROL_URL,
	.event_url =		TRANSPORT_EVENT_URL,
	.actions =              transport_actions,
	.action_arguments =     argument_list,
	.variable_names =       transport_variables,
	.variable_values =      NULL, // set later.
	.variable_container =   NULL, // set later.
	.variable_meta =        transport_var_meta,
	.variable_count =       TRANSPORT_VAR_UNKNOWN,
	.command_count =        TRANSPORT_CMD_UNKNOWN,
#ifdef HAVE_LIBUPNP
	.service_mutex =        &transport_mutex
#endif
};
