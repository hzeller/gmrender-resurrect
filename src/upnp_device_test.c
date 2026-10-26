/* upnp_device_test.c - Unit tests for upnp_device
 *
 * This file is part of GMediaRender.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <upnp.h>

#include "test-helpers.h"
#include "upnp_compat.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"
#include "xmldoc.h"

static void test_find_service_and_create_device_desc(void) {
	static pthread_mutex_t srv_mutex = PTHREAD_MUTEX_INITIALIZER;
	static const struct var_meta vars[] = {
		{ 0, "TestVar", "hello", EV_NO, DATATYPE_STRING, NULL, NULL },
	};
	variable_container_t *vc = VariableContainer_new(1, vars);

	struct service srv1;
	memset(&srv1, 0, sizeof(srv1));
	srv1.service_mutex = &srv_mutex;
	srv1.service_id = "urn:upnp-org:serviceId:TestService";
	srv1.service_type = "urn:schemas-upnp-org:service:TestService:1";
	srv1.scpd_url = "/upnp/testSCPD.xml";
	srv1.control_url = "/upnp/control/test1";
	srv1.event_url = "/upnp/event/test1";
	srv1.variable_container = vc;

	struct service *services[] = { &srv1, NULL };
	struct icon icon1 = { 64, 64, 24, "/upnp/icon.png", "image/png" };
	struct icon *icons[] = { &icon1, NULL };

	struct upnp_device_descriptor desc;
	memset(&desc, 0, sizeof(desc));
	desc.device_type = "urn:schemas-upnp-org:device:MediaRenderer:1";
	desc.friendly_name = "TestRenderer";
	desc.manufacturer = "TestMfg";
	desc.manufacturer_url = "http://example.com";
	desc.model_description = "TestDesc";
	desc.model_name = "TestModel";
	desc.model_number = "1.0";
	desc.model_url = "http://example.com/model";
	desc.udn = "uuid:test-1234";
	desc.presentation_url = "";
	desc.icons = icons;
	desc.services = services;

	EXPECT_TRUE(find_service(&desc, "urn:upnp-org:serviceId:TestService") == &srv1);
	EXPECT_TRUE(find_service(&desc, "urn:upnp-org:serviceId:Unknown") == NULL);

	char *xml = upnp_create_device_desc(&desc);
	EXPECT_TRUE(xml != NULL);
	EXPECT_TRUE(strstr(xml, "<friendlyName>TestRenderer</friendlyName>") != NULL);
	EXPECT_TRUE(strstr(xml, "<UDN>uuid:test-1234</UDN>") != NULL);
	EXPECT_TRUE(strstr(xml, "<url>/upnp/icon.png</url>") != NULL);
	EXPECT_TRUE(strstr(xml, "<serviceId>urn:upnp-org:serviceId:TestService</serviceId>") != NULL);

	struct xmldoc *doc = xmldoc_parsexml(xml);
	EXPECT_TRUE(doc != NULL);
	xmldoc_free(doc);
	free(xml);

	VariableContainer_delete(vc);
}

static void test_action_event_helpers_no_leaks(void) {
	static pthread_mutex_t srv_mutex = PTHREAD_MUTEX_INITIALIZER;
	static const struct var_meta vars[] = {
		{ 0, "CurrentVolume", "42", EV_NO, DATATYPE_UI2, NULL, NULL },
	};
	variable_container_t *vc = VariableContainer_new(1, vars);

	struct service srv;
	memset(&srv, 0, sizeof(srv));
	srv.service_mutex = &srv_mutex;
	srv.service_id = "urn:upnp-org:serviceId:RenderingControl";
	srv.service_type = "urn:schemas-upnp-org:service:RenderingControl:1";
	srv.variable_container = vc;

	UpnpActionRequest *req = UpnpActionRequest_new();
	UpnpActionRequest_strcpy_ActionName(req, "GetVolume");

	IXML_Document *req_doc = ixmlParseBuffer(
		"<u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"<Channel>Master</Channel>"
		"<EmptyArg></EmptyArg>"
		"</u:GetVolume>");
	EXPECT_TRUE(req_doc != NULL);
	UpnpActionRequest_set_ActionRequest(req, req_doc);

	struct action_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.request = req;
	ev.service = &srv;

	/* 1. Extract present and empty arguments via upnp_get_string. */
	EXPECT_STREQ(upnp_get_string(&ev, "InstanceID"), "0");
	EXPECT_STREQ(upnp_get_string(&ev, "Channel"), "Master");
	EXPECT_STREQ(upnp_get_string(&ev, "EmptyArg"), "");
	EXPECT_INT_EQ(ev.status, 0);

	/* 2. Append variable to response and verify ActionResult XML. */
	upnp_append_variable(&ev, 0, "CurrentVolume");
	IXML_Document *res_doc = UpnpActionRequest_get_ActionResult(req);
	EXPECT_TRUE(res_doc != NULL);
	char *res_xml = ixmlDocumenttoString(res_doc);
	EXPECT_TRUE(strstr(res_xml, "<CurrentVolume>42</CurrentVolume>") != NULL);
	free(res_xml);
	ixmlDocument_free(res_doc);
	UpnpActionRequest_set_ActionResult(req, NULL);

	/* 3. Missing argument sets error via upnp_set_error without leaking UpnpString. */
	EXPECT_TRUE(upnp_get_string(&ev, "MissingArg") == NULL);
	EXPECT_INT_EQ(ev.status, -1);
	EXPECT_TRUE(strstr(UpnpActionRequest_get_ErrStr_cstr(req), "MissingArg") != NULL);

	ixmlDocument_free(req_doc);
	UpnpActionRequest_set_ActionRequest(req, NULL);
	UpnpActionRequest_delete(req);
	VariableContainer_delete(vc);
}

int main(void) {
	test_find_service_and_create_device_desc();
	test_action_event_helpers_no_leaks();
	return 0;
}
