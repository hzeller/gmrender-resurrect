/* upnp_device.c - Generic UPnP device handling
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifdef HAVE_LIBUPNP
#include <upnp/upnp.h>
#include <upnp/ithread.h>
#include <upnp/upnptools.h>
#endif

#include "logging.h"

#include "xmlescape.h"
#include "webserver.h"
#include "xmldoc.h"
#include "upnp.h"
#include "upnp_device.h"
#include "variable-container.h"

#define ENABLE_ACTION_LOGGING

struct upnp_device {
	struct upnp_device_descriptor *upnp_device_descriptor;
#ifdef HAVE_LIBUPNP
	ithread_mutex_t device_mutex;
        UpnpDevice_Handle device_handle;
#endif
};

int upnp_add_response(struct action_event *event,
		      const char *key, const char *value)
{
	int result = -1;
	char *val;
#ifdef HAVE_LIBUPNP
	int rc;
#endif

	assert(event != NULL);
	assert(key != NULL);

	if (event->status) {
		goto out;
	}

	val = strdup(value);
	if (val == NULL) {
		/* report memory failure */
		event->status = -1;
#ifdef HAVE_LIBUPNP
		event->request->ActionResult = NULL;
		event->request->ErrCode = UPNP_SOAP_E_ACTION_FAILED;
		strcpy(event->request->ErrStr, strerror(errno));
#endif
		goto out;
	}

#ifdef HAVE_LIBUPNP
	rc =
	    UpnpAddToActionResponse(&event->request->ActionResult,
				    event->request->ActionName,
				    event->service->service_type, key, val);

	if (rc != UPNP_E_SUCCESS) {
		/* report custom error */
		event->request->ActionResult = NULL;
		event->request->ErrCode = UPNP_SOAP_E_ACTION_FAILED;
		strcpy(event->request->ErrStr, UpnpGetErrorMessage(rc));
		goto out;
	}

	result = 0;
#endif
out:
	if (val != NULL) {
		free(val);
	}
	return result;
}

int upnp_append_variable(struct action_event *event,
			 int varnum, const char *paramname)
{
	const char *value;
	struct service *service = event->service;
	int retval = -1;

	assert(event != NULL);
	assert(paramname != NULL);

	if (varnum >= service->variable_count) {
#ifdef HAVE_LIBUPNP
		upnp_set_error(event, UPNP_E_INTERNAL_ERROR,
			       "Internal Error - illegal variable number %d",
			       varnum);
#endif
		goto out;
	}

#ifdef HAVE_LIBUPNP
	ithread_mutex_lock(service->service_mutex);
#endif

	value = VariableContainer_get(service->variable_container, varnum, NULL);
	assert(value != NULL);
	retval = upnp_add_response(event, paramname, value);

#ifdef HAVE_LIBUPNP
	ithread_mutex_unlock(service->service_mutex);
#endif
out:
	return retval;
}

void upnp_set_error(struct action_event *event, int error_code,
		    const char *format, ...)
{
	event->status = -1;

#ifdef HAVE_LIBUPNP
	va_list ap;
	va_start(ap, format);
	event->request->ActionResult = NULL;
	event->request->ErrCode = UPNP_SOAP_E_ACTION_FAILED;
	vsnprintf(event->request->ErrStr, sizeof(event->request->ErrStr),
		  format, ap);

	va_end(ap);
	Log_error("upnp", "%s: %s\n", __FUNCTION__, event->request->ErrStr);
#endif
}

char *upnp_get_string(struct action_event *event, const char *key)
{
#ifdef HAVE_LIBUPNP
	IXML_Node *node;

	node = (IXML_Node *) event->request->ActionRequest;
	if (node == NULL) {
		upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
			       "Invalid action request document");
		return NULL;
	}
	node = ixmlNode_getFirstChild(node);
	if (node == NULL) {
		upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
			       "Invalid action request document");
		return NULL;
	}
	node = ixmlNode_getFirstChild(node);

	for (/**/; node != NULL; node = ixmlNode_getNextSibling(node)) {
		if (strcmp(ixmlNode_getNodeName(node), key) == 0) {
			node = ixmlNode_getFirstChild(node);
			const char *node_value = (node != NULL
						  ? ixmlNode_getNodeValue(node)
						  : NULL);
			return strdup(node_value != NULL ? node_value : "");
		}
	}

	upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
		       "Missing action request argument (%s)", key);
#endif /* HAVE_LIBUPNP */
	return NULL;
}

#ifdef HAVE_LIBUPNP
static int handle_subscription_request(struct upnp_device *priv,
                                       struct Upnp_Subscription_Request
                                              *sr_event)
{
	struct service *srv;
	int rc;
	int result = -1;

	assert(priv != NULL);


	Log_info("upnp", "Subscription request for %s (%s)",
		 sr_event->ServiceId, sr_event->UDN);

	srv = find_service(priv->upnp_device_descriptor, sr_event->ServiceId);
	if (srv == NULL) {
		Log_error("upnp", "%s: Unknown service '%s'", __FUNCTION__,
			  sr_event->ServiceId);
		goto out;
	}

	ithread_mutex_lock(&(priv->device_mutex));

	// There is really only one variable evented: LastChange
	const char *eventvar_names[] = {
		"LastChange",
		NULL
	};
	const char *eventvar_values[] = {
		NULL, NULL
	};

	// Build the current state of the variables as one gigantic initial
	// LastChange update.
	const int var_count =
		VariableContainer_get_num_vars(srv->variable_container);
	upnp_last_change_builder_t *builder = UPnPLastChangeBuilder_new();
	for (int i = 0; i < var_count; ++i) {
		const char *name;
		const char *value =
			VariableContainer_get(srv->variable_container, i, &name);
		// Send over all variables except "LastChange" itself.
		if (value && strcmp("LastChange", name) != 0) {
			UPnPLastChangeBuilder_add(builder, name, value);
		}
	}
	char *xml_value = UPnPLastChangeBuilder_to_xml(builder);
	Log_info("upnp", "Initial variable sync: %s", xml_value);
	eventvar_values[0] = xmlescape(xml_value, 0);
	free(xml_value);
	UPnPLastChangeBuilder_delete(builder);

	rc = UpnpAcceptSubscription(priv->device_handle,
				    sr_event->UDN, sr_event->ServiceId,
				    eventvar_names, eventvar_values, 1,
				    sr_event->Sid);
	if (rc == UPNP_E_SUCCESS) {
		result = 0;
	}

	ithread_mutex_unlock(&(priv->device_mutex));

	free((char*)eventvar_values[0]);

out:
	return result;
}
#endif

int upnp_device_notify(struct upnp_device *device,
                       const char *serviceID,
                       const char **varnames,
                       const char **varvalues, int varcount)
{
#ifdef HAVE_LIBUPNP
        UpnpNotify(device->device_handle, 
                   device->upnp_device_descriptor->udn, serviceID,
		   varnames, varvalues, varcount);
#endif

	return 0;
}


static int handle_var_request(struct upnp_device *priv,
			      struct Upnp_State_Var_Request *var_event) {
	struct service *srv = find_service(priv->upnp_device_descriptor,
					   var_event->ServiceID);
	if (srv == NULL) {
		var_event->ErrCode = UPNP_SOAP_E_INVALID_ARGS;
		return -1;
	}
	char *result = NULL;
	const int var_count =
		VariableContainer_get_num_vars(srv->variable_container);
	for (int i = 0; i < var_count; ++i) {
		const char *name;
		const char *value =
			VariableContainer_get(srv->variable_container, i, &name);
		if (value && strcmp(var_event->StateVarName, name) == 0) {
			result = strdup(value);
			break;
		}
	}
	var_event->CurrentVal = result;
	var_event->ErrCode = (result == NULL)
		? UPNP_SOAP_E_INVALID_VAR
		: UPNP_E_SUCCESS;
	Log_info("upnp", "Variable request %s -> %s (%s)",
		 var_event->StateVarName, result, var_event->ServiceID);
	return 0;
}

#ifdef HAVE_LIBUPNP
static int handle_action_request(struct upnp_device *priv,
                                 struct Upnp_Action_Request *ar_event)
{
	struct service *event_service;
	struct action *event_action;

	event_service = find_service(priv->upnp_device_descriptor,
				     ar_event->ServiceID);
	event_action = find_action(event_service, ar_event->ActionName);

	if (event_action == NULL) {
		Log_error("upnp", "Unknown action '%s' for service '%s'",
			  ar_event->ActionName, ar_event->ServiceID);
		ar_event->ActionResult = NULL;
		ar_event->ErrCode = 401;
		return -1;
	}
#ifdef ENABLE_ACTION_LOGGING
	{
		char *action_request_xml = NULL;
		if (ar_event->ActionRequest) {
			action_request_xml = ixmlDocumenttoString(
					   ar_event->ActionRequest);
		}
		Log_info("upnp", "Action '%s'; Request: %s",
			 ar_event->ActionName, action_request_xml);
		free(action_request_xml);
	}
#endif

	if (event_action->callback) {
		struct action_event event;
		int rc;
		event.request = ar_event;
		event.status = 0;
		event.service = event_service;
                event.device = priv;

		rc = (event_action->callback) (&event);
		if (rc == 0) {
			ar_event->ErrCode = UPNP_E_SUCCESS;
#ifdef ENABLE_ACTION_LOGGING
			if (ar_event->ActionResult) {
				char *action_result_xml = NULL;
				action_result_xml = ixmlDocumenttoString(
						ar_event->ActionResult);
				Log_info("upnp", "Action '%s' OK; Response %s",
					 ar_event->ActionName,
					 action_result_xml);
				free(action_result_xml);
			} else {
				Log_info("upnp", "Action '%s' OK",
					 ar_event->ActionName);
			}
#endif
		}
		if (ar_event->ActionResult == NULL) {
			ar_event->ActionResult =
			    UpnpMakeActionResponse(ar_event->ActionName,
						   ar_event->ServiceID, 0,
						   NULL);
		}
	} else {
		Log_error("upnp",
			  "Got a valid action, but no handler defined (!)\n"
			  "  ErrCode:    %d\n"
			  "  Socket:     %d\n"
			  "  ErrStr:     '%s'\n"
			  "  ActionName: '%s'\n"
			  "  DevUDN:     '%s'\n"
			  "  ServiceID:  '%s'\n",
			  ar_event->ErrCode, ar_event->Socket, ar_event->ErrStr,
			  ar_event->ActionName, ar_event->DevUDN,
			  ar_event->ServiceID);
		ar_event->ErrCode = UPNP_E_SUCCESS;
	}

	return 0;
}
#endif

#ifdef HAVE_LIBUPNP
static int event_handler(Upnp_EventType EventType, void *event, void *userdata)
{
	struct upnp_device *priv = (struct upnp_device *) userdata;
	switch (EventType) {
	case UPNP_CONTROL_ACTION_REQUEST:
		handle_action_request(priv, event);
		break;

	case UPNP_CONTROL_GET_VAR_REQUEST:
		handle_var_request(priv, event);
		break;

	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		handle_subscription_request(priv, event);
		break;

	default:
		Log_error("upnp", "Unknown event type: %d", EventType);
		break;
	}
	return 0;
}
#endif


struct upnp_device *upnp_device_init(struct upnp_device_descriptor *device_def,
				     const char *ip_address)
{
	int rc;
#ifdef HAVE_LIBUPNP
	short int port = 0;
#endif
	struct service *srv;
	struct icon *icon_entry;
	char *buf;
	struct upnp_device *priv = NULL;
	int i;

	assert(device_def != NULL);

	if (device_def->init_function)
	{
		rc = device_def->init_function();
		if (rc != 0) {
			goto out;
		}
	}

	priv = malloc(sizeof(*priv));
	priv->upnp_device_descriptor = device_def;
#ifdef HAVE_LIBUPNP
	ithread_mutex_init(&(priv->device_mutex), NULL);
#endif

	/* register icons in web server */
        for (i=0; (icon_entry = device_def->icons[i]); i++) {
		webserver_register_file(icon_entry->url, "image/png");
        }

	/* generate and register service schemas in web server */
        for (i=0; (srv = device_def->services[i]); i++) {
       		buf = upnp_get_scpd(srv);
		assert(buf != NULL);
		webserver_register_buf(srv->scpd_url, buf, "text/xml");
	}

#ifdef HAVE_LIBUPNP
	rc = UpnpInit(ip_address, port);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpInit() Error: %d", rc);
		goto upnp_err_out;
	}
	rc = UpnpEnableWebserver(TRUE);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpEnableWebServer() Error: %d", rc);
		goto upnp_err_out;
	}
	rc = UpnpSetVirtualDirCallbacks(&virtual_dir_callbacks);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpSetVirtualDirCallbacks() Error: %d", rc);
		goto upnp_err_out;
	}
	rc = UpnpAddVirtualDir("/upnp");
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpAddVirtualDir() Error: %d", rc);
		goto upnp_err_out;
	}


       	buf = upnp_get_device_desc(device_def);

	rc = UpnpRegisterRootDevice2(UPNPREG_BUF_DESC,
				     buf, strlen(buf), 1,
				     &event_handler, priv,
				     &(priv->device_handle));
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpRegisterRootDevice2() Error: %d", rc);
		goto upnp_err_out;
	}

	rc = UpnpSendAdvertisement(priv->device_handle, 100);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("unpp", "Error sending advertisements: %d", rc);
		goto upnp_err_out;
	}
#endif

	goto out;

#ifdef HAVE_LIBUPNP
upnp_err_out:
	UpnpFinish();
#endif
out:
	return priv;
}


struct service *find_service(struct upnp_device_descriptor *device_def,
                             const char *service_id)
{
	struct service *event_service;
	int serviceNum = 0;

	assert(device_def != NULL);
	assert(service_id != NULL);
	while (event_service =
	       device_def->services[serviceNum], event_service != NULL) {
		if (strcmp(event_service->service_id, service_id) == 0)
			return event_service;
		serviceNum++;
	}
	return NULL;
}


/// ---- code to generate device descriptor

static struct xmlelement *gen_specversion(struct xmldoc *doc,
                                          int major, int minor)
{
        struct xmlelement *top;

        top=xmlelement_new(doc, "specVersion");

        add_value_element_int(doc, top, "major", major);
        add_value_element_int(doc, top, "minor", minor);

        return top;
}


static struct xmlelement *gen_desc_iconlist(struct xmldoc *doc, struct icon **icons)
{
	struct xmlelement *top;
	struct xmlelement *parent;
	struct icon *icon_entry;
	int i;

	top=xmlelement_new(doc, "iconList");

	for (i=0; (icon_entry=icons[i]); i++) {
		parent=xmlelement_new(doc, "icon");
		add_value_element(doc,parent,"mimetype", icon_entry->mimetype);
		add_value_element_int(doc,parent,"width",icon_entry->width);
		add_value_element_int(doc,parent,"height",icon_entry->height);
		add_value_element_int(doc,parent,"depth",icon_entry->depth);
		add_value_element(doc,parent,"url",icon_entry->url);
		xmlelement_add_element(doc, top, parent);
	}

	return top;
}


static struct xmlelement *
gen_desc_servicelist(struct upnp_device_descriptor *device_def,
		     struct xmldoc *doc)
{
	int i;
	struct service *srv;
	struct xmlelement *top;
	struct xmlelement *parent;

	top=xmlelement_new(doc, "serviceList");

        for (i=0; (srv = device_def->services[i]); i++) {
		parent=xmlelement_new(doc, "service");
		add_value_element(doc,parent,"serviceType",srv->service_type);
		add_value_element(doc,parent,"serviceId", srv->service_id);
		add_value_element(doc,parent,"SCPDURL", srv->scpd_url);
		add_value_element(doc,parent,"controlURL", srv->control_url);
		add_value_element(doc,parent,"eventSubURL", srv->event_url);
		xmlelement_add_element(doc, top, parent);
        }

	return top;
}



static struct xmldoc *generate_desc(struct upnp_device_descriptor *device_def)
{
	struct xmldoc *doc;
	struct xmlelement *root;
	struct xmlelement *child;
	struct xmlelement *parent;

	doc = xmldoc_new();

	root=xmldoc_new_topelement(doc, "root", "urn:schemas-upnp-org:device-1-0");
	child=gen_specversion(doc,1,0);
	xmlelement_add_element(doc, root, child);
	parent=xmlelement_new(doc, "device");
	xmlelement_add_element(doc, root, parent);
	add_value_element(doc,parent,"deviceType", device_def->device_type);
	add_value_element(doc,parent,"presentationURL", device_def->presentation_url);
	add_value_element(doc,parent,"friendlyName", device_def->friendly_name);
	add_value_element(doc,parent,"manufacturer", device_def->manufacturer);
	add_value_element(doc,parent,"manufacturerURL", device_def->manufacturer_url);
	add_value_element(doc,parent,"modelDescription", device_def->model_description);
	add_value_element(doc,parent,"modelName", device_def->model_name);
	add_value_element(doc,parent,"modelURL", device_def->model_url);
	add_value_element(doc,parent,"UDN", device_def->udn);
	//add_value_element(doc,parent,"modelNumber", device_def->model_number);
	//add_value_element(doc,parent,"serialNumber", device_def->serial_number);
	//add_value_element(doc,parent,"UPC", device_def->upc);
	if (device_def->icons) {
		child=gen_desc_iconlist(doc,device_def->icons);
		xmlelement_add_element(doc,parent,child);
	}
	child=gen_desc_servicelist(device_def, doc);
	xmlelement_add_element(doc, parent, child);

	return doc;
}

char *upnp_get_device_desc(struct upnp_device_descriptor *device_def)
{
        char *result = NULL;
        struct xmldoc *doc;

        doc = generate_desc(device_def);

        if (doc != NULL)
        {
                result = xmldoc_tostring(doc);
                xmldoc_free(doc);
        }
        return result;
}

