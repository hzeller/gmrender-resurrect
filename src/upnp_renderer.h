/* upnp_renderer.h - UPnP renderer definitions
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
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

#ifndef _UPNP_RENDERER_H
#define _UPNP_RENDERER_H

void upnp_renderer_dump_connmgr_scpd(void);
void upnp_renderer_dump_control_scpd(void);
void upnp_renderer_dump_transport_scpd(void);

// Returned pointer not owned.
struct upnp_device_descriptor *upnp_renderer_descriptor(const char *name,
							const char *uuid);

#endif /* _UPNP_RENDERER_H */
