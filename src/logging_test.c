/* logging_test.c - Unit tests for logging.c
 *
 * This file is part of GMediaRender.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logging.h"
#include "test-helpers.h"

static void test_logging_to_temp_file(void) {
	char tmpl[] = "/tmp/gmrender-log-test-XXXXXX";
	int fd = mkstemp(tmpl);
	EXPECT_TRUE(fd >= 0);
	close(fd);

	Log_init(NULL);
	EXPECT_INT_EQ(Log_info_enabled(), 0);
	EXPECT_INT_EQ(Log_error_enabled(), 1);

	Log_init(tmpl);
	EXPECT_INT_EQ(Log_info_enabled(), 1);
	EXPECT_INT_EQ(Log_color_allowed(), 0);

	/* Log without trailing newline, with trailing newline, and empty message. */
	Log_info("test-cat", "Hello world %d", 42);
	Log_info("test-cat", "Already has newline\n");
	Log_info("test-cat", "%s", "");
	Log_error("err-cat", "Error code %d", -1);

	FILE *f = fopen(tmpl, "r");
	EXPECT_TRUE(f != NULL);
	char buf[1024];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = '\0';
	fclose(f);
	unlink(tmpl);

	EXPECT_TRUE(strstr(buf, "INFO  [") != NULL);
	EXPECT_TRUE(strstr(buf, "| test-cat] Hello world 42\n") != NULL);
	EXPECT_TRUE(strstr(buf, "| test-cat] Already has newline\n") != NULL);
	/* Ensure no double newline was added after "Already has newline\n". */
	EXPECT_TRUE(strstr(buf, "Already has newline\n\n") == NULL);
	EXPECT_TRUE(strstr(buf, "ERROR [") != NULL);
	EXPECT_TRUE(strstr(buf, "| err-cat] Error code -1\n") != NULL);
}

int main(void) {
	test_logging_to_temp_file();
	return 0;
}
