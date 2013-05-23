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
#include <ctype.h>

#include "upnp_device.h"
#include "xmlescape.h"
#include "xmldoc.h"

// -- VariableContainer
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

// Get number of variables.
int VariableContainer_get_num_vars(variable_container_t *object) {
	return object->variable_num;
}
// Get variable name/value. Any of the OUT parameters 'name' and 'value'
// can be NULL.
// Returns pointer to the value if variable exists.
const char *VariableContainer_get(variable_container_t *object,
				  int var, const char **name) {
	if (var < 0 || var >= object->variable_num)
		return NULL;
	const char *varname = object->variable_names[var];
	if (name) *name = varname;
	// Names of not used variables are set to NULL.
	return varname ? object->values[var] : NULL;
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

// -- UPnPLastChangeBuilder
struct upnp_last_change_builder {
	struct xmldoc *change_event_doc;
	struct xmlelement *instance_element;
};

upnp_last_change_builder_t *UPnPLastChangeBuilder_new(void) {
	upnp_last_change_builder_t *result = (upnp_last_change_builder_t*)
		malloc(sizeof(upnp_last_change_builder_t));
	result->change_event_doc = NULL;
	result->instance_element = NULL;
	return result;
}

void UPnPLastChangeBuilder_delete(upnp_last_change_builder_t *builder) {
	if (builder->change_event_doc != NULL) {
		xmldoc_free(builder->change_event_doc);
	}
	free(builder);
}

void UPnPLastChangeBuilder_add(upnp_last_change_builder_t *builder,
			       const char *name, const char *value) {
	assert(name != NULL);
	assert(value != NULL);
	if (builder->change_event_doc == NULL) {
		builder->change_event_doc = xmldoc_new();
		struct xmlelement *toplevel =
			xmldoc_new_topelement(builder->change_event_doc, "Event",
				     "urn:schemas-upnp-org:metadata-1-0/AVT/");
		// Right now, we only have exactly one instance.
		builder->instance_element =
			add_attributevalue_element(builder->change_event_doc,
						   toplevel,
						   "InstanceID", "val", "0");
	}
	add_attributevalue_element(builder->change_event_doc,
				   builder->instance_element,
				   name, "val", value);
}

char *UPnPLastChangeBuilder_to_xml(upnp_last_change_builder_t *builder) {
	if (builder->change_event_doc == NULL)
		return NULL;

	char *xml_document = xmldoc_tostring(builder->change_event_doc);
	// The XML-doc contains a header of the form <?xml version="1.0"?>
	// Apparently some players don't like that (foobar).
	char *end_pos = xml_document ? strstr(xml_document, "?>") : NULL;
	if (end_pos) {
		end_pos += 2;
		while (*end_pos && isspace(*end_pos))
			end_pos++;
		char *result = strdup(end_pos);
		free(xml_document);
		xml_document = result;
	}
	xmldoc_free(builder->change_event_doc);
	builder->change_event_doc = NULL;
	builder->instance_element = NULL;
	return xml_document;
}

// -- UPnPLastChangeCollector
struct upnp_last_change_collector {
	variable_container_t *variables;
	int last_change_variable_num;  // the variable we manipulate.
	struct upnp_device *upnp_device;
	const char *service_id;
	int open_transactions;
	upnp_last_change_builder_t *builder;
};

static void UPnPLastChangeCollector_notify(upnp_last_change_collector_t *obj);
static void UPnPLastChangeCollector_callback(void *userdata,
					     int var_num, const char *var_name,
					     const char *old_value,
					     const char *new_value);

upnp_last_change_collector_t *
UPnPLastChangeCollector_new(variable_container_t *variable_container,
			    int last_change_var_num,
			    struct upnp_device *upnp_device,
			    const char *service_id) {
	upnp_last_change_collector_t *result = (upnp_last_change_collector_t*)
		malloc(sizeof(upnp_last_change_collector_t));
	result->variables = variable_container;
	result->last_change_variable_num = last_change_var_num;
	result->upnp_device = upnp_device;
	result->service_id = service_id;
	result->open_transactions = 0;
	result->builder = UPnPLastChangeBuilder_new();
	VariableContainer_register_callback(variable_container,
					    UPnPLastChangeCollector_callback,
					    result);
	return result;
}

void UPnPLastChangeCollector_start(upnp_last_change_collector_t *object) {
	object->open_transactions += 1;
}

void UPnPLastChangeCollector_finish(upnp_last_change_collector_t *object) {
	assert(object->open_transactions >= 1);
	object->open_transactions -= 1;
	UPnPLastChangeCollector_notify(object);
}

// TODO(hzeller): add rate limiting. The standard talks about some limited
// amount of events per time-unit.
static void UPnPLastChangeCollector_notify(upnp_last_change_collector_t *obj) {
	if (obj->open_transactions != 0)
		return;

	char *xml_document = UPnPLastChangeBuilder_to_xml(obj->builder);
	if (xml_document == NULL)
		return;

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
				   obj->service_id,
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
		return;  // ignore change of LastChange variable.

	UPnPLastChangeBuilder_add(object->builder, var_name, new_value);
	UPnPLastChangeCollector_notify(object);
}
