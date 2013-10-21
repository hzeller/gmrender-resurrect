/* oh_playlist.h - OPenHome Playlist definitions
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

#ifndef _OH_PLAYLIST_H
#define _OH_PLAYLIST_H

#include "upnp_device.h"
#include "variable-container.h"

struct service *oh_playlist_get_service(void);
void oh_playlist_init(struct upnp_device *device);
void oh_playlist_register_variable_listener(variable_change_listener_t cb, void *userdata);

#endif /* _OH_PLAYLIST_H */
