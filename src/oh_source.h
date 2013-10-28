/* oh_source.h - OpenHome Source definitions
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
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

#ifndef _OH_SOURCE_H
#define _OH_SOURCE_H

void oh_source_dump_product_scpd(void);
void oh_source_dump_info_scpd(void);
void oh_source_dump_time_scpd(void);
void oh_source_dump_playlist_scpd(void);

// Returned pointer not owned.
struct upnp_device_descriptor *oh_source_descriptor(const char *name, const char *uuid);

void oh_playlist_load(char *filename);

#endif /* _OH_SOURCE_H */
