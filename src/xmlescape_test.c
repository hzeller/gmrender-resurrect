/* xmlescape_test.c - Unit tests for xmlescape
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "xmlescape.h"

static void test_plain_and_empty(void) {
	EXPECT_TRUE(xmlescape(NULL, 0) == NULL);

	char *res = xmlescape("", 0);
	EXPECT_STREQ(res, "");
	free(res);

	res = xmlescape("Hello, World! 12345", 0);
	EXPECT_STREQ(res, "Hello, World! 12345");
	free(res);
}

static void test_element_escaping(void) {
	/* In element text (attribute == 0), <, >, & are escaped, but " is not. */
	char *res = xmlescape("Rock & Roll <Live> \"Quote\"", 0);
	EXPECT_STREQ(res, "Rock &amp; Roll &lt;Live&gt; \"Quote\"");
	free(res);
}

static void test_attribute_escaping(void) {
	/* In attribute text (attribute == 1), " is also escaped as %22. */
	char *res = xmlescape("Rock & Roll <Live> \"Quote\"", 1);
	EXPECT_STREQ(res, "Rock &amp; Roll &lt;Live&gt; %22Quote%22");
	free(res);
}

static void test_only_special_chars(void) {
	char *res = xmlescape("<>&\"", 0);
	EXPECT_STREQ(res, "&lt;&gt;&amp;\"");
	free(res);

	res = xmlescape("<>&\"", 1);
	EXPECT_STREQ(res, "&lt;&gt;&amp;%22");
	free(res);
}

int main(void) {
	test_plain_and_empty();
	test_element_escaping();
	test_attribute_escaping();
	test_only_special_chars();
	return 0;
}
