/* gmrender_list.h - GList emulator
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
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

#ifndef _GMLIST_H
#define _GMLIST_H

typedef struct GmSList_s GmSList;
struct GmSList_s {
	void *data;
	GmSList *next;
};

typedef int (*GmCompareFunc)(const void *, const void *);

GmSList *gm_slist_prepend(GmSList *list, void *data);
GmSList *gm_slist_append(GmSList *list, void *data);
GmSList *gm_slist_insert_sorted(GmSList *list, void *data, GmCompareFunc cmp);
GmSList *gm_slist_find_custom(GmSList *list, const void *data, GmCompareFunc cmp);
GmSList *gm_slist_delete_link(GmSList *list, void *data);
#define gm_slist_next(entry) (entry->next)
#define gm_slist_free_full(list, free) do { \
	if (list == NULL) break; \
	GmSList *next = list->next; \
	free(list->data); \
	free(list); \
	list = next; \
} while(list != NULL)
#define gm_slist_foreach(list, func, udata) do { \
	if (list == NULL) break; \
	func(list->data, udata); \
	list = list->next; \
} while(list != NULL)

#endif
