/* playlist.h - Playlist utilities
 *
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

#ifndef _PLAYLIST_H
#define _PLAYLIST_H

#include <stdint.h>

struct playlist;

typedef uint32_t playlist_id_t;

typedef void (*playlist_current_change_listener_t)(struct playlist *list, playlist_id_t id, int index, int automatic);
typedef void (*playlist_current_remove_listener_t)(struct playlist *list);
typedef void (*playlist_next_change_listener_t)(struct playlist *list, playlist_id_t id, int index);


struct playlist *playlist_create(void);

void playlist_set_current_change_listener(struct playlist *list, playlist_current_change_listener_t l);
void playlist_set_current_remove_listener(struct playlist *list, playlist_current_remove_listener_t l);
void playlist_set_next_change_listener(struct playlist *list, playlist_next_change_listener_t l);


int playlist_add(struct playlist *list, playlist_id_t after_id, char *uri, char *metadata, playlist_id_t *assigned_id);
int playlist_remove(struct playlist *list, playlist_id_t id);
playlist_id_t playlist_current_id(struct playlist *list);
int playlist_clear(struct playlist *list);
void playlist_set_shuffle(struct playlist *list, int shuffle);
void playlist_set_repeat(struct playlist *list, int repeat);
int playlist_get(struct playlist *list, playlist_id_t id, char **uri, char **metadata);
playlist_id_t playlist_current_id(struct playlist *list);
void playlist_next(struct playlist *list, int automatic);
void playlist_prev(struct playlist *list);

int playlist_set_current_id(struct playlist *list, playlist_id_t id);
int playlist_set_current_index(struct playlist *list, int index);

int playlist_get_size(struct playlist *list);
playlist_id_t * playlist_get_ids(struct playlist *list);

int playlist_next_uri(struct playlist *list, char **uri);
uint32_t playlist_get_token(struct playlist *list);


#endif /* _PLAYLIST_H */
