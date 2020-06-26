/* output_mpg123.c - Output module for mpg123
 *
 * Copyright (C) 2014-2019   Mar Chalain
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>

#include <upnp/ithread.h>

#include <mpg123.h>

#include <tinyalsa/asoundlib.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "output_module.h"
#include "sound_module.h"

#include "logging.h"
#include "upnp_connmgr.h"

#include "webclient.h"

enum e_state
{
	STOPPED,
	PLAYING,
	PAUSING,
	HALTED,
};
enum e_state g_state = STOPPED;

typedef struct st_output_mpg123_uri output_mpg123_uri_t;
struct st_output_mpg123_uri
{
	char *uri;
	enum e_state state;
	size_t position;
	struct http_info info;
	output_mpg123_uri_t *next;
};


static output_mpg123_uri_t *g_first_uri;
static output_mpg123_uri_t *g_current_uri;
static output_transition_cb_t g_callback;

static mpg123_pars *g_mpg123_pars;
static mpg123_handle *g_mpg123_handle = NULL;

static pthread_mutex_t g_mutex_control;
static pthread_cond_t g_cond_control;

static const char *g_cmd_mime = "audio/mpeg";
static const struct sound_module *g_sound_api;

static int
output_mpg123_init(void)
{
	int ret = -1;

	if (g_cmd_mime)
	{
		const char *mime = g_cmd_mime;
		const char *nextmime = mime;
		for (;*mime; mime++)
		{
			if (*mime == ',')
			{
				*(char *)mime = '\0';
				register_mime_type(nextmime);
				nextmime = mime + 1;
			}
		}
		register_mime_type(nextmime);
	}
	pthread_mutex_init(&g_mutex_control, NULL);
	pthread_cond_init(&g_cond_control, NULL);

	ret = mpg123_init();
	g_mpg123_pars = mpg123_new_pars(&ret);
	const char **decoderslist = mpg123_decoders();
	g_mpg123_handle = mpg123_new(decoderslist[0], &ret);

	if (!ret)
	{
		g_sound_api = sound_module_get();
		if (g_sound_api == NULL)
		{
			Log_error("mpg123", "sound module not found");
			ret = -1;
		}
	}
	return ret;
}

static void
output_mpg123_set_uri(const char *uri,
				     output_update_meta_cb_t meta_cb)
{
	struct st_output_mpg123_uri *entry = malloc(sizeof(*entry));
	memset(entry, 0 ,sizeof(*entry));
	entry->uri = strdup(uri);
	if (g_state == PLAYING && g_current_uri != NULL)
	{
		g_current_uri->next = entry;
	}
	else
	{
		entry->next = g_first_uri;
		g_first_uri = entry;
	}
};

static void
output_mpg123_set_next_uri(const char *uri)
{
	struct st_output_mpg123_uri *entry = malloc(sizeof(*entry));
	memset(entry, 0 ,sizeof(*entry));
	entry->uri = strdup(uri);
	if (g_first_uri == NULL)
	{
		g_first_uri = entry;
	}
	else
	{
		struct st_output_mpg123_uri *it = g_first_uri;
		while (it->next != NULL) it = it->next;
		it->next = entry;
	}
}

static int
output_mpg123_openstream(int fdin, int *channels, int *encoding, long *rate, long *buffsize)
{
	if(mpg123_open_fd(g_mpg123_handle, fdin) != MPG123_OK)
	{
		return -1;
	}

	if (mpg123_getformat(g_mpg123_handle, rate, channels, encoding) != MPG123_OK)
	{
		return -1;
	}
	mpg123_format_none(g_mpg123_handle);
	mpg123_format(g_mpg123_handle, *rate, *channels, *encoding);

	*buffsize = mpg123_outblock(g_mpg123_handle);
	return 0;
}

static void*
thread_play(void *arg)
{
	while (g_state != HALTED)
	{
		pthread_mutex_lock(&g_mutex_control);
		while (g_state == STOPPED)
		{
			pthread_cond_wait(&g_cond_control, &g_mutex_control);
		}
		pthread_mutex_unlock(&g_mutex_control);
		if (g_current_uri == NULL || g_current_uri->uri == NULL)
			continue;

		int fdin = http_get(g_current_uri->uri, &g_current_uri->info);
		if (fdin < 0)
			break;

		int  channels = 0, encoding = 0;
		long rate = 0, buffsize = 0;
		if (output_mpg123_openstream(fdin, &channels, &encoding, &rate, &buffsize))
		{
			break;
		}
		unsigned char *buffer;
		buffer = malloc(buffsize);

		g_sound_api->open(channels, encoding, rate);

		int err = MPG123_OK;
		do
		{
			pthread_mutex_lock(&g_mutex_control);
			while (g_state == PAUSING)
			{
				pthread_cond_wait(&g_cond_control, &g_mutex_control);
			}
			/**
			 * stop is requested from the controler
			 **/
			if (g_state == STOPPED)
			{
				g_current_uri->position = 0;
				pthread_mutex_unlock(&g_mutex_control);
				break;
			}
			pthread_mutex_unlock(&g_mutex_control);
			size_t done = 0;
			err = mpg123_read( g_mpg123_handle, buffer, buffsize, &done );
			g_current_uri->position += done;
			if (err == MPG123_OK)
			{
				err = (g_sound_api->write(buffer, buffsize) >= 0)? MPG123_OK : MPG123_ERR;
			}
		} while (err == MPG123_OK);
		mpg123_close(g_mpg123_handle);
		g_sound_api->close();

		struct st_output_mpg123_uri *it = g_first_uri;
		if (it == g_current_uri)
		{
			g_first_uri = g_first_uri->next;
		}
		else
		{
			while (it->next != g_current_uri) it = it->next;
			it->next = it->next->next;
		}

		g_current_uri->position = g_current_uri->info.length;
		/**
		 * prepare the next stream
		 **/
		struct st_output_mpg123_uri *entry = g_current_uri->next;
		free(g_current_uri->uri);
		free(g_current_uri);
		if (!entry)
		{
			(*g_callback)(PLAY_STOPPED);
			g_current_uri = NULL;
		}
		else
		{
			g_current_uri = entry;
			g_current_uri->position = 0;
			(*g_callback)(PLAY_STARTED_NEXT_STREAM);
		}
	}
	return NULL;
}

/**
static int
output_mpg123_loop()
{
	thread_play(NULL);
	return 0;
}
**/

static int
output_mpg123_play(output_transition_cb_t callback)
{
	g_callback = callback;
	g_state = PLAYING;
	if (!g_current_uri)
	{
		struct st_output_mpg123_uri *entry = g_first_uri;
		if (entry)
		{
			g_current_uri = entry;
			g_current_uri->position = 0;

			pthread_t thread;
			pthread_create(&thread, NULL, thread_play, NULL);
		}
	}
	pthread_cond_signal(&g_cond_control);
	return 0;
}

static int
output_mpg123_stop(void)
{
	g_state = STOPPED;
	pthread_cond_signal(&g_cond_control);
	return 0;
}

static int
output_mpg123_pause(void)
{
	g_state = PAUSING;
	pthread_cond_signal(&g_cond_control);
	return 0;
}

static int
output_mpg123_seek(int64_t position_nanos)
{
	return 0;
}

static int
output_mpg123_get_position(int64_t *track_duration,
					 int64_t *track_pos)
{
	if (g_current_uri == NULL)
	{
		*track_duration = 0;
		*track_pos = 0;
	}
	else
	{
		*track_duration = g_current_uri->info.length;
		*track_pos = g_current_uri->position;
	}
	return 0;
}

static int
output_mpg123_getvolume(float *value)
{
	if (g_sound_api->get_volume)
		return g_sound_api->get_volume(value);
	return 0;
}
static int
output_mpg123_setvolume(float value)
{
	if (g_sound_api->set_volume)
		return g_sound_api->set_volume(value);
	return 0;
}
static int
output_mpg123_getmute(int *value)
{
	if (g_sound_api->get_mute)
		return g_sound_api->get_mute(value);
	return 0;
}
static int
output_mpg123_setmute(int value)
{
	if (g_sound_api->set_mute)
		return g_sound_api->set_mute(value);
	return 0;
}


struct output_module mpg123_output = {
    .shortname = "mpg123",
	.description = "daemon framework",
	.init        = output_mpg123_init,
	.set_uri     = output_mpg123_set_uri,
	.set_next_uri= output_mpg123_set_next_uri,
	.play        = output_mpg123_play,
	.stop        = output_mpg123_stop,
	.pause       = output_mpg123_pause,
	.seek        = output_mpg123_seek,
	.get_position = output_mpg123_get_position,
	.get_volume  = output_mpg123_getvolume,
	.set_volume  = output_mpg123_setvolume,
	.get_mute  = output_mpg123_getmute,
	.set_mute  = output_mpg123_setmute,
};

void output_mpg123_initlib(void) __attribute__((constructor));

void output_mpg123_initlib(void)
{
	output_append_module(&mpg123_output);
}
