/*
 * Copyright (C) 2013 Henner Zeller
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

#include "variable-container.h"

#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "upnp_device.h"
#include "xmlescape.h"
#include "xmldoc.h"

struct cb_list {
	variable_change_listener_t callback;
	void *userdata;
	struct cb_list *next;
};

struct variable_container {
	int variable_num;
	const char **variable_names;
	char **values;
	struct cb_list *callbacks;
};

variable_container_t *VariableContainer_new(int variable_num,
					    const char **variable_names,
					    const char **variable_init_values) {
	assert(variable_num > 0);
	variable_container_t *result
		= (variable_container_t*)malloc(sizeof(variable_container_t));
	result->variable_num = variable_num;
	result->variable_names = variable_names;
	result->values = (char **) malloc(variable_num * sizeof(char*));
	result->callbacks = NULL;
	for (int i = 0; i < variable_num; ++i) {
		result->values[i] = strdup(variable_init_values[i]
					   ? variable_init_values[i]
					   : "");
	}
	return result;
}

void VariableContainer_delete(variable_container_t *object) {
	for (int i = 0; i < object->variable_num; ++i) {
		free(object->values[i]);
	}
	free(object->values);

	for (struct cb_list *list = object->callbacks; list; /**/) {
		struct cb_list *next = list->next;
		free(list);
		list = next;
	}
	free(object);
}

const char **VariableContainer_get_values_hack(variable_container_t *object) {
	return (const char **) object->values;
}

// Change content of variable with given number to NUL terminated content.
int VariableContainer_change(variable_container_t *object,
			     int var_num, const char *value) {
	assert(var_num >= 0 && var_num < object->variable_num);
	if (value == NULL) value = "";
	if (strcmp(value, object->values[var_num]) == 0)
		return 0;  // no change.
	char *old_value = object->values[var_num];
	char *new_value = strdup(value);
	object->values[var_num] = new_value;
	for (struct cb_list *it = object->callbacks; it; it = it->next) {
		it->callback(it->userdata,
			     var_num, object->variable_names[var_num],
			     old_value, new_value);
	}
	free(old_value);
	return 1;
}

void VariableContainer_register_callback(variable_container_t *object,
					 variable_change_listener_t callback,
					 void *userdata) {
	// Order is not guaranteed, so we just register it at the front.
	struct cb_list *item = (struct cb_list*) malloc(sizeof(struct cb_list));
	item->next = object->callbacks;
	item->userdata = userdata;
	item->callback = callback;
	object->callbacks = item;
}

struct upnp_last_change_collector {
	variable_container_t *variables;
	int last_change_variable_num;  // the variable we manipulate.
	struct upnp_device *upnp_device;
	const char *service_name;
	int open_transactions;
	struct xmldoc *change_event_doc;
	struct xmlelement *instance_element;
};

// TODO(hzeller): add rate limiting. The standard talks about some limited
// amount of events per time-unit.
static void UPnPLastChangeCollector_notify(upnp_last_change_collector_t *obj) {
	if (obj->open_transactions != 0)
		return;

	if (obj->change_event_doc == NULL)
		return;

	char *xml_document = xmldoc_tostring(obj->change_event_doc);
	xmldoc_free(obj->change_event_doc);
	obj->change_event_doc = NULL;
	obj->instance_element = NULL;

	// Only if there is actually a change, send it over.
	if (VariableContainer_change(obj->variables,
				     obj->last_change_variable_num,
				     xml_document)) {
		const char *varnames[] = {
			"LastChange",
			NULL
		};
		const char *varvalues[] = {
			NULL, NULL
		};
		// Yes, now, the whole XML document is encapsulated in
		// XML so needs to be XML quoted. The time around 2000 was
		// pretty sick - people did everything in XML.
		varvalues[0] = xmlescape(xml_document, 0);
		upnp_device_notify(obj->upnp_device,
				   obj->service_name,
				   varnames, varvalues, 1);
		free((char*)varvalues[0]);
	}

	free(xml_document);
}

// The actual callback collecting changes by building an <Event/> XML document.
// This is not very robust if in the same transaction, we get the same variable
// changed twice -- it emits two changes.
static void UPnPLastChangeCollector_callback(void *userdata,
					     int var_num, const char *var_name,
					     const char *old_value,
					     const char *new_value) {
	upnp_last_change_collector_t *object = 
		(upnp_last_change_collector_t*) userdata;
	if (var_num == object->last_change_variable_num)
		return;  // ignore self change :)

	if (object->change_event_doc == NULL) {
		object->change_event_doc = xmldoc_new();
		struct xmlelement *toplevel =
			xmldoc_new_topelement(object->change_event_doc, "Event",
				     "urn:schemas-upnp-org:metadata-1-0/AVT/");
		// Right now, we only have exactly one instance.
		object->instance_element =
			add_attributevalue_element(object->change_event_doc,
						   toplevel,
						   "InstanceID", "val", "0");
	}
	add_attributevalue_element(object->change_event_doc,
				   object->instance_element,
				   var_name, "val", new_value);
	UPnPLastChangeCollector_notify(object);
}

upnp_last_change_collector_t *
UPnPLastChangeCollector_new(variable_container_t *variable_container,
			    int last_change_var_num,
			    struct upnp_device *upnp_device,
			    const char *service_name) {
	upnp_last_change_collector_t *result = (upnp_last_change_collector_t*)
		malloc(sizeof(upnp_last_change_collector_t));
	result->variables = variable_container;
	result->last_change_variable_num = last_change_var_num;
	result->upnp_device = upnp_device;
	result->service_name = service_name;
	result->open_transactions = 0;
	result->change_event_doc = NULL;
	result->instance_element = NULL;
	VariableContainer_register_callback(variable_container,
					    UPnPLastChangeCollector_callback,
					    result);
	return result;
}

void UPnPLastChangeCollector_start_transaction(
	   upnp_last_change_collector_t *object) {
	assert(object->open_transactions == 0);
	object->open_transactions = 1;
}

void UPnPLastChangeCollector_commit(upnp_last_change_collector_t *object) {
	assert(object->open_transactions == 1);
	object->open_transactions = 0;
	UPnPLastChangeCollector_notify(object);	
}
