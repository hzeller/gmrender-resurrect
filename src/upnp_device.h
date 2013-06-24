/* upnp_device.h - Generic UPnP Device handler
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

#ifndef _UPNP_DEVICE_H
#define _UPNP_DEVICE_H


struct upnp_device_descriptor {
	int (*init_function) (void);
        const char *device_type;
        const char *friendly_name;
        const char *manufacturer;
        const char *manufacturer_url;
        const char *model_description;
        const char *model_name;
        const char *model_number;
        const char *model_url;
        const char *serial_number;
        const char *udn;
        const char *upc;
        const char *presentation_url;
	struct icon **icons;
	struct service **services;
};

// ..  and this 'device'. This is an opaque type containing internals.
struct upnp_device;
struct action_event;

struct upnp_device *upnp_device_init(struct upnp_device_descriptor *device_def,
				     const char *ip_address,
				     unsigned short port);

void upnp_device_shutdown(struct upnp_device *device);

int upnp_add_response(struct action_event *event,
		      const char *key, const char *value);
void upnp_set_error(struct action_event *event, int error_code,
                    const char *format, ...);

// Returns a writable copy of the value.
char *upnp_get_string(struct action_event *event, const char *key);

// Append variable, identified by the variable number, to the event,
// store the value under the given parameter name. The caller needs to provide
// a valid variable number (assert()-ed).
void upnp_append_variable(struct action_event *event,
                          int varnum, const char *paramname);

int upnp_device_notify(struct upnp_device *device,
		       const char *serviceID,
		       const char **varnames,
		       const char **varvalues,
		       int varcount);

struct service *find_service(struct upnp_device_descriptor *device_def,
                             const char *service_name);

// Returns a newly allocated string with the device descriptor.
char *upnp_create_device_desc(struct upnp_device_descriptor *device_def);

#endif /* _UPNP_DEVICE_H */
