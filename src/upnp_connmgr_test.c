/* upnp_connmgr_test.c - Unit tests for upnp_connmgr
 *
 * This file is part of GMediaRender.
 */

#include <upnp.h>

#include "test-helpers.h"
#include "upnp_compat.h"
#include "upnp_connmgr.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"

static int invoke_connmgr_action(struct service *srv, const char *action_name,
				 const char *xml, char **out_response_xml) {
	struct action *act = find_action(srv, action_name);
	EXPECT_TRUE(act != NULL);
	EXPECT_TRUE(act->callback != NULL);

	UpnpActionRequest *req = UpnpActionRequest_new();
	UpnpActionRequest_strcpy_ActionName(req, action_name);
	IXML_Document *req_doc = ixmlParseBuffer(xml);
	EXPECT_TRUE(req_doc != NULL);
	UpnpActionRequest_set_ActionRequest(req, req_doc);

	struct action_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.request = req;
	ev.service = srv;

	int rc = act->callback(&ev);

	IXML_Document *res_doc = UpnpActionRequest_get_ActionResult(req);
	if (out_response_xml != NULL) {
		*out_response_xml = res_doc ? ixmlDocumenttoString(res_doc) : NULL;
	}
	if (res_doc != NULL) {
		ixmlDocument_free(res_doc);
		UpnpActionRequest_set_ActionResult(req, NULL);
	}
	ixmlDocument_free(req_doc);
	UpnpActionRequest_set_ActionRequest(req, NULL);
	UpnpActionRequest_delete(req);
	return rc;
}

static void test_connmgr_mime_registration_and_filtering(void) {
	struct service *srv = upnp_connmgr_get_service();
	EXPECT_TRUE(srv != NULL);

	/* 1. Register several MIME types and initialize with no filter. */
	register_mime_type("audio/mpeg");
	register_mime_type("audio/x-flac");
	register_mime_type("video/mp4");

	EXPECT_INT_EQ(connmgr_init(NULL), 0);

	const char *sink_proto = VariableContainer_get(
		srv->variable_container, 1 /* CONNMGR_VAR_SINK_PROTO_INFO */, NULL);
	EXPECT_TRUE(sink_proto != NULL);
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:audio/mpeg:*") != NULL);
	/* audio/mpeg also auto-registers audio/x-mpeg and audio/x-scpls. */
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:audio/x-mpeg:*") != NULL);
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:audio/x-flac:*") != NULL);
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:video/mp4:*") != NULL);

	/* 2. Re-register and re-initialize with a filter string.
	 * Previously, connmgr_init() freed supported_types_list without
	 * resetting the pointer to NULL, causing a use-after-free here. */
	register_mime_type("audio/mpeg");
	register_mime_type("audio/x-flac");
	register_mime_type("video/mp4");

	EXPECT_INT_EQ(connmgr_init("audio,-audio/x-flac,+audio/ogg"), 0);

	sink_proto = VariableContainer_get(
		srv->variable_container, 1 /* CONNMGR_VAR_SINK_PROTO_INFO */, NULL);
	EXPECT_TRUE(sink_proto != NULL);
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:audio/mpeg:*") != NULL);
	EXPECT_TRUE(strstr(sink_proto, "http-get:*:audio/ogg:*") != NULL);
	/* Filtered out by -audio/x-flac and root filter 'audio': */
	EXPECT_TRUE(strstr(sink_proto, "audio/x-flac") == NULL);
	EXPECT_TRUE(strstr(sink_proto, "video/mp4") == NULL);
}

static void test_connmgr_service_actions(void) {
	struct service *srv = upnp_connmgr_get_service();
	char *resp = NULL;

	/* GetCurrentConnectionIDs should return 0 (success) and ConnectionIDs=0. */
	EXPECT_INT_EQ(invoke_connmgr_action(
		srv, "GetCurrentConnectionIDs",
		"<u:GetCurrentConnectionIDs xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
		"</u:GetCurrentConnectionIDs>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<ConnectionIDs>0</ConnectionIDs>") != NULL);
	free(resp);

	/* GetProtocolInfo returns Source and Sink. */
	EXPECT_INT_EQ(invoke_connmgr_action(
		srv, "GetProtocolInfo",
		"<u:GetProtocolInfo xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
		"</u:GetProtocolInfo>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<Sink>") != NULL);
	free(resp);

	/* GetCurrentConnectionInfo with missing ConnectionID returns -1. */
	EXPECT_INT_EQ(invoke_connmgr_action(
		srv, "GetCurrentConnectionInfo",
		"<u:GetCurrentConnectionInfo xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
		"</u:GetCurrentConnectionInfo>", NULL), -1);
}

int main(void) {
	test_connmgr_mime_registration_and_filtering();
	test_connmgr_service_actions();
	return 0;
}
