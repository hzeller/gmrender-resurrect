/* sound_module.h - Audio sink module
 *
 * Copyright (C) 2014-2019   Marc Chalain
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

#ifndef _SOUND_MODULE_H
#define _SOUND_MODULE_H
struct sound_module
{
	const char *name;
	int (*open)(int channels, int encoding, unsigned int rate);
	ssize_t (*write)(unsigned char *buffer, ssize_t size);
	int (*close)(void);
	int (*get_volume)(float *);
	int (*set_volume)(float);
	int (*get_mute)(int *);
	int (*set_mute)(int);
};

const struct sound_module *sound_module_get(void);

extern const struct sound_module *g_sound_alsa;

#endif
