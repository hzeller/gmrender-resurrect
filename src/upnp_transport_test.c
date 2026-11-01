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
static int mock_play_rc = 0;
static int mock_pause_rc = 0;
static int listener_call_count = 0;

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
	return mock_play_rc;
}
int output_stop(void) {
	return 0;
}
int output_pause(void) {
	return mock_pause_rc;
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

static void transport_var_listener(void *userdata, int var_num,
				   const char *var_name, const char *old_val,
				   const char *new_val) {
	(void)userdata;
	(void)var_num;
	(void)var_name;
	(void)old_val;
	(void)new_val;
	listener_call_count++;
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

	upnp_transport_register_variable_listener(transport_var_listener, NULL);

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
	EXPECT_TRUE(listener_call_count > 0);

	/* 2. Stream metadata callback updates AVTransportURIMetaData. */
	struct SongMetaData smd;
	SongMetaData_init(&smd);
	smd.title = "Stream Title";
	smd.artist = "Stream Artist";
	captured_meta_cb(&smd);

	/* Empty title is ignored by update_meta_from_stream. */
	smd.title = "";
	captured_meta_cb(&smd);

	/* 3. Query GetMediaInfo, GetTransportInfo, GetCurrentTransportActions,
	 * GetDeviceCapabilities, and GetTransportSettings. */
	char *resp = NULL;
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetMediaInfo",
		"<u:GetMediaInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetMediaInfo>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<CurrentURI>http://example.com/song1.mp3</CurrentURI>") != NULL);
	free(resp);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetDeviceCapabilities",
		"<u:GetDeviceCapabilities xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetDeviceCapabilities>", NULL), 0);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetTransportSettings",
		"<u:GetTransportSettings xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetTransportSettings>", NULL), 0);

	/* Pause while STOPPED fails with transition not allowed (-1). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Pause",
		"<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Pause>", NULL), -1);

	/* Play failure when output_play returns non-zero. */
	mock_play_rc = -1;
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Speed>1</Speed>"
		"</u:Play>", NULL), -1);
	mock_play_rc = 0;

	/* 4. Play, Seek, Pause, and Stop. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Speed>1</Speed>"
		"</u:Play>", NULL), 0);
	EXPECT_TRUE(captured_transition_cb != NULL);

	/* Play while already PLAYING is a no-op (0). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Speed>1</Speed>"
		"</u:Play>", NULL), 0);

	/* SetAVTransportURI while PLAYING also updates current track URI. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetAVTransportURI",
		"<u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<CurrentURI>http://example.com/song1b.mp3</CurrentURI>"
		"<CurrentURIMetaData></CurrentURIMetaData>"
		"</u:SetAVTransportURI>", NULL), 0);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetTransportInfo",
		"<u:GetTransportInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetTransportInfo>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<CurrentTransportState>PLAYING</CurrentTransportState>") != NULL);
	free(resp);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetCurrentTransportActions",
		"<u:GetCurrentTransportActions xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:GetCurrentTransportActions>", &resp), 0);
	EXPECT_TRUE(resp != NULL);
	EXPECT_TRUE(strstr(resp, "<Actions>PAUSE,STOP,SEEK</Actions>") != NULL);
	free(resp);

	/* Pause failure when output_pause returns non-zero. */
	mock_pause_rc = -1;
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Pause",
		"<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Pause>", NULL), -1);
	mock_pause_rc = 0;

	/* Valid Pause transitions to PAUSED_PLAYBACK. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Pause",
		"<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Pause>", NULL), 0);

	/* Pause while already PAUSED_PLAYBACK is a no-op (0). */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Pause",
		"<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Pause>", NULL), 0);

	/* Resume Play from PAUSED_PLAYBACK. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Speed>1</Speed>"
		"</u:Play>", NULL), 0);

	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Seek",
		"<u:Seek xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<Unit>REL_TIME</Unit>"
		"<Target>0:01:30</Target>"
		"</u:Seek>", NULL), 0);
	EXPECT_TRUE(last_seek_nanos == 90LL * 1000000000LL);

	/* 5. SetNextAVTransportURI and trigger gapless transition. */
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

	/* Stop while PLAYING transitions to STOPPED, and second Stop is no-op. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Stop",
		"<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Stop>", NULL), 0);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Stop",
		"<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"</u:Stop>", NULL), 0);

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

	/* SetNextAVTransportURI with missing <NextURIMetaData> returns -1. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "SetNextAVTransportURI",
		"<u:SetNextAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
		"<InstanceID>0</InstanceID>"
		"<NextURI>http://example.com/nometa2.mp3</NextURI>"
		"</u:SetNextAVTransportURI>", NULL), -1);

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

	/* Missing InstanceID returns -1 across actions. */
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetMediaInfo",
		"<u:GetMediaInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetTransportInfo",
		"<u:GetTransportInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetPositionInfo",
		"<u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetCurrentTransportActions",
		"<u:GetCurrentTransportActions xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetDeviceCapabilities",
		"<u:GetDeviceCapabilities xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "GetTransportSettings",
		"<u:GetTransportSettings xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Play",
		"<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Pause",
		"<u:Pause xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
	EXPECT_INT_EQ(invoke_action_xml(
		srv, "Stop",
		"<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>",
		NULL), -1);
}

int main(void) {
	test_transport_lifecycle_and_transitions();
	test_missing_transport_arguments_do_not_crash();
	free(last_set_uri);
	free(last_next_uri);
	return 0;
}
