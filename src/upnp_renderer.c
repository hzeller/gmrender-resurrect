/* upnp_renderer.c - UPnP renderer routines
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <upnp/upnp.h>
#include <upnp/ithread.h>

#include "webserver.h"
#include "upnp.h"
#include "upnp_device.h"
#include "upnp_connmgr.h"
#include "upnp_control.h"
#include "upnp_transport.h"

#include "upnp_renderer.h"
#include "git-version.h"

static struct icon icon1 = {
        .width =        64,
        .height =       64,
        .depth =        24,
        .url =          "/upnp/grender-64x64.png",
        .mimetype =     "image/png"
};
static struct icon icon2 = {
        .width =        128,
        .height =       128,
        .depth =        24,
        .url =          "/upnp/grender-128x128.png",
        .mimetype =     "image/png"
};

static struct icon *renderer_icon[] = {
        &icon1,
        &icon2,
        NULL
};

static int upnp_renderer_init(void);

static struct upnp_device_descriptor render_device = {
	.init_function          = upnp_renderer_init,
        .device_type            = "urn:schemas-upnp-org:device:MediaRenderer:1",
        .friendly_name          = "GMediaRender",
        .manufacturer           = "Ivo Clarysse, Henner Zeller",
        .manufacturer_url       = "http://github.com/hzeller/gmrender-resurrect",
        .model_description      = PACKAGE_STRING,
        .model_name             = PACKAGE_NAME,
        .model_number           = GM_COMPILE_VERSION,
        .model_url              = "http://github.com/hzeller/gmrender-resurrect",
        .serial_number          = "1",
        .udn                    = "uuid:GMediaRender-1_0-000-000-002",
        .upc                    = "",
        .presentation_url       = "",  // TODO(hzeller) show something useful.
        .icons                  = renderer_icon,
};

void upnp_renderer_dump_connmgr_scpd(void)
{
	char *buf;
	buf = upnp_get_scpd(upnp_connmgr_get_service());
	assert(buf != NULL);
	fputs(buf, stdout);
}
void upnp_renderer_dump_control_scpd(void)
{
	char *buf;
	buf = upnp_get_scpd(upnp_control_get_service());
	assert(buf != NULL);
	fputs(buf, stdout);
}
void upnp_renderer_dump_transport_scpd(void)
{
	char *buf;
	buf = upnp_get_scpd(upnp_transport_get_service());
	assert(buf != NULL);
	fputs(buf, stdout);
}

static int upnp_renderer_init(void)
{
	static struct service *upnp_services[4];
	upnp_services[0] = upnp_transport_get_service();
	upnp_services[1] = upnp_connmgr_get_service();
	upnp_services[2] = upnp_control_get_service();
	upnp_services[3] = NULL;
	render_device.services = upnp_services;
        return connmgr_init();
}

struct upnp_device_descriptor *
upnp_renderer_descriptor(const char *friendly_name,
			 const char *uuid)
{
	render_device.friendly_name = friendly_name;

	char *udn = NULL;
	if (asprintf(&udn, "uuid:%s", uuid) > 0) {
		render_device.udn = udn;
	}
	return &render_device;
}
