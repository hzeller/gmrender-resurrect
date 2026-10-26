/* test-helpers.h - Minimal assertion macros for unit tests
 *
 * This file is part of GMediaRender.
 */

#ifndef _TEST_HELPERS_H
#define _TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT_TRUE(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: EXPECT_TRUE(%s) failed\n", \
			__FILE__, __LINE__, #cond); \
		exit(1); \
	} \
} while (0)

#define EXPECT_INT_EQ(actual, expected) do { \
	int _a = (actual); \
	int _e = (expected); \
	if (_a != _e) { \
		fprintf(stderr, "%s:%d: EXPECT_INT_EQ failed: %d != %d\n", \
			__FILE__, __LINE__, _a, _e); \
		exit(1); \
	} \
} while (0)

#define EXPECT_STREQ(actual, expected) do { \
	const char *_a = (actual); \
	const char *_e = (expected); \
	if (_a == NULL || _e == NULL || strcmp(_a, _e) != 0) { \
		fprintf(stderr, "%s:%d: EXPECT_STREQ failed:\n" \
			"  actual:   \"%s\"\n  expected: \"%s\"\n", \
			__FILE__, __LINE__, \
			_a ? _a : "(null)", _e ? _e : "(null)"); \
		exit(1); \
	} \
} while (0)

#endif /* _TEST_HELPERS_H */
