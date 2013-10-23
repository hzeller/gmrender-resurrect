/* shared_metadata.c - Current song metedata change notification
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

#include <stdlib.h>
#include <assert.h>
#include <upnp/ithread.h>

#include "shared_metadata.h"

typedef struct shared_meta_callback_ {
	void *callback;
	struct shared_meta_callback_ *next;
} shared_meta_callback;

struct shared_metadata {
	shared_meta_callback *song_callbacks;
	shared_meta_callback *meta_callbacks;
	shared_meta_callback *time_callbacks;
	shared_meta_callback *details_callbacks;
	ithread_mutex_t mutex;
};


#define LIST_START_ITERATE(list, type)			\
	shared_meta_callback *cur = list;			\
	while (cur != NULL) {						\
		type callback = (type)cur->callback;

#define LIST_END_ITERATE() cur = cur->next; }

static void add_callback(shared_meta_callback **list, void *cb)
{
	assert(cb != NULL);
	shared_meta_callback *cur = *list;
	while (cur != NULL) {
		if (cur->callback == cb)
			break;
		cur = cur->next;
	}
	if (cur == NULL) {
		cur = malloc(sizeof(shared_meta_callback));
		assert(cur != NULL);
		cur->callback = cb;
		cur->next = *list;
		*list = cur;
	}
}

static void clear_list(shared_meta_callback *list)
{
	shared_meta_callback *next;
	while (list != NULL) {
		next = list->next;
		free(list);
		list = next;
	}
}

static void remove_callback(shared_meta_callback **list, void *cb)
{
	assert(cb != NULL);
	shared_meta_callback *cur = *list;
	shared_meta_callback *prev = NULL;
	while (cur != NULL) {
		if (cur->callback == cb)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur != NULL) {
		if (prev != NULL) {
			prev->next = cur->next;
		} else {
			*list = NULL;
		}
		free(cur);
	}
}

void shared_meta_song_add_listener(struct shared_metadata *sm, shared_meta_song_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	add_callback(&(sm->song_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_song_remove_listener(struct shared_metadata *sm, shared_meta_song_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	remove_callback(&(sm->song_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_meta_add_listener(struct shared_metadata *sm, shared_meta_metadata_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	add_callback(&(sm->meta_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_meta_remove_listener(struct shared_metadata *sm, shared_meta_metadata_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	remove_callback(&(sm->meta_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_time_add_listener(struct shared_metadata *sm, shared_meta_time_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	add_callback(&(sm->time_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_time_remove_listener(struct shared_metadata *sm, shared_meta_time_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	remove_callback(&(sm->time_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_details_add_listener(struct shared_metadata *sm, shared_meta_details_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	add_callback(&(sm->details_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_details_remove_listener(struct shared_metadata *sm, shared_meta_details_change_t l)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	remove_callback(&(sm->details_callbacks), l);
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_song_notify(struct shared_metadata *sm, char *uri, char *metadata)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	LIST_START_ITERATE(sm->song_callbacks, shared_meta_song_change_t);
	callback(uri, metadata);
	LIST_END_ITERATE();
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_meta_notify(struct shared_metadata *sm, char *metadata)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	LIST_START_ITERATE(sm->meta_callbacks, shared_meta_metadata_change_t);
	callback(metadata);
	LIST_END_ITERATE();
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_time_notify(struct shared_metadata *sm, uint32_t total, uint32_t current)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	LIST_START_ITERATE(sm->time_callbacks, shared_meta_time_change_t);
	callback(total, current);
	LIST_END_ITERATE();
	ithread_mutex_unlock(&(sm->mutex));
}

void shared_meta_details_notify(struct shared_metadata *sm, int channels, int bits, int rate)
{
	assert(sm != NULL);
	ithread_mutex_lock(&(sm->mutex));
	LIST_START_ITERATE(sm->details_callbacks, shared_meta_details_change_t);
	callback(channels, bits, rate);
	LIST_END_ITERATE();
	ithread_mutex_unlock(&(sm->mutex));
}

struct shared_metadata* shared_metadata_create(void)
{
	struct shared_metadata *sm = malloc(sizeof(struct shared_metadata));
	assert(sm != NULL);
	return sm;
}

void shared_metadata_free(struct shared_metadata *sm)
{
	if (sm != NULL) {
		clear_list(sm->song_callbacks);
		clear_list(sm->meta_callbacks);
		clear_list(sm->time_callbacks);
		clear_list(sm->details_callbacks);
	}
}
