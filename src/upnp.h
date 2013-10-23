/* upnp.h - Generic UPnP definitions
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

#ifndef _UPNP_H
#define _UPNP_H

struct action;
struct service;
struct action_event;
struct variable_container;
struct upnp_last_change_collector;

struct action {
	const char *action_name;
	int (*callback) (struct action_event *);
};

typedef enum {
        PARAM_DIR_IN,
        PARAM_DIR_OUT,
} param_dir;

struct argument {
        const char *name;
        param_dir direction;
        int statevar;
};

typedef enum {
        DATATYPE_STRING,
        DATATYPE_BOOLEAN,
        DATATYPE_I2,
        DATATYPE_I4,
        DATATYPE_UI2,
        DATATYPE_UI4,
        DATATYPE_UNKNOWN,
        DATATYPE_COUNT
} param_datatype;

typedef enum {
        SENDEVENT_NO,
        SENDEVENT_YES
} param_event;

struct param_range {
        long long min;
        long long max;
        long long step;
};

struct var_meta {
        param_event     sendevents;
        param_datatype  datatype;
        const char      **allowed_values;
        struct param_range      *allowed_range;
	const char      *default_value;
};


struct icon {
        int width;
        int height;
        int depth;
        const char *url;
        const char *mimetype;
};

struct service {
	ithread_mutex_t *service_mutex;
	const char *service_id;
	const char *service_type;
	const char *scpd_url;
	const char *control_url;
	const char *event_url;
	const char *event_xml_ns;
	struct action *actions;
	struct argument ***action_arguments;
	const char **variable_names;
	struct variable_container *variable_container;
	struct upnp_last_change_collector *last_change;
	struct var_meta *variable_meta;
	int variable_count;
	int command_count;
	const char **eventvar_names;
	const char **eventvar_values;
};

struct action_event {
	struct Upnp_Action_Request *request;
	int status;
	struct service *service;
	struct upnp_device *device;
};

struct action *find_action(struct service *event_service,
                                  char *action_name);

char *upnp_get_scpd(struct service *srv);

#endif /* _UPNP_H */
