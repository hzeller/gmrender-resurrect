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

extern struct service transport_service;

#define UPNP_TRANSPORT_E_TRANSITION_NA	701
#define UPNP_TRANSPORT_E_NO_CONTENTS	702
#define UPNP_TRANSPORT_E_READ_ERROR	703
#define UPNP_TRANSPORT_E_PLAY_FORMAT_NS	704
#define UPNP_TRANSPORT_E_TRANSPORT_LOCKED	705
#define UPNP_TRANSPORT_E_WRITE_ERROR	706
#define UPNP_TRANSPORT_E_REC_MEDIA_WP	707
#define UPNP_TRANSPORT_E_REC_FORMAT_NS	708
#define UPNP_TRANSPORT_E_REC_MEDIA_FULL	709
#define UPNP_TRANSPORT_E_SEEKMODE_NS	710
#define UPNP_TRANSPORT_E_ILL_SEEKTARGET	711
#define UPNP_TRANSPORT_E_PLAYMODE_NS	712
#define UPNP_TRANSPORT_E_RECQUAL_NS	713
#define UPNP_TRANSPORT_E_ILLEGAL_MIME	714
#define UPNP_TRANSPORT_E_CONTENT_BUSY	715
#define UPNP_TRANSPORT_E_RES_NOT_FOUND	716
#define UPNP_TRANSPORT_E_PLAYSPEED_NS	717
#define UPNP_TRANSPORT_E_INVALID_IID	718

#endif /* _UPNP_TRANSPORT_H */
