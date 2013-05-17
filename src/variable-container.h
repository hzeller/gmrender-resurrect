/* variable_container - handling a bunch of variables containting NUL
 *   terminated strings, allowing callbacks to be called on changes.
 *
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

/*
1)
 - get a list of variables, default values
 - provide a setter for these.
 - provide a way to register/remove a callback when any of these changes.

2)
 - provide a UPnP variable change collector, that reacts on callbacks and
   builds a LastChange document.
 - the collector can have a OpenTransaction(), Commit() to keep collect
   updates before it changes the LastChange. If no transaction is open, it
   changes right away. assert(): only one transaction can be open at a time.
 - (future: add rate limiting: only fire if last commit is older than x time)
 - provide a way to register callbacks when transaction commits.

[ note: the LastChange variable should probably be not stored in the original
  set to avoid recursive calls ]

3) use
 - create transport variable list with defaults and store them in variable
   container.
 - create variable change collector with proper namespace.
 - in upnp_device, handle_subscription_request(): register callback at
   upnp variable cange collector.
*/

#ifndef VARIABLE_CONTAINER_H
#define VARIABLE_CONTAINER_H

struct variable_container;

// Create a new variable container. The variable_names need to be valid for the
// lifetime of this objec.
struct variable_container *
VariableContainer_new(int variable_num,
		      const char **variable_names,
		      const char **variable_init_values);
void VariableContainer_delete(struct variable_container *object);

// Change content of variable with given number to NUL terminated content.
// Returns '1' if value actually changed and all callbacks were called,
// '0' if no change was detected.
int VariableContainer_change(struct variable_container *object,
			     int variable_num, const char *value);

// Callback handling. Whenever a variable changes, the callback is called.
// The callback must never change a variable while it is being called.
typedef void (*variable_changed_t)(int var_num, const char *var_name,
				   const char *old_value, const char *new_value);
void VariableContainer_register_callback(struct variable_container *object,
					 variable_changed_t callback);
// No unregister yet.
#endif  /* VARIABLE_CONTAINER_H */
