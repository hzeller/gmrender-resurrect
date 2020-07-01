/* gmrender_list.c - GList emulator
 *
 * Copyright (C) 2020   Marc Chalain
 *
 * Adapted to gstreamer-0.10 2006 David Siorpaes
 * Adapted to output to snapcast 2017 Daniel Jäcksch
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "gmrender_list.h"
#include "logging.h"

GmSList *gm_slist_prepend(GmSList *list, void *data)
{
	GmSList *new = calloc(1, sizeof(*new));
	new->data = data;
	new->next = list;
	return new;
}

GmSList *gm_slist_append(GmSList *list, void *data)
{
	GmSList *new = calloc(1, sizeof(*new));
	new->data = data;
	if (list == NULL)
		return new;

	GmSList *it = list;
	while (it->next != NULL) it = it->next;
	it->next = new;
	return list;
}

GmSList *gm_slist_insert_sorted(GmSList *list, void *data, GmCompareFunc cmp)
{
	GmSList *it = list;
	GmSList *prev;
	if (list == NULL) {
		list = gm_slist_append(list, data);
		return list;
	}
	prev = NULL;
	while (it != NULL && cmp(data,it->data) > 0) {
		prev = it;
		it = it->next;
	}
	if (it != NULL && prev == NULL)
		list = gm_slist_prepend(it, data);
	else if (it != NULL)
		prev->next = gm_slist_prepend(it, data);
	else
		list = gm_slist_append(list, data);
	return list;
}

GmSList *gm_slist_find_custom(GmSList *list, const void *data, GmCompareFunc cmp)
{
	GmSList *it = list;

	while (it != NULL && cmp(data,it->data) != 0) it = it->next;
	if (it != NULL)
		return it;
	return NULL;
}

GmSList *gm_slist_delete_link(GmSList *list, void *data)
{
	if (list == NULL)
		return list;
	GmSList *it = list;
	while (it->next != NULL && it->next->data != data) it = it->next;
	if (it->next != NULL) {
		GmSList *next = it->next;
		it->next = it->next->next;
		free(next);
	}
	else if (list->data == data) {
		free(list);
		list = NULL;
	}

	return list;
}
