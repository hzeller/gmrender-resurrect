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
#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <upnp.h>
#include <ithread.h>
#include <upnptools.h>

#include "logging.h"

#include "xmlescape.h"
#include "webserver.h"
#include "xmldoc.h"
#include "upnp_service.h"
#include "upnp_device.h"
#include "variable-container.h"

// Enable logging of action requests.
//#define ENABLE_ACTION_LOGGING

struct upnp_device {
	struct upnp_device_descriptor *upnp_device_descriptor;
	ithread_mutex_t device_mutex;
        UpnpDevice_Handle device_handle;
};

int upnp_add_response(struct action_event *event,
		      const char *key, const char *value)
{
	assert(event != NULL);
	assert(key != NULL);
	assert(value != NULL);

	if (event->status) {
		return -1;
	}

	IXML_Document* actionResult = UpnpActionRequest_get_ActionResult(event->request);
	const char* actionName = UpnpActionRequest_get_ActionName_cstr(event->request);
	int rc = UpnpAddToActionResponse(&actionResult, actionName,
				     event->service->service_type, key, value);
	if (rc != UPNP_E_SUCCESS) {
		/* report custom error */
		UpnpString *errorMessage = UpnpString_new();
		UpnpString_set_String(errorMessage, UpnpGetErrorMessage(rc));
		UpnpActionRequest_set_ActionResult(event->request, NULL);
		UpnpActionRequest_set_ErrCode(event->request, UPNP_SOAP_E_ACTION_FAILED);
		UpnpActionRequest_set_ErrStr(event->request, errorMessage);
		return -1;
	}

	UpnpActionRequest_set_ActionResult(event->request, actionResult);
	return 0;
}

void upnp_append_variable(struct action_event *event,
                          int varnum, const char *paramname)
{
	const char *value;
	struct service *service = event->service;

	assert(event != NULL);
	assert(paramname != NULL);

	ithread_mutex_lock(service->service_mutex);

	value = VariableContainer_get(service->variable_container, varnum, NULL);
	assert(value != NULL);   // triggers on invalid variable.
	upnp_add_response(event, paramname, value);

	ithread_mutex_unlock(service->service_mutex);
}

void upnp_set_error(struct action_event *event, int error_code,
		    const char *format, ...)
{
	event->status = -1;

	va_list ap;
	va_start(ap, format);
	char buffer[LINE_SIZE];
	vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);

	UpnpActionRequest_set_ActionResult(event->request, NULL);
	UpnpActionRequest_set_ErrCode(event->request, UPNP_SOAP_E_ACTION_FAILED);
	UpnpString *errStr = UpnpString_new();
	UpnpString_set_String(errStr, buffer);
	UpnpActionRequest_set_ErrStr(event->request, errStr);
	Log_error("upnp", "%s: %s (%d)\n", __FUNCTION__,
		  UpnpActionRequest_get_ErrStr_cstr(event->request),
		  error_code);
}

const char *upnp_get_string(struct action_event *event, const char *key)
{
	IXML_Node *node;

	node = (IXML_Node *)UpnpActionRequest_get_ActionRequest(event->request);
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
			return node_value != NULL ? node_value : "";
		}
	}

	upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
		       "Missing action request argument (%s)", key);
	return NULL;
}

static int handle_subscription_request(struct upnp_device *priv,
				       const UpnpSubscriptionRequest *sr_event)
{
	struct service *srv;
	int rc;

	assert(priv != NULL);

	const char *serviceId = UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event);
	const char *udn = UpnpSubscriptionRequest_get_UDN_cstr(sr_event);
	Log_info("upnp", "Subscription request for %s (%s)", serviceId, udn);
	srv = find_service(priv->upnp_device_descriptor, serviceId);
	if (srv == NULL) {
		Log_error("upnp", "%s: Unknown service '%s'", __FUNCTION__,
			serviceId);
		return -1;
	}

	int result = -1;
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
	ithread_mutex_lock(srv->service_mutex);
	const int var_count =
		VariableContainer_get_num_vars(srv->variable_container);
	// TODO(hzeller): maybe use srv->last_change directly ?
	upnp_last_change_builder_t *builder = UPnPLastChangeBuilder_new(srv->event_xml_ns);
	for (int i = 0; i < var_count; ++i) {
		const char *name;
		const char *value =
			VariableContainer_get(srv->variable_container, i, &name);
		// Send over all variables except "LastChange" itself. Also all
		// A_ARG_TYPE variables are not evented.
		if (value && strcmp("LastChange", name) != 0
		    && strncmp("A_ARG_TYPE_", name, strlen("A_ARG_TYPE_")) != 0) {
			UPnPLastChangeBuilder_add(builder, name, value);
		}
	}
	ithread_mutex_unlock(srv->service_mutex);
	char *xml_value = UPnPLastChangeBuilder_to_xml(builder);
	Log_info("upnp", "Initial variable sync: %s", xml_value);
	eventvar_values[0] = xmlescape(xml_value, 0);
	free(xml_value);
	UPnPLastChangeBuilder_delete(builder);

	const char *sid = UpnpSubscriptionRequest_get_SID_cstr(sr_event);
	rc = UpnpAcceptSubscription(priv->device_handle,
				    udn, serviceId,
				    eventvar_names, eventvar_values, 1, sid);
	if (rc == UPNP_E_SUCCESS) {
		result = 0;
	} else {
		Log_error("upnp", "Accept Subscription Error: %s (%d)",
			  UpnpGetErrorMessage(rc), rc);
	}

	ithread_mutex_unlock(&(priv->device_mutex));

	free((char*)eventvar_values[0]);

	return result;
}

int upnp_device_notify(struct upnp_device *device,
                       const char *serviceID,
                       const char **varnames,
                       const char **varvalues, int varcount)
{
        UpnpNotify(device->device_handle,
                   device->upnp_device_descriptor->udn, serviceID,
		   varnames, varvalues, varcount);

	return 0;
}


static int handle_var_request(struct upnp_device *priv,
			      UpnpStateVarRequest *event)
{
	const char *serviceID = UpnpStateVarRequest_get_ServiceID_cstr(event);

	struct service *srv = find_service(priv->upnp_device_descriptor, serviceID);
	if (srv == NULL) {
		UpnpStateVarRequest_set_ErrCode(event, UPNP_SOAP_E_INVALID_ARGS);
		return -1;
	}

	ithread_mutex_lock(srv->service_mutex);

	char *result = NULL;
	const int var_count =
		VariableContainer_get_num_vars(srv->variable_container);
	for (int i = 0; i < var_count; ++i) {
		const char *name = NULL;
		const char *value = VariableContainer_get(srv->variable_container, i, &name);
		const char *stateVarName = UpnpStateVarRequest_get_StateVarName_cstr(event);
		if (value && strcmp(stateVarName, name) == 0) {
			result = strdup(value);
			break;
		}
	}

	ithread_mutex_unlock(srv->service_mutex);

	UpnpStateVarRequest_set_CurrentVal(event, result);
	int errCode = (result == NULL) ? UPNP_SOAP_E_INVALID_VAR : UPNP_E_SUCCESS;
	UpnpStateVarRequest_set_ErrCode(event, errCode);
	Log_info("upnp", "Variable request %s -> %s (%s)",
		UpnpStateVarRequest_get_StateVarName_cstr(event), result, serviceID);
	return 0;
}

static int handle_action_request(struct upnp_device *priv,
				 UpnpActionRequest *ar_event)
{
	const char *serviceID = UpnpActionRequest_get_ServiceID_cstr(ar_event);
	const char *actionName = UpnpActionRequest_get_ActionName_cstr(ar_event);

	struct service *event_service = find_service(priv->upnp_device_descriptor, serviceID);
	struct action *event_action = find_action(event_service, actionName);
	if (event_action == NULL) {
		Log_error("upnp", "Unknown action '%s' for service '%s'",
			  actionName, serviceID);
		UpnpActionRequest_set_ActionResult(ar_event, NULL);
		UpnpActionRequest_set_ErrCode(ar_event, 401);
		return -1;
	}

	// We want to send the LastChange event only after the action is
	// finished - just to be conservative, we don't know how clients
	// react to get LastChange notifictions while in the middle of
	// issuing an action.
	//
	// So we nest the change collector level here, so that we only send the
	// LastChange after the action is finished ().
	//
	// Note, this is, in fact, only a preparation and not yet working as
	// described above: we are still in the middle
	// of executing the event-callback while sending the last change
	// event implicitly when calling UPnPLastChangeCollector_finish() below.
	// It would be good to enqueue the upnp_device_notify() after
	// the action event is finished.
	if (event_service->last_change) {
		ithread_mutex_lock(event_service->service_mutex);
		UPnPLastChangeCollector_start(event_service->last_change);
		ithread_mutex_unlock(event_service->service_mutex);
	}

#ifdef ENABLE_ACTION_LOGGING
	{
		char *action_request_xml = NULL;
		if (UpnpActionRequest_get_ActionRequest(ar_event)) {
			action_request_xml = ixmlDocumenttoString(
					   UpnpActionRequest_get_ActionRequest(ar_event));
		}
		Log_info("upnp", "Action '%s'; Request: %s",
			 UpnpActionRequest_get_ActionName_cstr(ar_event), action_request_xml);
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
			UpnpActionRequest_set_ErrCode(event.request, UPNP_E_SUCCESS);
#ifdef ENABLE_ACTION_LOGGING
			if (UpnpActionRequest_get_ActionResult(ar_event)) {
				char *action_result_xml = ixmlDocumenttoString(
						UpnpActionRequest_get_ActionResult(ar_event));
				Log_info("upnp", "Action '%s' OK; Response %s",
					 UpnpActionRequest_get_ActionName_cstr(ar_event),
					 action_result_xml);
				free(action_result_xml);
			} else {
				Log_info("upnp", "Action '%s' OK",
					 UpnpActionRequest_get_ActionName_cstr(ar_event));
			}
#endif
		}
		IXML_Document *actionResult = UpnpActionRequest_get_ActionResult(ar_event);
		if (actionResult == NULL) {
			actionResult = UpnpMakeActionResponse(actionName,
						   event_service->service_type, 0, NULL);
			UpnpActionRequest_set_ActionResult(event.request, actionResult);
		}
	} else {
		int errCode = UpnpActionRequest_get_ErrCode(ar_event);
		int sock = UpnpActionRequest_get_Socket(ar_event);
		const char *errStr = UpnpActionRequest_get_ErrStr_cstr(ar_event);
		const char *actionName = UpnpActionRequest_get_ActionName_cstr(ar_event);
		const char *devUDN = UpnpActionRequest_get_DevUDN_cstr(ar_event);
		const char *serviceID = UpnpActionRequest_get_ServiceID_cstr(ar_event);
		Log_error("upnp",
			  "Got a valid action, but no handler defined (!)\n"
			  "  ErrCode:    %d\n"
			  "  Socket:     %d\n"
			  "  ErrStr:     '%s'\n"
			  "  ActionName: '%s'\n"
			  "  DevUDN:     '%s'\n"
			  "  ServiceID:  '%s'\n",
			  errCode, sock, errStr, actionName, devUDN, serviceID);
		UpnpActionRequest_set_ErrCode(ar_event, UPNP_E_SUCCESS);
	}

	if (event_service->last_change) {   // See comment above.
		ithread_mutex_lock(event_service->service_mutex);
		UPnPLastChangeCollector_finish(event_service->last_change);
		ithread_mutex_unlock(event_service->service_mutex);
	}
	return 0;
}

static UPNP_CALLBACK(event_handler, EventType, event, userdata)
{
	struct upnp_device *priv = (struct upnp_device *) userdata;
	switch (EventType) {
	case UPNP_CONTROL_ACTION_REQUEST:
		handle_action_request(priv, (UpnpActionRequest*)event);
		break;

	case UPNP_CONTROL_GET_VAR_REQUEST:
		handle_var_request(priv, (UpnpStateVarRequest*)event);
		break;

	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		handle_subscription_request(priv,
				     (const UpnpSubscriptionRequest*)event);
		break;

	default:
		Log_error("upnp", "Unknown event type: %d", EventType);
		break;
	}
	return 0;
}

static gboolean initialize_device(struct upnp_device_descriptor *device_def,
				  struct upnp_device *result_device,
				  const char *ip_address,
				  unsigned short port)
{
	int rc;
	char *buf;

	rc = UpnpInit(ip_address, port);
	/* There have been situations reported in which UPNP had issues
	 * initializing right after network came up. #129
	 */
	int retries_left = 60;
	static const int kRetryTimeMs = 1000;
	while (rc != UPNP_E_SUCCESS && retries_left--) {
		usleep(kRetryTimeMs * 1000);
		Log_error("upnp", "UpnpInit(ip=%s, port=%d) Error: %s (%d). Retrying... (%ds)",
			  ip_address, port, UpnpGetErrorMessage(rc), rc, retries_left);
		rc = UpnpInit(ip_address, port);
	}
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpInit(ip=%s, port=%d) Error: %s (%d). Giving up.",
			  ip_address, port, UpnpGetErrorMessage(rc), rc);
		return FALSE;
	}
	Log_info("upnp", "Registered IP=%s port=%d\n",
		 UpnpGetServerIpAddress(), UpnpGetServerPort());

	rc = UpnpEnableWebserver(TRUE);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpEnableWebServer() Error: %s (%d)",
			  UpnpGetErrorMessage(rc), rc);
		return FALSE;
	}

	if (!webserver_register_callbacks())
	  return FALSE;

	rc = UpnpAddVirtualDir("/upnp");
	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpAddVirtualDir() Error: %s (%d)",
			  UpnpGetErrorMessage(rc), rc);
		return FALSE;
	}

       	buf = upnp_create_device_desc(device_def);
	rc = UpnpRegisterRootDevice2(UPNPREG_BUF_DESC,
				     buf, strlen(buf), 1,
				     &event_handler, result_device,
				     &(result_device->device_handle));
	free(buf);

	if (UPNP_E_SUCCESS != rc) {
		Log_error("upnp", "UpnpRegisterRootDevice2() Error: %s (%d)",
			  UpnpGetErrorMessage(rc), rc);
		return FALSE;
	}

	rc = UpnpSendAdvertisement(result_device->device_handle, 100);
	if (UPNP_E_SUCCESS != rc) {
		Log_error("unpp", "Error sending advertisements: %s (%d)",
			  UpnpGetErrorMessage(rc), rc);
		return FALSE;
	}

	return TRUE;
}

struct upnp_device *upnp_device_init(struct upnp_device_descriptor *device_def,
				     const char *ip_address,
				     unsigned short port)
{
	int rc;
	char *buf;
	struct service *srv;
	struct icon *icon_entry;

	assert(device_def != NULL);

	if (device_def->init_function) {
		rc = device_def->init_function();
		if (rc != 0) {
			return NULL;
		}
	}

	struct upnp_device *result_device = (struct upnp_device*)malloc(sizeof(*result_device));
	result_device->upnp_device_descriptor = device_def;
	ithread_mutex_init(&(result_device->device_mutex), NULL);

	/* register icons in web server */
        for (int i = 0; (icon_entry = device_def->icons[i]); i++) {
		webserver_register_file(icon_entry->url, "image/png");
        }

	/* generate and register service schemas in web server */
        for (int i = 0; (srv = device_def->services[i]); i++) {
       		buf = upnp_get_scpd(srv);
		assert(buf != NULL);
		webserver_register_buf(srv->scpd_url, buf, "text/xml");
	}

	if (!initialize_device(device_def, result_device, ip_address, port)) {
		UpnpFinish();
		free(result_device);
		return NULL;
	}

	return result_device;
}

void upnp_device_shutdown(struct upnp_device *device) {
	UpnpFinish();
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


static struct xmlelement *gen_desc_iconlist(struct xmldoc *doc,
					    struct icon **icons) {
	struct xmlelement *top;
	struct xmlelement *parent;
	struct icon *icon_entry;

	top=xmlelement_new(doc, "iconList");

	for (int i = 0; (icon_entry=icons[i]); i++) {
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
		parent = xmlelement_new(doc, "service");
		add_value_element(doc, parent, "serviceType",srv->service_type);
		add_value_element(doc, parent, "serviceId", srv->service_id);
		add_value_element(doc, parent, "SCPDURL", srv->scpd_url);
		add_value_element(doc, parent, "controlURL", srv->control_url);
		add_value_element(doc, parent, "eventSubURL", srv->event_url);
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
	add_value_element(doc,parent,"modelNumber", device_def->model_number);
	add_value_element(doc,parent,"modelURL", device_def->model_url);
	add_value_element(doc,parent,"UDN", device_def->udn);
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

char *upnp_create_device_desc(struct upnp_device_descriptor *device_def) {
        char *result = NULL;
        struct xmldoc *doc;

        doc = generate_desc(device_def);

        if (doc != NULL) {
                result = xmldoc_tostring(doc);
                xmldoc_free(doc);
        }
        return result;
}
