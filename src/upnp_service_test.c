/* upnp_service_test.c - Unit tests for upnp_service
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "upnp_service.h"
#include "variable-container.h"
#include "xmldoc.h"

static int dummy_cb(struct action_event *ev) {
	(void)ev;
	return 0;
}

static void test_find_action(void) {
	static struct action actions[] = {
		{ "Play", dummy_cb },
		{ "Stop", dummy_cb },
		{ NULL, NULL }
	};
	struct service srv;
	memset(&srv, 0, sizeof(srv));
	srv.actions = actions;

	EXPECT_TRUE(find_action(NULL, "Play") == NULL);

	const struct action *play = find_action(&srv, "Play");
	EXPECT_TRUE(play == &actions[0]);

	const struct action *stop = find_action(&srv, "Stop");
	EXPECT_TRUE(stop == &actions[1]);

	EXPECT_TRUE(find_action(&srv, "Pause") == NULL);
}

static void test_upnp_get_scpd(void) {
	static const char *play_modes[] = { "NORMAL", "REPEAT_ALL", NULL };
	static struct param_range vol_range = { 0, 100, 1 };

	static const struct var_meta meta[] = {
		{ 0, "CurrentPlayMode", "NORMAL", EV_NO, DATATYPE_STRING, play_modes, NULL },
		{ 1, "Volume", "50", EV_YES, DATATYPE_UI2, NULL, &vol_range },
	};

	static struct argument set_vol_args[] = {
		{ "DesiredVolume", PARAM_DIR_IN, 1 },
		{ NULL, 0, 0 }
	};
	static struct argument get_vol_args[] = {
		{ "CurrentVolume", PARAM_DIR_OUT, 1 },
		{ NULL, 0, 0 }
	};
	static struct argument *action_args[] = {
		set_vol_args,
		get_vol_args,
		NULL
	};
	static struct action actions[] = {
		{ "SetVolume", dummy_cb },
		{ "GetVolume", dummy_cb },
		{ NULL, NULL }
	};

	variable_container_t *vc = VariableContainer_new(2, meta);

	struct service srv;
	memset(&srv, 0, sizeof(srv));
	srv.actions = actions;
	srv.action_arguments = action_args;
	srv.variable_container = vc;
	srv.command_count = 2;

	char *scpd = upnp_get_scpd(&srv);
	EXPECT_TRUE(scpd != NULL);

	/* Verify key SCPD elements are present and parseable as valid XML. */
	struct xmldoc *doc = xmldoc_parsexml(scpd);
	EXPECT_TRUE(doc != NULL);
	struct xmlelement *root = find_element_in_doc(doc, "scpd");
	EXPECT_TRUE(root != NULL);
	EXPECT_TRUE(find_element_in_element(root, "specVersion") != NULL);
	EXPECT_TRUE(find_element_in_element(root, "actionList") != NULL);
	EXPECT_TRUE(find_element_in_element(root, "serviceStateTable") != NULL);
	xmldoc_free(doc);

	EXPECT_TRUE(strstr(scpd, "<name>SetVolume</name>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<name>DesiredVolume</name>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<direction>in</direction>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<direction>out</direction>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<relatedStateVariable>Volume</relatedStateVariable>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<allowedValue>REPEAT_ALL</allowedValue>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<defaultValue>NORMAL</defaultValue>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<minimum>0</minimum>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<maximum>100</maximum>") != NULL);
	EXPECT_TRUE(strstr(scpd, "<step>1</step>") != NULL);

	free(scpd);
	VariableContainer_delete(vc);
}

int main(void) {
	test_find_action();
	test_upnp_get_scpd();
	return 0;
}
