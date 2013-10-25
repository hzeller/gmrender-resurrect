/* playlist.c - Playlist utilities
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
#include <string.h>

#include "playlist.h"

#define ALLOC_STEP 50

typedef struct {
	char *uri;
	char *metadata;
} playlist_item;


struct playlist {
	int next_assigned_id;
	int list_size;
	int list_allocated;
	int shuffle;
	int repeat;
	playlist_id_t *ids;
	playlist_item *items;

	int current_index;
	int next_index;
	uint32_t token;

	playlist_list_change_listener_t list_change_listener;
	playlist_current_change_listener_t current_change_listener;
	playlist_current_remove_listener_t current_remove_listener;
	playlist_next_change_listener_t next_change_listener;
};

static void ensure_capacity(struct playlist *list, int size)
{
	if (list->list_allocated >= size)
		return;
	if (list->items == NULL) {
		list->ids = malloc(sizeof(playlist_id_t) * (ALLOC_STEP + list->list_allocated));
		assert(list->ids != NULL);
		list->items = malloc(sizeof(playlist_item) * (ALLOC_STEP + list->list_allocated));
		assert(list->items != NULL);
	} else {
		list->ids = realloc(list->ids, sizeof(playlist_id_t) * (ALLOC_STEP + list->list_allocated));
		assert(list->ids != NULL);
		list->items = realloc(list->items, sizeof(playlist_item) * (ALLOC_STEP + list->list_allocated));
		assert(list->items != NULL);
	}
	list->list_allocated += ALLOC_STEP;
}

static int find_id_in_list(playlist_id_t *ids, int size, playlist_id_t id)
{
	if (ids == NULL)
		return -1;

	int i;
	for (i = 0; i < size; i++) {
		if (ids[i] == id)
			return i;
	}
	return -1;
}


static void assign_next(struct playlist *list)
{
	int save = list->next_index;
	if (list->current_index < 0) {
		list->next_index = -1;
	} else {
		if (list->current_index < list->list_size - 1) {
			list->next_index = list->current_index + 1;
		} else {
			if (list->shuffle) {
				list->next_index = 0;
			} else {
				list->next_index = -1;
			}
		}
	}
	if (save != list->next_index) {
		if (list->next_change_listener != NULL) {
			if (list->next_index < 0) {
				list->next_change_listener(list, 0, -1);
			} else {
				list->next_change_listener(list, list->ids[list->next_index], list->next_index);
			}
		}
	}
}

struct playlist *playlist_create(void)
{
	struct playlist *list = malloc(sizeof(struct playlist));
	assert(list != NULL);
	memset(list, 0, sizeof(struct playlist));
	list->next_assigned_id = 1;
	list->current_index = -1;
	list->next_index = -1;
	list->token = 1;
	return list;
}

void playlist_set_list_change_listener(struct playlist *list, playlist_list_change_listener_t l)
{
	list->list_change_listener = l;
}

void playlist_set_current_change_listener(struct playlist *list, playlist_current_change_listener_t l)
{
	list->current_change_listener = l;
}

void playlist_set_current_remove_listener(struct playlist *list, playlist_current_remove_listener_t l)
{
	list->current_remove_listener = l;
}

void playlist_set_next_change_listener(struct playlist *list, playlist_next_change_listener_t l)
{
	list->next_change_listener = l;
}

int playlist_add(struct playlist *list, playlist_id_t after_id, char *uri, char *metadata, playlist_id_t *assigned_id)
{
	assert(list != NULL);
	int after_index;
	if (!after_id) {
		after_index = -1;
	} else {
		after_index = find_id_in_list(list->ids, list->list_size, after_id);
		if (after_index < 0)
			return 1;
	}
	ensure_capacity(list, list->list_size + 1);

	if (after_index < list->list_size - 1) {
		memmove(list->ids + after_index + 2, list->ids + after_index + 1, sizeof(playlist_id_t) * (list->list_size - after_index - 1));
		memmove(list->items + after_index + 2, list->items + after_index + 1, sizeof(playlist_item) * (list->list_size - after_index - 1));
	}

	list->ids[after_index + 1] = list->next_assigned_id;
	list->items[after_index + 1].uri = uri;
	list->items[after_index+1].metadata = metadata;
	if (assigned_id != NULL)
		*assigned_id = list->next_assigned_id;

	list->next_assigned_id++;
	list->list_size++;
	list->token++;

	if (list->next_index > after_index) {
		list->next_index++;
	}
		
	if (list->list_change_listener != NULL) {
		list->list_change_listener(list);
	}

	if (list->current_index < 0) {
		//list->current_index = after_index + 1;
		if (list->current_change_listener != NULL) {
			//list->current_change_listener(list, list->ids[list->current_index], list->current_index, 0);
			list->current_change_listener(list, 0, -1, 0);
		}
	} else if (list->current_index > after_index) {
		list->current_index++;
	}
	assign_next(list);
	return 0;
}

int playlist_clear(struct playlist *list)
{
	assert(list != NULL);
	int i;
	for (i = 0; i < list->list_size; i++) {
		free(list->items[i].uri);
		free(list->items[i].metadata);
	}
	list->list_size = 0;
	list->token++;
	if (list->current_index >= 0) {
		
		list->current_index = -1;

		if (list->list_change_listener != NULL) {
			list->list_change_listener(list);
		}

		if (list->current_remove_listener != NULL) {
			list->current_remove_listener(list);
		}

		if (list->current_change_listener != NULL) {
			list->current_change_listener(list, 0, -1, 0);
		}
		assign_next(list);
	}
	return 0;
}

int playlist_remove(struct playlist *list, playlist_id_t id)
{
	assert(list != NULL);
	const int idx = find_id_in_list(list->ids, list->list_size, id);
	if (idx < 0)
		return 1;
		
	free(list->items[idx].uri);
	free(list->items[idx].metadata);

	if (idx < list->list_size - 1) {
		memmove(list->ids + idx, list->ids + idx + 1, sizeof(playlist_id_t) * (list->list_size - idx - 1));
		memmove(list->items + idx, list->items + idx + 1, sizeof(playlist_item) * (list->list_size - idx - 1));
	}
	list->token++;
	list->list_size--;


	if (idx == list->current_index) {
		if (list->current_remove_listener != NULL) {
			list->current_remove_listener(list);
		}
		if (list->list_size) {
			list->current_index = 0;
		} else {
			list->current_index = -1;
		}

		if (list->current_change_listener != NULL) {
			if (list->list_size) {
				list->current_change_listener(list, list->ids[0], 0, 0);
			} else {
				list->current_change_listener(list, 0, -1, 0);
			}
		}
		assign_next(list);
	} else {
		if (list->current_index > idx) {
			list->current_index--;
		}
		if (list->next_index > idx) {
			list->next_index--;
		}
	}
	if (list->list_change_listener != NULL) {
		list->list_change_listener(list);
	}

	return 0;
}

int playlist_get_size(struct playlist *list)
{
	return list->list_size;
}

playlist_id_t * playlist_get_ids(struct playlist *list)
{
	return list->ids;
}

int playlist_get(struct playlist *list, playlist_id_t id, char **uri, char **metadata)
{
	assert(list != NULL);
	int idx = find_id_in_list(list->ids, list->list_size, id);
	if (idx < 0)
		return 1;
	if (uri != NULL)
		*uri = list->items[idx].uri;
	if (metadata != NULL)
		*metadata = list->items[idx].metadata;
	return 0;
}

int playlist_set_current_index(struct playlist *list, int index, int play_after_set)
{
	assert(list != NULL);
	if (index < 0 || index >= list->list_size)
		return 1;
	if (index != list->current_index || play_after_set) {
		list->current_index = index;
		if (list->current_change_listener != NULL) {
			list->current_change_listener(list, list->ids[index], index, play_after_set);
		}
		assign_next(list);
	}
	return 0;
}

int playlist_set_current_id(struct playlist *list, playlist_id_t id, int play_after_set)
{
	assert(list != NULL);
	int index = find_id_in_list(list->ids, list->list_size, id);
	return playlist_set_current_index(list, index, play_after_set);
}

void playlist_next(struct playlist *list, int automatic)
{
	assert(list != NULL);
	if (list->next_index >= 0) {
		list->current_index = list->next_index;
		if (list->current_change_listener != NULL) {
			if (list->current_index >= 0) {
				list->current_change_listener(list, list->ids[list->current_index], list->current_index, automatic);
			} else {
				list->current_change_listener(list, 0, -1, automatic);
			}
		}
		assign_next(list);
	}
}

void playlist_prev(struct playlist *list)
{
	assert(list != NULL);
	if (list->list_size > 0) {
		int prev = list->current_index - 1;
		if (prev < 0)
			return;
		list->current_index = prev;
		if (list->current_change_listener != NULL) {
			list->current_change_listener(list, list->ids[list->current_index], list->current_index, 0);
		}
	}
	assign_next(list);
}

playlist_id_t playlist_current_id(struct playlist *list)
{
	assert(list != NULL);
	if (list->current_index < 0)
		return 0;
	return list->ids[list->current_index];
}

uint32_t playlist_get_token(struct playlist *list)
{
	assert(list != NULL);
	return list->token;
}
