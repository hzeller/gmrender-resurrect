/* upnp_transport_test.c - Unit tests for upnp_transport
 *
 * This file is part of GMediaRender.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <upnp.h>

#include "output.h"
#include "song-meta-data.h"
#include "test-helpers.h"
#include "upnp_compat.h"
#include "upnp_device.h"
#include "upnp_service.h"
#include "upnp_transport.h"

/* Stubs for output.h functions used by upnp_transport.c. */
static char *last_set_uri = NULL;
static char *last_next_uri = NULL;
static output_update_meta_cb_t captured_meta_cb = NULL;
static output_transition_cb_t captured_transition_cb = NULL;
static gint64 last_seek_nanos = -1;

void output_set_uri(const char *uri, output_update_meta_cb_t meta_info) {
	free(last_set_uri);
	last_set_uri = uri ? strdup(uri) : NULL;
	captured_meta_cb = meta_info;
}
void output_set_next_uri(const char *uri) {
	free(last_next_uri);
	last_next_uri = uri ? strdup(uri) : NULL;
}
int output_play(output_transition_cb_t callback) {
	captured_transition_cb = callback;
	return 0;
}
int output_stop(void) {
	return 0;
}
int output_pause(void) {
	return 0;
}
int output_seek(gint64 position_nanos) {
	last_seek_nanos = position_nanos;
	return 0;
}
int output_get_position(gint64 *track_dur, gint64 *track_pos) {
	*track_dur = 0;
	*track_pos = 0;
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

static void test_transport_lifecycle_and_transitions(void) {
	struct service *srv = upnp_transport_get_service();
	EXPECT_TRUE(srv != NULL);

	/* 1. SetAVTransportURI with URI and empty metadata. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetAVTransportURI",
		"<u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<CurrentURI>http://example.com/song1.mp3</CurrentURI>"
		"<CurrentURIMetaData></CurrentURIMetaData>"
		"</u:SetAVTransportURI>", NULL), 0);
	EXPECT_STREQ(last_set_uri, "http://example.com/song1.mp3");
	EXPECT_TRUE(captured_meta_cb != NULL);

	/* 2. Stream metadata callback updates AVTransportURIMetaData. */
	struct SongMetaData smd;
	SongMetaData_init(&smd);
	smd.title = "Stream Title";
	smd.artist = "Stream Artist";
	captured_meta_cb(&smd);

	/* 3. Play, Seek, Pause, and Stop. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Speed>1</Speed>"
		"</u:Play>", NULL), 0);
	EXPECT_TRUE(captured_transition_cb != NULL);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Seek",
		"<u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Unit>REL_TIME</Unit>"
		"<Target>0:01:30</Target>"
		"</u:Seek>", NULL), 0);
	EXPECT_TRUE(last_seek_nanos == 90LL * 1000000000LL);

	/* 4. SetNextAVTransportURI and trigger gapless transition. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetNextAVTransportURI",
		"<u:SetNextAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<NextURI>http://example.com/song2.mp3</NextURI>"
		"<NextURIMetaData></NextURIMetaData>"
		"</u:SetNextAVTransportURI>", NULL), 0);
	EXPECT_STREQ(last_next_uri, "http://example.com/song2.mp3");

	captured_transition_cb(PLAY_STARTED_NEXT_STREAM);

	char *pos_xml = NULL;
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetPositionInfo",
		"<u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetPositionInfo>", &pos_xml), 0);
	EXPECT_TRUE(pos_xml != NULL);
	EXPECT_TRUE(strstr(pos_xml, "<TrackURI>http://example.com/song2.mp3</TrackURI>") != NULL);
	free(pos_xml);

	captured_transition_cb(PLAY_STOPPED);
}

static void test_missing_transport_arguments_do_not_crash(void) {
	struct service *srv = upnp_transport_get_service();

	/* SetAVTransportURI with missing <CurrentURIMetaData> does not NULL-deref in strlen(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetAVTransportURI",
		"<u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<CurrentURI>http://example.com/nometa.mp3</CurrentURI>"
		"</u:SetAVTransportURI>", NULL), 0);

	/* Seek with missing <Unit> returns -1 instead of NULL-deref in strcmp(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Seek",
		"<u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Seek>", NULL), -1);

	/* Seek with REL_TIME but missing <Target> returns -1 instead of NULL-deref in sscanf(). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Seek",
		"<u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Unit>REL_TIME</Unit>"
		"</u:Seek>", NULL), -1);
}

int main(void) {
	test_transport_lifecycle_and_transitions();
	test_missing_transport_arguments_do_not_crash();
	free(last_set_uri);
	free(last_next_uri);
	return 0;
}
