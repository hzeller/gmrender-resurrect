/* shared_metadata.h - Current song metedata change notification
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
#ifndef _SHARED_METADATA_H
#define _SHARED_METADATA_H

#include <stdint.h>
#include "song-meta-data.h"

struct shared_metadata;

typedef void (*shared_meta_song_change_t)(char *uri, char *metadata);
typedef void (*shared_meta_metadata_change_t)(char *metadata);
typedef void (*shared_meta_time_change_t)(uint32_t total, uint32_t current);


void shared_meta_song_add_listener(struct shared_metadata *sm, shared_meta_song_change_t l);
void shared_meta_song_remove_listener(struct shared_metadata *sm, shared_meta_song_change_t l);
void shared_meta_meta_add_listener(struct shared_metadata *sm, shared_meta_metadata_change_t l);
void shared_meta_meta_remove_listener(struct shared_metadata *sm, shared_meta_metadata_change_t l);
void shared_meta_time_add_listener(struct shared_metadata *sm, shared_meta_time_change_t l);
void shared_meta_time_remove_listener(struct shared_metadata *sm, shared_meta_time_change_t l);

void shared_meta_song_notify(struct shared_metadata *sm, char *uri, char *metadata);
void shared_meta_meta_notify(struct shared_metadata *sm, char *metadata);
void shared_meta_time_notify(struct shared_metadata *sm, uint32_t total, uint32_t current);


#endif /* _SHARED_METADATA_H */
