/* variable-container_test.c - Unit tests for variable-container
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "variable-container.h"
#include "upnp_device.h"
#include "upnp_service.h"

/* Mock implementation of upnp_device_notify used by UPnPLastChangeCollector. */
static struct {
	int call_count;
	const char *last_service_id;
	char *last_varname;
	char *last_varvalue;
	int last_varcount;
} mock_notify;

static void reset_mock_notify(void) {
	mock_notify.call_count = 0;
	mock_notify.last_service_id = NULL;
	free(mock_notify.last_varname);
	mock_notify.last_varname = NULL;
	free(mock_notify.last_varvalue);
	mock_notify.last_varvalue = NULL;
	mock_notify.last_varcount = 0;
}

int upnp_device_notify(struct upnp_device *device,
		       const char *serviceID,
		       const char **varnames,
		       const char **varvalues,
		       int varcount) {
	(void)device;
	mock_notify.call_count++;
	mock_notify.last_service_id = serviceID;
	mock_notify.last_varcount = varcount;
	if (varcount > 0) {
		free(mock_notify.last_varname);
		mock_notify.last_varname = strdup(varnames[0]);
		free(mock_notify.last_varvalue);
		mock_notify.last_varvalue = strdup(varvalues[0]);
	}
	return 0;
}

struct callback_record {
	int call_count;
	int last_var_num;
	const char *last_var_name;
	char *last_old_val;
	char *last_new_val;
};

static void record_callback(void *userdata,
			    int var_num, const char *var_name,
			    const char *old_value, const char *new_value) {
	struct callback_record *rec = (struct callback_record *)userdata;
	rec->call_count++;
	rec->last_var_num = var_num;
	rec->last_var_name = var_name;
	free(rec->last_old_val);
	rec->last_old_val = strdup(old_value);
	free(rec->last_new_val);
	rec->last_new_val = strdup(new_value);
}

static void test_variable_container(void) {
	/* Pass metadata in intentionally unsorted order to test sorting by id. */
	static const struct var_meta vars[] = {
		{ 2, "Volume", "50", EV_YES, DATATYPE_UI2, NULL, NULL },
		{ 0, "TransportState", "STOPPED", EV_YES, DATATYPE_STRING, NULL, NULL },
		{ 1, "CurrentTrackURI", "", EV_YES, DATATYPE_STRING, NULL, NULL },
	};

	variable_container_t *vc = VariableContainer_new(3, vars);
	EXPECT_TRUE(vc != NULL);
	EXPECT_INT_EQ(VariableContainer_get_num_vars(vc), 3);

	int meta_count = 0;
	const struct var_meta *sorted_meta = VariableContainer_get_meta(vc, &meta_count);
	EXPECT_INT_EQ(meta_count, 3);
	EXPECT_STREQ(sorted_meta[0].name, "TransportState");
	EXPECT_STREQ(sorted_meta[1].name, "CurrentTrackURI");
	EXPECT_STREQ(sorted_meta[2].name, "Volume");

	const char *name = NULL;
	EXPECT_STREQ(VariableContainer_get(vc, 0, &name), "STOPPED");
	EXPECT_STREQ(name, "TransportState");
	EXPECT_STREQ(VariableContainer_get(vc, 2, &name), "50");
	EXPECT_STREQ(name, "Volume");
	EXPECT_TRUE(VariableContainer_get(vc, -1, NULL) == NULL);
	EXPECT_TRUE(VariableContainer_get(vc, 3, NULL) == NULL);

	struct callback_record rec = { 0, -1, NULL, NULL, NULL };
	VariableContainer_register_callback(vc, record_callback, &rec);

	/* Changing to identical value returns 0 and does not fire callback. */
	EXPECT_INT_EQ(VariableContainer_change(vc, 0, "STOPPED"), 0);
	EXPECT_INT_EQ(rec.call_count, 0);

	/* Changing to a new value returns 1 and fires callback. */
	EXPECT_INT_EQ(VariableContainer_change(vc, 0, "PLAYING"), 1);
	EXPECT_INT_EQ(rec.call_count, 1);
	EXPECT_INT_EQ(rec.last_var_num, 0);
	EXPECT_STREQ(rec.last_var_name, "TransportState");
	EXPECT_STREQ(rec.last_old_val, "STOPPED");
	EXPECT_STREQ(rec.last_new_val, "PLAYING");
	EXPECT_STREQ(VariableContainer_get(vc, 0, NULL), "PLAYING");

	/* NULL value is treated as empty string "". */
	EXPECT_INT_EQ(VariableContainer_change(vc, 0, NULL), 1);
	EXPECT_STREQ(VariableContainer_get(vc, 0, NULL), "");

	free(rec.last_old_val);
	free(rec.last_new_val);
	VariableContainer_delete(vc);
}

static void test_last_change_builder(void) {
	upnp_last_change_builder_t *builder =
		UPnPLastChangeBuilder_new("urn:schemas-upnp-org:metadata-1-0/RCS/");
	EXPECT_TRUE(builder != NULL);

	/* Empty builder produces NULL XML. */
	EXPECT_TRUE(UPnPLastChangeBuilder_to_xml(builder) == NULL);

	UPnPLastChangeBuilder_add(builder, "TransportState", "PLAYING");
	UPnPLastChangeBuilder_add(builder, "Volume", "75");
	UPnPLastChangeBuilder_add(builder, "Mute", "0");

	char *xml = UPnPLastChangeBuilder_to_xml(builder);
	EXPECT_TRUE(xml != NULL);
	EXPECT_TRUE(strstr(xml, "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/RCS/\">") != NULL);
	EXPECT_TRUE(strstr(xml, "<InstanceID val=\"0\">") != NULL);
	EXPECT_TRUE(strstr(xml, "<TransportState val=\"PLAYING\"") != NULL);
	/* Volume and Mute get channel="Master" attribute. */
	EXPECT_TRUE(strstr(xml, "<Volume val=\"75\" channel=\"Master\"") != NULL);
	EXPECT_TRUE(strstr(xml, "<Mute val=\"0\" channel=\"Master\"") != NULL);
	free(xml);

	/* Builder is reset after to_xml(). */
	EXPECT_TRUE(UPnPLastChangeBuilder_to_xml(builder) == NULL);

	UPnPLastChangeBuilder_delete(builder);
}

static void test_last_change_collector(void) {
	static const struct var_meta vars[] = {
		{ 0, "TransportState", "STOPPED", EV_YES, DATATYPE_STRING, NULL, NULL },
		{ 1, "IgnoredVar", "init", EV_NO, DATATYPE_STRING, NULL, NULL },
		{ 2, "Volume", "50", EV_YES, DATATYPE_UI2, NULL, NULL },
		{ 3, "LastChange", "<Event/>", EV_YES, DATATYPE_STRING, NULL, NULL },
	};

	reset_mock_notify();
	variable_container_t *vc = VariableContainer_new(4, vars);
	struct upnp_device *dummy_dev = (struct upnp_device *)0x1;

	/* Creating collector triggers an initial LastChange notification. */
	upnp_last_change_collector_t *collector = UPnPLastChangeCollector_new(
		vc, "urn:schemas-upnp-org:metadata-1-0/AVT/",
		dummy_dev, "urn:upnp-org:serviceId:AVTransport");
	EXPECT_TRUE(collector != NULL);
	EXPECT_INT_EQ(mock_notify.call_count, 1);
	EXPECT_STREQ(mock_notify.last_service_id, "urn:upnp-org:serviceId:AVTransport");
	EXPECT_STREQ(mock_notify.last_varname, "LastChange");
	/* The notified value is XML-escaped. */
	EXPECT_TRUE(strstr(mock_notify.last_varvalue, "&lt;TransportState val=\"STOPPED\"&gt;") != NULL);

	/* Ignore variable 1 ("IgnoredVar") and bit 31 (no signed shift UB). */
	UPnPLastChangeCollector_add_ignore(collector, 1);
	UPnPLastChangeCollector_add_ignore(collector, 31);
	VariableContainer_change(vc, 1, "changed-but-ignored");
	EXPECT_INT_EQ(mock_notify.call_count, 1);

	/* Transaction batching (nested start/finish). */
	UPnPLastChangeCollector_start(collector);
	UPnPLastChangeCollector_start(collector);
	VariableContainer_change(vc, 0, "PLAYING");
	VariableContainer_change(vc, 2, "80");
	/* Still inside transaction: no notification yet. */
	EXPECT_INT_EQ(mock_notify.call_count, 1);

	UPnPLastChangeCollector_finish(collector);
	EXPECT_INT_EQ(mock_notify.call_count, 1);

	/* Outermost finish sends single combined notification. */
	UPnPLastChangeCollector_finish(collector);
	EXPECT_INT_EQ(mock_notify.call_count, 2);
	EXPECT_TRUE(strstr(mock_notify.last_varvalue, "&lt;TransportState val=\"PLAYING\"&gt;") != NULL);
	EXPECT_TRUE(strstr(mock_notify.last_varvalue, "&lt;Volume val=\"80\" channel=\"Master\"&gt;") != NULL);

	reset_mock_notify();
	UPnPLastChangeCollector_delete(collector);
	VariableContainer_delete(vc);
}

int main(void) {
	test_variable_container();
	test_last_change_builder();
	test_last_change_collector();
	return 0;
}
