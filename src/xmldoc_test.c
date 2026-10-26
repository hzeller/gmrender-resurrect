/* xmldoc_test.c - Unit tests for xmldoc
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "xmldoc.h"

static void test_build_and_parse_xml(void) {
	struct xmldoc *doc = xmldoc_new();
	EXPECT_TRUE(doc != NULL);

	struct xmlelement *root = xmldoc_new_topelement(
		doc, "root", "urn:example:ns");
	EXPECT_TRUE(root != NULL);

	add_value_element(doc, root, "greeting", "Hello & Welcome");
	add_value_element_int(doc, root, "count", -42);
	add_value_element_long(doc, root, "bigcount", 9876543210LL);

	struct xmlelement *attr_elem = add_attributevalue_element(
		doc, root, "item", "id", "item-1");
	EXPECT_TRUE(attr_elem != NULL);
	xmlelement_set_attribute(doc, attr_elem, "extra", "yes");
	add_value_element(doc, attr_elem, "nested", "inner-text");

	struct xmlelement *empty_elem = xmlelement_new(doc, "empty");
	xmlelement_add_element(doc, root, empty_elem);

	char *xml_str = xmldoc_tostring(doc);
	EXPECT_TRUE(xml_str != NULL);
	EXPECT_TRUE(strstr(xml_str, "xmlns=\"urn:example:ns\"") != NULL);
	EXPECT_TRUE(strstr(xml_str, "id=\"item-1\"") != NULL);
	EXPECT_TRUE(strstr(xml_str, "extra=\"yes\"") != NULL);

	/* Now parse the serialized XML back and verify elements. */
	struct xmldoc *parsed = xmldoc_parsexml(xml_str);
	EXPECT_TRUE(parsed != NULL);

	struct xmlelement *p_root = find_element_in_doc(parsed, "root");
	EXPECT_TRUE(p_root != NULL);
	EXPECT_TRUE(find_element_in_doc(parsed, "nonexistent") == NULL);

	struct xmlelement *p_greeting = find_element_in_element(
		p_root, "greeting");
	EXPECT_TRUE(p_greeting != NULL);
	char *val = get_node_value(p_greeting);
	EXPECT_STREQ(val, "Hello & Welcome");
	free(val);

	struct xmlelement *p_count = find_element_in_element(p_root, "count");
	EXPECT_TRUE(p_count != NULL);
	val = get_node_value(p_count);
	EXPECT_STREQ(val, "-42");
	free(val);

	struct xmlelement *p_big = find_element_in_element(p_root, "bigcount");
	EXPECT_TRUE(p_big != NULL);
	val = get_node_value(p_big);
	EXPECT_STREQ(val, "9876543210");
	free(val);

	struct xmlelement *p_item = find_element_in_element(p_root, "item");
	EXPECT_TRUE(p_item != NULL);
	struct xmlelement *p_nested = find_element_in_element(p_item, "nested");
	EXPECT_TRUE(p_nested != NULL);
	val = get_node_value(p_nested);
	EXPECT_STREQ(val, "inner-text");
	free(val);

	/* Empty element should return an empty allocated string "". */
	struct xmlelement *p_empty = find_element_in_element(p_root, "empty");
	EXPECT_TRUE(p_empty != NULL);
	val = get_node_value(p_empty);
	EXPECT_STREQ(val, "");
	free(val);

	EXPECT_TRUE(find_element_in_element(p_root, "missing") == NULL);

	xmldoc_free(parsed);
	free(xml_str);
	xmldoc_free(doc);
}

static void test_parse_invalid_xml(void) {
	struct xmldoc *doc = xmldoc_parsexml("<unclosed><tag>");
	EXPECT_TRUE(doc == NULL);
}

int main(void) {
	test_build_and_parse_xml();
	test_parse_invalid_xml();
	return 0;
}
