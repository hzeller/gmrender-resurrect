/* upnp_renderer_test.c - Unit tests for upnp_renderer.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "output.h"
#include "test-helpers.h"
#include "upnp_device.h"
#include "upnp_renderer.h"

/* Stubs for output subsystem used by transport and control services */
void output_set_uri(const char *uri, output_update_meta_cb_t meta_info) {
	(void)uri;
	(void)meta_info;
}
void output_set_next_uri(const char *uri) {
	(void)uri;
}
int output_play(output_transition_cb_t callback) {
	(void)callback;
	return 0;
}
int output_stop(void) { return 0; }
int output_pause(void) { return 0; }
int output_seek(gint64 position_nanos) {
	(void)position_nanos;
	return 0;
}
int output_get_position(gint64 *track_duration, gint64 *track_pos) {
	*track_duration = 0;
	*track_pos = 0;
	return 0;
}
int output_get_volume(float *v) {
	*v = 1.0f;
	return 0;
}
int output_set_volume(float v) {
	(void)v;
	return 0;
}
int output_get_mute(int *m) {
	*m = 0;
	return 0;
}
int output_set_mute(int m) {
	(void)m;
	return 0;
}

static void test_renderer_descriptor(void) {
	struct upnp_device_descriptor *desc1 =
		upnp_renderer_descriptor("Living Room", "1234-5678", "audio");
	EXPECT_TRUE(desc1 != NULL);
	EXPECT_STREQ(desc1->friendly_name, "Living Room");
	EXPECT_STREQ(desc1->udn, "uuid:1234-5678");
	EXPECT_STREQ(desc1->mime_filter, "audio");

	/* Calling upnp_renderer_descriptor again should not leak previous udn */
	struct upnp_device_descriptor *desc2 =
		upnp_renderer_descriptor("Bedroom", "abcd-ef01", NULL);
	EXPECT_TRUE(desc2 == desc1);
	EXPECT_STREQ(desc2->friendly_name, "Bedroom");
	EXPECT_STREQ(desc2->udn, "uuid:abcd-ef01");
	EXPECT_TRUE(desc2->mime_filter == NULL);

	/* Calling init_function populates services and initializes connmgr */
	EXPECT_TRUE(desc2->init_function != NULL);
	EXPECT_INT_EQ(desc2->init_function(), 0);
	EXPECT_TRUE(desc2->services != NULL);
	EXPECT_TRUE(desc2->services[0] != NULL);
	EXPECT_TRUE(desc2->services[1] != NULL);
	EXPECT_TRUE(desc2->services[2] != NULL);
	EXPECT_TRUE(desc2->services[3] == NULL);

	/* Device description generation should succeed */
	char *device_xml = upnp_create_device_desc(desc2);
	EXPECT_TRUE(device_xml != NULL);
	EXPECT_TRUE(strstr(device_xml, "<friendlyName>Bedroom</friendlyName>") != NULL);
	EXPECT_TRUE(strstr(device_xml, "<UDN>uuid:abcd-ef01</UDN>") != NULL);
	free(device_xml);
}

static void test_renderer_dump_scpd(void) {
	/* Redirect stdout to /dev/null temporarily so test output stays clean,
	 * while exercising the dump functions under LeakSanitizer. */
	fflush(stdout);
	int saved_stdout = dup(STDOUT_FILENO);
	FILE *null_out = fopen("/dev/null", "w");
	if (null_out != NULL) {
		dup2(fileno(null_out), STDOUT_FILENO);
		fclose(null_out);
	}

	upnp_renderer_dump_connmgr_scpd();
	upnp_renderer_dump_control_scpd();
	upnp_renderer_dump_transport_scpd();
	fflush(stdout);

	if (saved_stdout >= 0) {
		dup2(saved_stdout, STDOUT_FILENO);
		close(saved_stdout);
	}
}

int main(void) {
	test_renderer_descriptor();
	test_renderer_dump_scpd();

	return 0;
}
