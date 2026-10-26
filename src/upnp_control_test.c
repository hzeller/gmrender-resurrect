/* upnp_control_test.c - Unit tests for upnp_control
 *
 * This file is part of GMediaRender.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <upnp.h>

#include "output.h"
#include "test-helpers.h"
#include "upnp_compat.h"
#include "upnp_control.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "variable-container.h"

/* Stubs for output.h functions used by upnp_control.c. */
static float last_output_volume = -1.0f;
static int last_output_mute = -1;

int output_get_volume(float *value) {
	*value = 1.0f;
	return 0;
}
int output_set_volume(float value) {
	last_output_volume = value;
	return 0;
}
int output_set_mute(int value) {
	last_output_mute = value;
	return 0;
}

static int invoke_action_xml(struct service *srv, const char *action_name,
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

static void test_volume_and_mute_actions(void) {
	struct service *srv = upnp_control_get_service();
	EXPECT_TRUE(srv != NULL);

	/* 1. Valid SetVolume to 100 -> sets mute to 0 and volume fraction to 1.0. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetVolume",
		"<u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"<Channel>Master</Channel>"
		"<DesiredVolume>100</DesiredVolume>"
		"</u:SetVolume>", NULL), 0);
	EXPECT_INT_EQ(last_output_mute, 0);
	EXPECT_TRUE(last_output_volume > 0.99f && last_output_volume <= 1.01f);

	/* 2. GetVolume returns 100. */
	char *resp = NULL;
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetVolume",
		"<u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"<Channel>Master</Channel>"
		"</u:GetVolume>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<CurrentVolume>100</CurrentVolume>") != NULL);
	free(resp);

	/* 3. Valid SetMute to 1. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetMute",
		"<u:SetMute xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"<Channel>Master</Channel>"
		"<DesiredMute>1</DesiredMute>"
		"</u:SetMute>", NULL), 0);
	EXPECT_INT_EQ(last_output_mute, 1);
}

static void test_missing_arguments_do_not_crash(void) {
	struct service *srv = upnp_control_get_service();

	/* Missing <DesiredMute> in SetMute returns -1 instead of NULL deref in atoi(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetMute",
		"<u:SetMute xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:SetMute>", NULL), -1);

	/* Missing <DesiredVolume> in SetVolume returns -1 instead of NULL deref in atoi(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetVolume",
		"<u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:SetVolume>", NULL), -1);

	/* Missing <DesiredVolume> in SetVolumeDB returns -1 instead of NULL deref in atof(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetVolumeDB",
		"<u:SetVolumeDB xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:SetVolumeDB>", NULL), -1);
}

int main(void) {
	test_volume_and_mute_actions();
	test_missing_arguments_do_not_crash();
	return 0;
}
