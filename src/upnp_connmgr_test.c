/* upnp_connmgr_test.c - Unit tests for upnp_connmgr
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "upnp_connmgr.h"
#include "upnp_service.h"
#include "variable-container.h"

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
	EXPECT_TRUE(find_action(srv, "GetProtocolInfo") != NULL);
	EXPECT_TRUE(find_action(srv, "GetCurrentConnectionIDs") != NULL);
	EXPECT_TRUE(find_action(srv, "GetCurrentConnectionInfo") != NULL);
	EXPECT_TRUE(find_action(srv, "PrepareForConnection") != NULL);
}

int main(void) {
	test_connmgr_mime_registration_and_filtering();
	test_connmgr_service_actions();
	return 0;
}
