/* mpris_notification.h - MPRIS D-Bus Status Notification
 *
 * Copyright (C) 2020 Tucker Kern
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

#ifndef _MPRIS_NOTIFICATION_H
#define _MPRIS_NOTIFICATION_H

#include <gio/gio.h>
#include <glib.h>

#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_BASE_NAME "org.mpris.MediaPlayer2.gmediarender.uuid"

void mpris_configure(const char* uuid, const char* friendly_name);

#endif