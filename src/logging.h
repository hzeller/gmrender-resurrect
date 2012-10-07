/* logging.h - Logging facility
 *
 * Copyright (C) 2007   Ivo Clarysse
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

#ifndef _LOGGING_H
#define _LOGGING_H

#ifdef ENABLE_TRACING
#define ENTER() do { fprintf(stderr, "%s: ENTER\n", __FUNCTION__); } while(0)
#define LEAVE() do { fprintf(stderr, "%s: LEAVE\n", __FUNCTION__); } while(0)
#else
#define ENTER() do { } while(0)
#define LEAVE() do { } while(0)
#endif

#endif /* _LOGGING_H */
