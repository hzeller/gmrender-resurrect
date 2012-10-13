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

struct device_private {
	struct device *upnp_device;
#ifdef HAVE_LIBUPNP
	ithread_mutex_t device_mutex;
        UpnpDevice_Handle device_handle;
#endif
};

int
upnp_add_response(struct action_event *event,
		  const char *key, const char *value)
{
	int result = -1;
	char *val;
#ifdef HAVE_LIBUPNP
	int rc;
#endif

	//ENTER();

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
				    event->service->type, key, val);

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
	//LEAVE();
	return result;
}

int upnp_append_variable(struct action_event *event,
			 int varnum, const char *paramname)
{
	const char *value;
	struct service *service = event->service;
	int retval = -1;

	//ENTER();

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

	value = (const char *) service->variable_values[varnum];
	if (value == NULL) {
#ifdef HAVE_LIBUPNP
		upnp_set_error(event, UPNP_E_INTERNAL_ERROR,
			       "Internal Error");
#endif
	} else {
		retval = upnp_add_response(event, paramname, value);
	}

#ifdef HAVE_LIBUPNP
	ithread_mutex_unlock(service->service_mutex);
#endif
out:
	//LEAVE();
	return retval;
}

void
upnp_set_error(struct action_event *event, int error_code,
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
	fprintf(stderr, "%s: %s\n", __FUNCTION__, event->request->ErrStr);
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

	for (; node != NULL; node = ixmlNode_getNextSibling(node)) {
		if (strcmp(ixmlNode_getNodeName(node), key) == 0) {
			node = ixmlNode_getFirstChild(node);
			if (node == NULL) {
				/* Are we sure empty arguments are reported like this? */
				return strdup("");
			}
			return strdup(ixmlNode_getNodeValue(node));
		}
	}

	upnp_set_error(event, UPNP_SOAP_E_INVALID_ARGS,
		       "Missing action request argument (%s)", key);
#endif /* HAVE_LIBUPNP */
	return NULL;
}

#ifdef HAVE_LIBUPNP
static int handle_subscription_request(struct device_private *priv,
                                       struct Upnp_Subscription_Request
                                              *sr_event)
{
	struct service *srv;
	int i;
	int eventVarCount = 0, eventVarIdx = 0;
	const char **eventvar_names;
	char **eventvar_values;
	int rc;
	int result = -1;

	ENTER();

	assert(priv != NULL);


	printf("Subscription request\n");
	printf("  %s\n", sr_event->UDN);
	printf("  %s\n", sr_event->ServiceId);

	srv = find_service(priv->upnp_device, sr_event->ServiceId);
	if (srv == NULL) {
		fprintf(stderr, "%s: Unknown service '%s'\n", __FUNCTION__,
			sr_event->ServiceId);
		goto out;
	}

	ithread_mutex_lock(&(priv->device_mutex));

	/* generate list of eventable variables */
	for(i=0; i<srv->variable_count; i++) {
		struct var_meta *metaEntry;
		metaEntry = &(srv->variable_meta[i]);
		if (metaEntry->sendevents == SENDEVENT_YES) {
			eventVarCount++;
		}
	}
	eventvar_names = malloc((eventVarCount+1) * sizeof(const char *));
	eventvar_values = malloc((eventVarCount+1) * sizeof(const char *));
	printf("%d evented variables\n", eventVarCount);

	for(i=0; i<srv->variable_count; i++) {
		struct var_meta *metaEntry;
		metaEntry = &(srv->variable_meta[i]);
		if (metaEntry->sendevents == SENDEVENT_YES) {
			eventvar_names[eventVarIdx] = srv->variable_names[i];
			eventvar_values[eventVarIdx] = xmlescape(srv->variable_values[i], 0);
			printf("Evented: '%s' == '%s'\n",
				eventvar_names[eventVarIdx],
				eventvar_values[eventVarIdx]);
			eventVarIdx++;
		}
	}
	eventvar_names[eventVarIdx] = NULL;
	eventvar_values[eventVarIdx] = NULL;

	rc = UpnpAcceptSubscription(priv->device_handle,
			       sr_event->UDN, sr_event->ServiceId,
			       (const char **)eventvar_names,
			       (const char **)eventvar_values,
			       eventVarCount,
			       sr_event->Sid);
	if (rc == UPNP_E_SUCCESS) {
		result = 0;
	}

	ithread_mutex_unlock(&(priv->device_mutex));

	for(i=0; i<eventVarCount; i++) {
		free(eventvar_values[i]);
	}
	free(eventvar_names);
	free(eventvar_values);

out:
	LEAVE();
	return result;
}
#endif

int upnp_device_notify(struct device_private *priv,
                       const char *serviceID,
                       const char **varnames,
                       const char **varvalues,
                       int varcount)
{
#ifdef HAVE_LIBUPNP
        UpnpNotify(priv->device_handle, 
                   priv->upnp_device->udn,
                   serviceID, varnames,
                   varvalues, 1);
#endif

	return 0;
}


#ifdef HAVE_LIBUPNP
static int handle_action_request(struct device_private *priv,
                                 struct Upnp_Action_Request
					  *ar_event)
{
	struct service *event_service;
	struct action *event_action;

	event_service = find_service(priv->upnp_device, ar_event->ServiceID);
	event_action = find_action(event_service, ar_event->ActionName);

	if (event_action == NULL) {
		fprintf(stderr, "Unknown action '%s' for service '%s'\n",
			ar_event->ActionName, ar_event->ServiceID);
		ar_event->ActionResult = NULL;
		ar_event->ErrCode = 401;
		return -1;
	}
	if (event_action->callback) {
		struct action_event event;
		int rc;
		event.request = ar_event;
		event.status = 0;
		event.service = event_service;
                event.device_priv = priv;

		rc = (event_action->callback) (&event);
		if (rc == 0) {
			ar_event->ErrCode = UPNP_E_SUCCESS;
			printf("Action '%s' was a success!\n",
                               ar_event->ActionName);
		}
		if (ar_event->ActionResult == NULL) {
			ar_event->ActionResult =
			    UpnpMakeActionResponse(ar_event->ActionName,
						   ar_event->ServiceID, 0,
						   NULL);
		}
	} else {
		fprintf(stderr,
			"Got a valid action, but no handler defined (!)\n");
		fprintf(stderr, "  ErrCode:    %d\n", ar_event->ErrCode);
		fprintf(stderr, "  Socket:     %d\n", ar_event->Socket);
		fprintf(stderr, "  ErrStr:     '%s'\n", ar_event->ErrStr);
		fprintf(stderr, "  ActionName: '%s'\n",
			ar_event->ActionName);
		fprintf(stderr, "  DevUDN:     '%s'\n", ar_event->DevUDN);
		fprintf(stderr, "  ServiceID:  '%s'\n",
			ar_event->ServiceID);
		ar_event->ErrCode = UPNP_E_SUCCESS;
	}



	return 0;
}
#endif

#ifdef HAVE_LIBUPNP
static int event_handler(Upnp_EventType EventType, void *event,
			    void *Cookie)
{
	struct device_private *priv = (struct device_private *)Cookie;
	switch (EventType) {
	case UPNP_CONTROL_ACTION_REQUEST:
		handle_action_request(priv, event);
		break;
	case UPNP_CONTROL_GET_VAR_REQUEST:
		printf("NOT IMPLEMENTED: control get variable request\n");
		break;
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
		printf("event subscription request\n");
		handle_subscription_request(priv, event);
		break;

	default:
		printf("Unknown event type: %d\n", EventType);
		break;
	}
	return 0;
}
#endif


int upnp_device_init(struct device *device_def, char *ip_address)
{
	int rc;
	int result = -1;
#ifdef HAVE_LIBUPNP
	short int port = 0;
#endif
	struct service *srv;
	struct icon *icon_entry;
	char *buf;
	struct device_private *priv = NULL;
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
	priv->upnp_device = device_def;
#ifdef HAVE_LIBUPNP
	ithread_mutex_init(&(priv->device_mutex), NULL);
#endif

	//upnp_device = device_def;

	/* register icons in web server */
        for (i=0; (icon_entry = device_def->icons[i]); i++) {
		webserver_register_file(icon_entry->url, "image/png");
        }

	/* generate and register service schemas in web server */
        for (i=0; (srv = device_def->services[i]); i++) {
       		buf = upnp_get_scpd(srv);
		assert(buf != NULL);
                printf("registering '%s'\n", srv->scpd_url);
		webserver_register_buf(srv->scpd_url,buf,"text/xml");
	}

#ifdef HAVE_LIBUPNP
	rc = UpnpInit(ip_address, port);
	if (UPNP_E_SUCCESS != rc) {
		printf("UpnpInit() Error: %d\n", rc);
		goto upnp_err_out;
	}
	rc = UpnpEnableWebserver(TRUE);
	if (UPNP_E_SUCCESS != rc) {
		printf("UpnpEnableWebServer() Error: %d\n", rc);
		goto upnp_err_out;
	}
	rc = UpnpSetVirtualDirCallbacks(&virtual_dir_callbacks);
	if (UPNP_E_SUCCESS != rc) {
		printf("UpnpSetVirtualDirCallbacks() Error: %d\n", rc);
		goto upnp_err_out;
	}
	rc = UpnpAddVirtualDir("/upnp");
	if (UPNP_E_SUCCESS != rc) {
		printf("UpnpAddVirtualDir() Error: %d\n", rc);
		goto upnp_err_out;
	}


       	buf = upnp_get_device_desc(device_def);

	rc = UpnpRegisterRootDevice2(UPNPREG_BUF_DESC,
				     buf, strlen(buf), 1,
				     &event_handler, priv,
				     &(priv->device_handle));
	if (UPNP_E_SUCCESS != rc) {
		printf("UpnpRegisterRootDevice2() Error: %d\n", rc);
		goto upnp_err_out;
	}

	rc = UpnpSendAdvertisement(priv->device_handle, 100);
	if (UPNP_E_SUCCESS != rc) {
		fprintf(stderr, "Error sending advertisements: %d\n", rc);
		goto upnp_err_out;
	}
	result = 0;
#endif

	goto out;

#ifdef HAVE_LIBUPNP
upnp_err_out:
	UpnpFinish();
#endif
out:
	return result;
}


struct service *find_service(struct device *device_def,
                             char *service_name)
{
	struct service *event_service;
	int serviceNum = 0;

	assert(device_def != NULL);
	assert(service_name != NULL);
	while (event_service =
	       device_def->services[serviceNum], event_service != NULL) {
		if (strcmp(event_service->service_name, service_name) == 0)
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
		add_value_element(doc,parent,"mimetype",(char *)icon_entry->mimetype);
		add_value_element_int(doc,parent,"width",icon_entry->width);
		add_value_element_int(doc,parent,"height",icon_entry->height);
		add_value_element_int(doc,parent,"depth",icon_entry->depth);
		add_value_element(doc,parent,"url",(char *)icon_entry->url);
		xmlelement_add_element(doc, top, parent);
	}

	return top;
}


static struct xmlelement *gen_desc_servicelist(struct device *device_def,
                                          struct xmldoc *doc)
{
	int i;
	struct service *srv;
	struct xmlelement *top;
	struct xmlelement *parent;

	top=xmlelement_new(doc, "serviceList");

        for (i=0; (srv = device_def->services[i]); i++) {
		parent=xmlelement_new(doc, "service");
		add_value_element(doc,parent,"serviceType",srv->type);
		add_value_element(doc,parent,"serviceId",(char *)srv->service_name);
		add_value_element(doc,parent,"SCPDURL",(char *)srv->scpd_url);
		add_value_element(doc,parent,"controlURL",(char *)srv->control_url);
		add_value_element(doc,parent,"eventSubURL",(char *)srv->event_url);
		xmlelement_add_element(doc, top, parent);
        }

	return top;
}



static struct xmldoc *generate_desc(struct device *device_def)
{
	struct xmldoc *doc;
	struct xmlelement *root;
	struct xmlelement *child;
	struct xmlelement *parent;

	doc = xmldoc_new();

	//root=ixmlDocument_createElementNS(doc, "urn:schemas-upnp-org:device-1-0","root");
	root=xmldoc_new_topelement(doc, "root", "urn:schemas-upnp-org:device-1-0");
	child=gen_specversion(doc,1,0);
	xmlelement_add_element(doc, root, child);
	parent=xmlelement_new(doc, "device");
	xmlelement_add_element(doc, root, parent);
	add_value_element(doc,parent,"deviceType",(char *)device_def->device_type);
	add_value_element(doc,parent,"presentationURL",(char *)device_def->presentation_url);
	add_value_element(doc,parent,"friendlyName",(char *)device_def->friendly_name);
	add_value_element(doc,parent,"manufacturer",(char *)device_def->manufacturer);
	add_value_element(doc,parent,"manufacturerURL",(char *)device_def->manufacturer_url);
	add_value_element(doc,parent,"modelDescription",(char *)device_def->model_description);
	add_value_element(doc,parent,"modelName",(char *)device_def->model_name);
	add_value_element(doc,parent,"modelURL",(char *)device_def->model_url);
	add_value_element(doc,parent,"UDN",(char *)device_def->udn);
	//add_value_element(doc,parent,"modelNumber",(char *)device_def->model_number);
	//add_value_element(doc,parent,"serialNumber",(char *)device_def->serial_number);
	//add_value_element(doc,parent,"UPC",(char *)device_def->upc);
	if (device_def->icons) {
		child=gen_desc_iconlist(doc,device_def->icons);
		xmlelement_add_element(doc,parent,child);
	}
	child=gen_desc_servicelist(device_def, doc);
	xmlelement_add_element(doc, parent, child);

	return doc;
}

char *upnp_get_device_desc(struct device *device_def)
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

