// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* dbus_notification.c - D-Bus Status Notification
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "mpris_notification.h"
#include "logging.h"
#include "upnp_control.h"
#include "upnp_transport.h"
#include "mpris_interface.h"

static const char* TAG = "mpris";

static struct
{
	MprisMediaPlayer2* media_player;
	MprisMediaPlayer2Player* player;
} mpris;

/**
	@brief  Initialize the MPRIS MediaPlayer2 Player object

	@param  none
	@retval none
*/
static void mpris_player_init()
{
	// Construct the MediaPlayer2.Player interface
	mpris.player = mpris_media_player2_player_skeleton_new();

	// We won't accept any control inputs
	mpris_media_player2_player_set_can_control(mpris.player, false);
	mpris_media_player2_player_set_can_go_next(mpris.player, false);
	mpris_media_player2_player_set_can_go_previous(mpris.player, false);
	mpris_media_player2_player_set_can_play(mpris.player, false);
	mpris_media_player2_player_set_can_pause(mpris.player, false);
	mpris_media_player2_player_set_can_seek(mpris.player, false);

	// Set initial state
	mpris_media_player2_player_set_playback_status(mpris.player, "Stopped");
	mpris_media_player2_player_set_position(mpris.player, 0);
	//mpris_media_player2_player_set_loop_status(player_, "None"); // Optional
	mpris_media_player2_player_set_rate(mpris.player, 1.0);
	mpris_media_player2_player_set_minimum_rate(mpris.player, 1.0);
	mpris_media_player2_player_set_maximum_rate(mpris.player, 1.0);
	//mpris_media_player2_player_set_shuffle(player_, false); // Optional
	mpris_media_player2_player_set_volume(mpris.player, 1.0);
}

/**
	@brief  Initialize the MPRIS MediaPlayer2 object

	@param  name Friendly name of the player
	@retval none
*/
static void mpris_media_player_init(const char* name)
{
	// Construct the MediaPlayer2 interface
	mpris.media_player = mpris_media_player2_skeleton_new();

	// We won't accept any quit, raise or fullscreen commands
	mpris_media_player2_set_can_quit(mpris.media_player, false);
	mpris_media_player2_set_can_raise(mpris.media_player, false);
	mpris_media_player2_set_can_set_fullscreen(mpris.media_player, false);

	// Set the initial state
	mpris_media_player2_set_has_track_list(mpris.media_player, false);
	mpris_media_player2_set_fullscreen(mpris.media_player, false);

	// TODO Technically we know enough to fill these, but we don't care really
	mpris_media_player2_set_supported_uri_schemes(mpris.media_player, NULL);
	mpris_media_player2_set_supported_mime_types(mpris.media_player, NULL);

	// Set a friendly name
	mpris_media_player2_set_identity(mpris.media_player, name);
}

/**
	@brief  Export MPRIS objects on the D-Bus at the provided path

	@param  connection GDBusConnection received from bus_acquired callback
	@param  path Path to export objects to
	@retval none
*/
static void mpris_export(GDBusConnection* connection, const char* path)
{
	// Export this interface on the bus
	g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mpris.media_player), connection, path, NULL);
	g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mpris.player), connection, path, NULL);
}

/**
	@brief  Set the playback status of the MPRIS player object

	@param  status Playback status string
	@retval none
*/
static void mpris_set_playback_status(const char* status)
{
	mpris_media_player2_player_set_playback_status(mpris.player, status);
}

/**
	@brief  Set the volume level of the MPRIS player object

	@param  volume Volume fraction to set
	@retval none
*/
static void mpris_set_volume(double volume)
{
	mpris_media_player2_player_set_volume(mpris.player, volume);
}

/**
	@brief  UPNP Transport callback that updates MPRIS playback state when TransportState variable changes

	@param  userdata Unused
	@param  var_num Unused
	@param  variable_name Name of variable that was changed
	@param  old_value unused
	@param  new_value New value of variable that changed
	@retval none
*/
static void mpris_transport_variable_callback(void* userdata, int var_num, const char* variable_name, const char* old_value, const char* new_value)
{
	if (strcmp(variable_name, "TransportState") != 0)
		return;

	if (strcmp(new_value, "PLAYING") == 0)
		mpris_set_playback_status("Playing");
	else if (strcmp(new_value, "PAUSED_PLAYBACK") == 0)
		mpris_set_playback_status("Paused");
	else if (strcmp(new_value, "STOPPED") == 0)
		mpris_set_playback_status("Stopped");
	else
		Log_error(TAG, "Unknown transport state '%s'.", new_value);
}

/**
	@brief  UPNP Control callback that updates MPRIS state when Volume variable changes

	@param  userdata Unused
	@param  var_num Unused
	@param  variable_name Name of variable that was changed
	@param  old_value unused
	@param  new_value New value of variable that changed
	@retval none
*/
static void mpris_control_variable_callback(void* userdata, int var_num, const char* variable_name, const char* old_value, const char* new_value)
{
	if (strcmp(variable_name, "Volume") != 0)
		return;

	mpris_set_volume(strtod(new_value, NULL)/100);
}

/**
	@brief  Callback when D-Bus is acquired.

	@param  connection GDBusConnection to D-Bus
	@param  name unused
	@param  user_data unused
	@retval none
*/
static void bus_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data)
{
	Log_info(TAG, "Acquired bus. Exporting MPRIS objects.");

	mpris_export(connection, MPRIS_PATH);
}

/**
	@brief  Callback when name is acquired on the D-Bus.

	@param  connection GDBusConnection to D-Bus
	@param  name Name acquired on bus
	@param  user_data unused
	@retval none
*/
static void name_acquired(GDBusConnection* connection, const gchar* name, gpointer user_data)
{
	Log_info(TAG, "Acquired '%s' on D-Bus.", name);
}

/**
	@brief  Callback when name is lost on the D-Bus.

	@param  connection GDBusConnection to D-Bus
	@param  name Name lost on bus
	@param  user_data unused
	@retval none
*/
static void name_lost(GDBusConnection* connection, const gchar* name, gpointer user_data)
{
	Log_error(TAG, "Lost '%s' on D-Bus.", name);
}

/**
	@brief  Callback when name is lost on the D-Bus.

	@param  uuid UUID of this renderer
	@param  friendly_name Friendly name of this renderer
	@retval none
*/
void mpris_configure(const char* uuid, const char* friendly_name)
{
	// Init the MPRIS objects
	mpris_player_init();
	mpris_media_player_init(friendly_name);

	// Construct a unique name for this instance
	char name[256] = {0};
	strncpy(name, MPRIS_BASE_NAME, sizeof(name));

	// Create a copy of the UUID
	char safe_uuid[65] = {0};
	strncpy(safe_uuid, uuid, sizeof(safe_uuid)-1);

	for (size_t i = 0; i < sizeof(safe_uuid); i++)
	{
		// Replace '-' in UUID string with '_' for D-Bus compat
		if (safe_uuid[i] == '-')
			safe_uuid[i] = '_';
	}  

	// Concat base name with safe uuid string
	strncat(name, safe_uuid, sizeof(name)-1);

	// Start acquiring the name on the system D-Bus
	g_bus_own_name(G_BUS_TYPE_SYSTEM, name, G_BUS_NAME_OWNER_FLAGS_REPLACE, bus_acquired, name_acquired, name_lost, NULL, NULL);

	// Register callbacks to update player state on variable change
	upnp_transport_register_variable_listener(mpris_transport_variable_callback, NULL);
	upnp_control_register_variable_listener(mpris_control_variable_callback, NULL);
}