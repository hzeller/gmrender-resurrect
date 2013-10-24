/* oh_product.c - OpenHome Product service routines.
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 * Copyright (C) 2013 Andrey Demenev
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

#include "oh_product.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <glib.h>

#include <upnp/upnp.h>
#include <upnp/ithread.h>

#include "upnp.h"
#include "upnp_device.h"
#include "output.h"
#include "oh_time.h"

#define PRODUCT_TYPE "urn:av-openhome-org:service:Product:1"
#define PRODUCT_SERVICE_ID "urn:av-openhome:serviceId:Product"

#define PRODUCT_SCPD_URL "/upnp/openhomeproductSCPD.xml"
#define PRODUCT_CONTROL_URL "/upnp/control/openhomeproduct1"
#define PRODUCT_EVENT_URL "/upnp/event/openhomeproduct1"

typedef enum {
	PRODUCT_VAR_MANUFACTURER_NAME,
	PRODUCT_VAR_MANUFACTURER_INFO,
	PRODUCT_VAR_MANUFACTURER_URL,
	PRODUCT_VAR_MANUFACTURER_IMAGE_URL,
	PRODUCT_VAR_MODEL_NAME,
	PRODUCT_VAR_MODEL_INFO,
	PRODUCT_VAR_MODEL_URL,
	PRODUCT_VAR_MODEL_IMAGE_URL,
	PRODUCT_VAR_PRODUCT_ROOM,
	PRODUCT_VAR_PRODUCT_NAME,
	PRODUCT_VAR_PRODUCT_INFO,
	PRODUCT_VAR_PRODUCT_URL,
	PRODUCT_VAR_PRODUCT_IMAGE_URL,
	PRODUCT_VAR_STANDBY,
	PRODUCT_VAR_SOURCE_INDEX,
	PRODUCT_VAR_SOURCE_COUNT,
	PRODUCT_VAR_SOURCE_XML,
	PRODUCT_VAR_ATTRIBUTES,
	PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT,
	PRODUCT_VAR_SOURCE_TYPE,
	PRODUCT_VAR_SOURCE_NAME,
	PRODUCT_VAR_SOURCE_VISIBLE,
	
	PRODUCT_VAR_LAST_CHANGE,
	PRODUCT_VAR_UNKNOWN,
	PRODUCT_VAR_COUNT
} product_variable_t;

enum {
	PRODUCT_CMD_SOURCE_COUNT,
	PRODUCT_CMD_SOURCE,
	PRODUCT_CMD_SOURCE_INDEX,
	PRODUCT_CMD_SET_SOURCE_INDEX,
	PRODUCT_CMD_SET_SOURCE_INDEX_BY_NAME,
	PRODUCT_CMD_SOURCE_XML,
	PRODUCT_CMD_SOURCE_XML_CHANGE_COUNT,
	PRODUCT_CMD_ATTRIBUTES,
	PRODUCT_CMD_MANUFACTURER,
	PRODUCT_CMD_MODEL,
	PRODUCT_CMD_PRODUCT,
	PRODUCT_CMD_STANDBY,
	PRODUCT_CMD_SET_STANDBY,

	PRODUCT_CMD_UNKNOWN,
	PRODUCT_CMD_COUNT
};

static const char *product_variable_names[] = {
	[PRODUCT_VAR_MANUFACTURER_NAME] = "ManufacturerName",
	[PRODUCT_VAR_MANUFACTURER_INFO] = "ManufacturerInfo",
	[PRODUCT_VAR_MANUFACTURER_URL] = "ManufacturerUrl",
	[PRODUCT_VAR_MANUFACTURER_IMAGE_URL] = "ManufacturerImageUrl",
	[PRODUCT_VAR_MODEL_NAME] = "ModelName",
	[PRODUCT_VAR_MODEL_INFO] = "ModelInfo",
	[PRODUCT_VAR_MODEL_URL] = "ModelUrl",
	[PRODUCT_VAR_MODEL_IMAGE_URL] = "ModelImageUrl",
	[PRODUCT_VAR_PRODUCT_ROOM] = "ProductRoom",
	[PRODUCT_VAR_PRODUCT_NAME] = "ProductName",
	[PRODUCT_VAR_PRODUCT_INFO] = "ProductInfo",
	[PRODUCT_VAR_PRODUCT_URL] = "ProductUrl",
	[PRODUCT_VAR_PRODUCT_IMAGE_URL] = "ProductImageUrl",
	[PRODUCT_VAR_STANDBY] = "Standby",
	[PRODUCT_VAR_SOURCE_INDEX] = "SourceIndex",
	[PRODUCT_VAR_SOURCE_COUNT] = "SourceCount",
	[PRODUCT_VAR_SOURCE_XML] = "SourceXml",
	[PRODUCT_VAR_ATTRIBUTES] = "Attributes",
	[PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT] = "SourceXmlChangeCount",
	[PRODUCT_VAR_SOURCE_TYPE] = "SourceType",
	[PRODUCT_VAR_SOURCE_NAME] = "SourceName",
	[PRODUCT_VAR_SOURCE_VISIBLE] = "SourceVisible",
	[PRODUCT_VAR_LAST_CHANGE] = "LastChange",
	[PRODUCT_VAR_UNKNOWN] = NULL,
};

static const char *product_default_values[] = {
	[PRODUCT_VAR_MANUFACTURER_NAME] = "Ivo Clarysse, Henner Zeller, Andrey Demenev",
	[PRODUCT_VAR_MANUFACTURER_INFO] = "Ivo Clarysse, Henner Zeller, Andrey Demenev",
	[PRODUCT_VAR_MANUFACTURER_URL] = "http://github.com/hzeller/gmrender-resurrect",
	[PRODUCT_VAR_MANUFACTURER_IMAGE_URL] = "",
	[PRODUCT_VAR_MODEL_NAME] = PACKAGE_NAME,
	[PRODUCT_VAR_MODEL_INFO] = PACKAGE_STRING,
	[PRODUCT_VAR_MODEL_URL] = "http://github.com/hzeller/gmrender-resurrect",
	[PRODUCT_VAR_MODEL_IMAGE_URL] = "",
	[PRODUCT_VAR_PRODUCT_ROOM] = "Main Room",
	[PRODUCT_VAR_PRODUCT_NAME] = PACKAGE_NAME,
	[PRODUCT_VAR_PRODUCT_INFO] = "OpenHome Renderer",
	[PRODUCT_VAR_PRODUCT_URL] = "http://github.com/hzeller/gmrender-resurrect",
	[PRODUCT_VAR_PRODUCT_IMAGE_URL] = "",
	[PRODUCT_VAR_STANDBY] = "",
	[PRODUCT_VAR_SOURCE_INDEX] = "0",
	[PRODUCT_VAR_SOURCE_COUNT] = "1",
	[PRODUCT_VAR_SOURCE_XML] = "<SourceList><Source><Name>Playlist</Name><Type>Playlist</Type><Visible>1</Visible></Source></SourceList>",
	[PRODUCT_VAR_ATTRIBUTES] = "Info Time",
	[PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT] = "0",
	[PRODUCT_VAR_SOURCE_TYPE] = "Playlist",
	[PRODUCT_VAR_SOURCE_NAME] = "Playlist",
	[PRODUCT_VAR_SOURCE_VISIBLE] = "1",
	
	[PRODUCT_VAR_LAST_CHANGE] = "",
	[PRODUCT_VAR_UNKNOWN] = NULL,
};


static struct var_meta product_var_meta[] = {
	[PRODUCT_VAR_MANUFACTURER_NAME]			= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MANUFACTURER_INFO]			= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MANUFACTURER_URL]			= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MANUFACTURER_IMAGE_URL]	= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MODEL_NAME]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MODEL_INFO]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MODEL_URL]					= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_MODEL_IMAGE_URL]			= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_PRODUCT_ROOM]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_PRODUCT_NAME]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_PRODUCT_INFO]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_PRODUCT_URL]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_PRODUCT_IMAGE_URL]			= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_STANDBY]					= { SENDEVENT_YES, DATATYPE_BOOLEAN, NULL, NULL },
	[PRODUCT_VAR_SOURCE_INDEX]				= { SENDEVENT_YES, DATATYPE_UI4,     NULL, NULL },
	[PRODUCT_VAR_SOURCE_COUNT]				= { SENDEVENT_YES, DATATYPE_UI4,     NULL, NULL },
	[PRODUCT_VAR_SOURCE_XML]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_ATTRIBUTES]				= { SENDEVENT_YES, DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT]	= { SENDEVENT_NO,  DATATYPE_UI4,     NULL, NULL },
	[PRODUCT_VAR_SOURCE_TYPE]				= { SENDEVENT_NO,  DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_SOURCE_NAME]				= { SENDEVENT_NO,  DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_SOURCE_VISIBLE]			= { SENDEVENT_NO,  DATATYPE_STRING,  NULL, NULL },

	[PRODUCT_VAR_LAST_CHANGE]				= { SENDEVENT_NO,  DATATYPE_STRING,  NULL, NULL },
	[PRODUCT_VAR_UNKNOWN]					= { SENDEVENT_NO,  DATATYPE_UNKNOWN, NULL, NULL },
};


static struct argument *arguments_manufacturer[] = {
        & (struct argument) { "Name", PARAM_DIR_OUT, PRODUCT_VAR_MANUFACTURER_NAME },
        & (struct argument) { "Info", PARAM_DIR_OUT, PRODUCT_VAR_MANUFACTURER_INFO },
        & (struct argument) { "Url", PARAM_DIR_OUT, PRODUCT_VAR_MANUFACTURER_URL },
        & (struct argument) { "ImageUrl", PARAM_DIR_OUT, PRODUCT_VAR_MANUFACTURER_IMAGE_URL },
        NULL
};

static struct argument *arguments_model[] = {
        & (struct argument) { "Name", PARAM_DIR_OUT, PRODUCT_VAR_MODEL_NAME },
        & (struct argument) { "Info", PARAM_DIR_OUT, PRODUCT_VAR_MODEL_INFO },
        & (struct argument) { "Url", PARAM_DIR_OUT, PRODUCT_VAR_MODEL_URL },
        & (struct argument) { "ImageUrl", PARAM_DIR_OUT, PRODUCT_VAR_MODEL_IMAGE_URL },
        NULL
};

static struct argument *arguments_product[] = {
        & (struct argument) { "Room", PARAM_DIR_OUT, PRODUCT_VAR_PRODUCT_ROOM },
        & (struct argument) { "Name", PARAM_DIR_OUT, PRODUCT_VAR_PRODUCT_NAME },
        & (struct argument) { "Info", PARAM_DIR_OUT, PRODUCT_VAR_PRODUCT_INFO },
        & (struct argument) { "Url", PARAM_DIR_OUT, PRODUCT_VAR_PRODUCT_URL },
        & (struct argument) { "ImageUrl", PARAM_DIR_OUT, PRODUCT_VAR_PRODUCT_IMAGE_URL },
        NULL
};

static struct argument *arguments_standby[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_STANDBY },
        NULL
};

static struct argument *arguments_set_standby[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PRODUCT_VAR_STANDBY },
        NULL
};

static struct argument *arguments_source_xml[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_XML },
        NULL
};

static struct argument *arguments_set_source_index[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PRODUCT_VAR_SOURCE_INDEX },
        NULL
};

static struct argument *arguments_set_source_index_by_name[] = {
        & (struct argument) { "Value", PARAM_DIR_IN, PRODUCT_VAR_SOURCE_NAME },
        NULL
};

static struct argument *arguments_source_count[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_COUNT},
        NULL
};

static struct argument *arguments_source_index[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_INDEX },
		NULL
};

static struct argument *arguments_source[] = {
        & (struct argument) { "Index", PARAM_DIR_IN, PRODUCT_VAR_SOURCE_INDEX },
        & (struct argument) { "SystemName", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_NAME },
        & (struct argument) { "Type", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_TYPE },
        & (struct argument) { "Name", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_NAME },
        & (struct argument) { "Visible", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_VISIBLE },
        NULL
};

static struct argument *arguments_attributes[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_ATTRIBUTES },
        NULL
};

static struct argument *arguments_xml_count[] = {
        & (struct argument) { "Value", PARAM_DIR_OUT, PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT },
        NULL
};

static struct argument **argument_list[] = {
	[PRODUCT_CMD_SOURCE_COUNT] = arguments_source_count,
	[PRODUCT_CMD_SOURCE] = arguments_source,
	[PRODUCT_CMD_SOURCE_INDEX] = arguments_source_index,
	[PRODUCT_CMD_SET_SOURCE_INDEX] = arguments_set_source_index,
	[PRODUCT_CMD_SET_SOURCE_INDEX_BY_NAME] = arguments_set_source_index_by_name,
	[PRODUCT_CMD_SOURCE_XML] = arguments_source_xml,
	[PRODUCT_CMD_SOURCE_XML_CHANGE_COUNT] = arguments_xml_count,
	[PRODUCT_CMD_ATTRIBUTES] = arguments_attributes,
	[PRODUCT_CMD_MANUFACTURER] = arguments_manufacturer,
	[PRODUCT_CMD_MODEL] = arguments_model,
	[PRODUCT_CMD_PRODUCT] = arguments_product,
	[PRODUCT_CMD_STANDBY] = arguments_standby,
	[PRODUCT_CMD_SET_STANDBY] = arguments_set_standby,
	
	[PRODUCT_CMD_UNKNOWN] = NULL
};


extern struct service product_service_;
static variable_container_t *state_variables_ = NULL;

static ithread_mutex_t product_mutex;

static void service_lock(void)
{
	ithread_mutex_lock(&product_mutex);
	if (product_service_.last_change) {
		UPnPLastChangeCollector_start(product_service_.last_change);
	}
}

static void service_unlock(void)
{
	if (product_service_.last_change) {
		UPnPLastChangeCollector_finish(product_service_.last_change);
	}
	ithread_mutex_unlock(&product_mutex);
}

static int replace_var(product_variable_t varnum, const char *new_value) {
	return VariableContainer_change(state_variables_, varnum, new_value);
}

static int replace_var_uint(product_variable_t varnum, unsigned int new_value)
{
	char buf[32];
	sprintf(buf, "%u", new_value);
	return replace_var(varnum, buf);
}

static int cmd_source_count(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_COUNT, "Value");
	return 0;
}

static int cmd_source_index(struct action_event *event) {
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_INDEX, "Value");
	return 0;
}

static int cmd_source(struct action_event *event) {
	char *index_str = upnp_get_string(event, "Index");
	if (index_str == NULL)
		return -1;
	if (*index_str != '0' || index_str[1] != 0) {
		free(index_str);
		upnp_set_error(event, 800, "Invalid index");
		return -1;
	}
	free(index_str);
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_NAME, "SystemName");
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_TYPE, "Type");
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_NAME, "Name");
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_VISIBLE, "Visible");
	return 0;
}

static int cmd_set_source_index(struct action_event *event)
{
	char *index_str = upnp_get_string(event, "Value");
	if (index_str == NULL)
		return -1;
	if (*index_str != '0' || index_str[1] != 0) {
		free(index_str);
		upnp_set_error(event, 800, "Invalid index");
		return -1;
	}
	free(index_str);
	return 0;
}

static int cmd_set_source_index_by_name(struct action_event *event)
{
	char *name_str = upnp_get_string(event, "Value");
	if (name_str == NULL)
		return -1;
	if (strcmp(name_str, "Playlist")) {
		free(name_str);
		upnp_set_error(event, 800, "Invalid name");
		return -1;
	}
	free(name_str);
	return 0;
}

static int cmd_source_xml(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_XML, "Value");
	return 0;
}

static int cmd_source_xml_change_count(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_SOURCE_XML_CHANGE_COUNT, "Value");
	return 0;
}

static int cmd_attributes(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_ATTRIBUTES, "Value");
	return 0;
}

static int cmd_manufacturer(struct action_event *event) {
	upnp_append_variable(event, PRODUCT_VAR_MANUFACTURER_NAME, "Name");
	upnp_append_variable(event, PRODUCT_VAR_MANUFACTURER_INFO, "Info");
	upnp_append_variable(event, PRODUCT_VAR_MANUFACTURER_URL, "Url");
	upnp_append_variable(event, PRODUCT_VAR_MANUFACTURER_IMAGE_URL, "ImageUrl");
	return 0;
}

static int cmd_model(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_MODEL_NAME, "Name");
	upnp_append_variable(event, PRODUCT_VAR_MODEL_INFO, "Info");
	upnp_append_variable(event, PRODUCT_VAR_MODEL_URL, "Url");
	upnp_append_variable(event, PRODUCT_VAR_MODEL_IMAGE_URL, "ImageUrl");
	return 0;
}

static int cmd_product(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_PRODUCT_ROOM, "Room");
	upnp_append_variable(event, PRODUCT_VAR_PRODUCT_NAME, "Name");
	upnp_append_variable(event, PRODUCT_VAR_PRODUCT_INFO, "Info");
	upnp_append_variable(event, PRODUCT_VAR_PRODUCT_URL, "Url");
	upnp_append_variable(event, PRODUCT_VAR_PRODUCT_IMAGE_URL, "ImageUrl");
	return 0;
}

static int cmd_standby(struct action_event *event)
{
	upnp_append_variable(event, PRODUCT_VAR_STANDBY, "Value");
	return 0;
}

static int cmd_set_standby(struct action_event *event)
{
	return 0;
}

static struct action product_actions[] = {
	[PRODUCT_CMD_SOURCE_COUNT] = { "SourceCount", cmd_source_count },
	[PRODUCT_CMD_SOURCE] = { "Source", cmd_source },
	[PRODUCT_CMD_SOURCE_INDEX] = { "SourceIndex", cmd_source_index },
	[PRODUCT_CMD_SET_SOURCE_INDEX] = { "SetSourceIndex", cmd_set_source_index },
	[PRODUCT_CMD_SET_SOURCE_INDEX_BY_NAME] = { "SetSourceIndexByName", cmd_set_source_index_by_name },
	[PRODUCT_CMD_SOURCE_XML] = { "SourceXml", cmd_source_xml },
	[PRODUCT_CMD_SOURCE_XML_CHANGE_COUNT] = { "SourceXmlChangeCount", cmd_source_xml_change_count },
	[PRODUCT_CMD_ATTRIBUTES] = { "Attributes", cmd_attributes },
	[PRODUCT_CMD_MANUFACTURER] = { "Manufacturer", cmd_manufacturer },
	[PRODUCT_CMD_MODEL] = { "Model", cmd_model },
	[PRODUCT_CMD_PRODUCT] = { "Product", cmd_product },
	[PRODUCT_CMD_STANDBY] = { "Standby", cmd_standby },
	[PRODUCT_CMD_SET_STANDBY] = { "SetStandby", cmd_set_standby },
	[PRODUCT_CMD_UNKNOWN] = {NULL, NULL}
};

struct service *oh_product_get_service(void) {
	if (product_service_.variable_container == NULL) {
		state_variables_ =
			VariableContainer_new(PRODUCT_VAR_COUNT,
					      product_variable_names,
					      product_default_values);
		product_service_.variable_container = state_variables_;
	}
	return &product_service_;
}

void oh_product_init(struct upnp_device *device) {
	assert(product_service_.last_change == NULL);
	product_service_.last_change =
		UPnPLastChangeCollector_new(state_variables_, device,
					    PRODUCT_SERVICE_ID);
}

struct service product_service_ = {
	.service_id =           PRODUCT_SERVICE_ID,
	.service_type =         PRODUCT_TYPE,
	.scpd_url =				PRODUCT_SCPD_URL,
	.control_url =			PRODUCT_CONTROL_URL,
	.event_url =			PRODUCT_EVENT_URL,
	.actions =              product_actions,
	.action_arguments =     argument_list,
	.variable_names =       product_variable_names,
	.variable_container =   NULL,
	.last_change =          NULL,
	.variable_meta =        product_var_meta,
	.variable_count =       PRODUCT_VAR_UNKNOWN,
	.command_count =        PRODUCT_CMD_UNKNOWN,
	.service_mutex =        &product_mutex
};

