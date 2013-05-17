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

#include <assert.h>
#include <string.h>
#include <stdlib.h>

struct cb_list {
	variable_changed_t callback;
	struct cb_list *next;
};

struct variable_container {
	int variable_num;
	const char **variable_names;
	char **values;
	struct cb_list *callbacks;
};
typedef struct variable_container variable_container_t;

struct variable_container *
VariableContainer_new(int variable_num,
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

void VariableContainer_delete(struct variable_container *object) {
	free(object->values);
	for (struct cb_list *list = object->callbacks; list; /**/) {
		struct cb_list *next = list->next;
		free(list);
		list = next;
	}
	free(object);
}

// Change content of variable with given number to NUL terminated content.
int VariableContainer_change(struct variable_container *object,
			     int var_num, const char *value) {
	assert(var_num >= 0 && var_num < object->variable_num);
	if (strcmp(value, object->values[var_num]) == 0)
		return 0;  // no change.
	char *old_value = object->values[var_num];
	char *new_value = strdup(value);
	object->values[var_num] = new_value;
	for (struct cb_list *it = object->callbacks; it; it = it->next) {
		it->callback(var_num, object->variable_names[var_num],
			     old_value, new_value);
	}
	free(old_value);
	return 1;
}

void VariableContainer_register_callback(struct variable_container *object,
					 variable_changed_t callback) {
	// Order is not guaranteed, so we just register it at the front.
	struct cb_list *item = (struct cb_list*) malloc(sizeof(struct cb_list));
	item->next = object->callbacks;
	item->callback = callback;
	object->callbacks = item;
}
