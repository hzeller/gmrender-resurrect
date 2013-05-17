/* upnp_transport.h - UPnP AVTransport definitions
 *
 * Copyright (C) 2005   Ivo Clarysse
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

#ifndef _UPNP_TRANSPORT_H
#define _UPNP_TRANSPORT_H

#include "variable-container.h"

struct service;
struct upnp_device;

struct service *upnp_transport_get_service(void);
void upnp_transport_init(struct upnp_device *);

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
	TRANSPORT_VAR_UNKNOWN,
	TRANSPORT_VAR_COUNT
} transport_variable_t;

// Register a callback to get informed when variables change. This should
// return quickly.
void upnp_transport_register_variable_listener(variable_change_listener_t cb,
						       void *userdata);

#endif /* _UPNP_TRANSPORT_H */
