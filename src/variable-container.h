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
 * -----------------
 *
 * Helpers for keeping track of server state variables. UPnP is about syncing
 * state between server and connected controllers and it does so by variables
 * (such as 'CurrentTrackDuration') that can be queried and whose changes
 * can be actively sent to parties that have registered for updates.
 * However, changes are not sent individually when a variable changes
 * but instead encapsulated in XML in a 'LastChange' variable, that contains
 * recent changes since the last update.
 *
 * These utility classes are here to help getting this done:
 *
 * variable_container - handling a bunch of variables containting NUL
 *   terminated strings, allowing C-callbacks to be called when content changes
 *   and differs from previous value.
 *
 * upnp_last_change_builder - a builder for the LastChange XML document
 *   containing recently changed variables.
 *
 * upnp_last_change_collector - handling of the LastChange variable in UPnP.
 *   Hooks into the callback mechanism of the variable_container to assemble
 *   the LastChange variable to be sent over (using the last change builder).
 *
 */ 
#ifndef VARIABLE_CONTAINER_H
#define VARIABLE_CONTAINER_H

// -- VariableContainer
struct variable_container;
typedef struct variable_container variable_container_t;

// Create a new variable container. The variable_names need to be valid for the
// lifetime of this objec.
variable_container_t *VariableContainer_new(int variable_num,
					    const char **variable_names,
					    const char **variable_init_values);
void VariableContainer_delete(variable_container_t *object);

// Only temporary while refactoring.
const char **VariableContainer_get_values_hack(variable_container_t *object);

// Change content of variable with given number to NUL terminated content.
// Returns '1' if value actually changed and all callbacks were called,
// '0' if no change was detected.
int VariableContainer_change(variable_container_t *object,
			     int variable_num, const char *value);

// Callback handling. Whenever a variable changes, the callback is called.
// Be careful when changing variables in the original container as this will
// trigger recursive calls to the container.
typedef void (*variable_change_listener_t)(void *userdata,
					   int var_num, const char *var_name,
					   const char *old_value,
					   const char *new_value);
void VariableContainer_register_callback(variable_container_t *object,
					 variable_change_listener_t callback,
					 void *userdata);

// Iterate through all variables.
typedef void (*variable_iterator_t)(void *userdata, int var_num,
				    const char *var_name, const char *var_value);
void VariableContainer_iterate(variable_container_t *object,
			       variable_iterator_t cb, void *userdata);

// -- UPnP LastChange Builder - builds a LastChange XML document from
// added name/value pairs.
struct upnp_last_change_builder;
typedef struct upnp_last_change_builder upnp_last_change_builder_t;
upnp_last_change_builder_t *UPnPLastChangeBuilder_new(void);
void UPnPLastChangeBuilder_delete(upnp_last_change_builder_t *builder);

void UPnPLastChangeBuilder_add(upnp_last_change_builder_t *builder,
			       const char *name, const char *value);
// Returns a newly allocated XML string that needs to be free()'d by the caller.
// Resets the document. If no changes have been added, NULL is returned.
char *UPnPLastChangeBuilder_to_xml(upnp_last_change_builder_t *builder);

// -- UPnP LastChange collector
struct upnp_device;  // forward declare.
struct upnp_last_change_collector;
typedef struct upnp_last_change_collector upnp_last_change_collector_t;
upnp_last_change_collector_t *
UPnPLastChangeCollector_new(variable_container_t *variable_container,
			    int last_change_var_num,
			    struct upnp_device *upnp_device,
			    const char *service_name);

// If we know that there are a couple of changes upcoming, we can
// 'start_transaction' and tell the collector to keep collecting until we
// 'commit'. There can be only one transaction open at a time.
void UPnPLastChangeCollector_start_transaction(upnp_last_change_collector_t *o);
void UPnPLastChangeCollector_commit(upnp_last_change_collector_t *object);

// no delete yet. We leak that.
#endif  /* VARIABLE_CONTAINER_H */
