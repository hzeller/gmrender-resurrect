/* webserver_test.c - Unit tests for webserver
 *
 * This file is part of GMediaRender.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <upnp.h>

#include "test-helpers.h"
#include "upnp_compat.h"
#include "webserver.h"

static VDCallback_GetInfo captured_get_info = NULL;
static VDCallback_Open captured_open = NULL;
static VDCallback_Read captured_read = NULL;
static VDCallback_Write captured_write = NULL;
static VDCallback_Seek captured_seek = NULL;
static VDCallback_Close captured_close = NULL;

int UpnpVirtualDir_set_GetInfoCallback(VDCallback_GetInfo cb) {
	captured_get_info = cb;
	return UPNP_E_SUCCESS;
}
int UpnpVirtualDir_set_OpenCallback(VDCallback_Open cb) {
	captured_open = cb;
	return UPNP_E_SUCCESS;
}
int UpnpVirtualDir_set_ReadCallback(VDCallback_Read cb) {
	captured_read = cb;
	return UPNP_E_SUCCESS;
}
int UpnpVirtualDir_set_WriteCallback(VDCallback_Write cb) {
	captured_write = cb;
	return UPNP_E_SUCCESS;
}
int UpnpVirtualDir_set_SeekCallback(VDCallback_Seek cb) {
	captured_seek = cb;
	return UPNP_E_SUCCESS;
}
int UpnpVirtualDir_set_CloseCallback(VDCallback_Close cb) {
	captured_close = cb;
	return UPNP_E_SUCCESS;
}

#if UPNP_VERSION >= 11000
#define CALL_GET_INFO(fn, info) captured_get_info((fn), (info), NULL, NULL)
#define CALL_OPEN(fn, mode) captured_open((fn), (mode), NULL, NULL)
#define CALL_READ(fh, buf, len) captured_read((fh), (buf), (len), NULL, NULL)
#define CALL_SEEK(fh, off, orig) captured_seek((fh), (off), (orig), NULL, NULL)
#define CALL_CLOSE(fh) captured_close((fh), NULL, NULL)
#elif UPNP_VERSION >= 10803
#define CALL_GET_INFO(fn, info) captured_get_info((fn), (info), NULL)
#define CALL_OPEN(fn, mode) captured_open((fn), (mode), NULL)
#define CALL_READ(fh, buf, len) captured_read((fh), (buf), (len), NULL)
#define CALL_SEEK(fh, off, orig) captured_seek((fh), (off), (orig), NULL)
#define CALL_CLOSE(fh) captured_close((fh), NULL)
#else
#define CALL_GET_INFO(fn, info) captured_get_info((fn), (info))
#define CALL_OPEN(fn, mode) captured_open((fn), (mode))
#define CALL_READ(fh, buf, len) captured_read((fh), (buf), (len))
#define CALL_SEEK(fh, off, orig) captured_seek((fh), (off), (orig))
#define CALL_CLOSE(fh) captured_close((fh))
#endif

static void test_webserver_callbacks(void) {
	EXPECT_TRUE(webserver_register_callbacks() == TRUE);
	EXPECT_TRUE(captured_get_info != NULL);
	EXPECT_TRUE(captured_open != NULL);
	EXPECT_TRUE(captured_read != NULL);
	EXPECT_TRUE(captured_seek != NULL);
	EXPECT_TRUE(captured_close != NULL);

	/* Register a non-empty buffer and an empty buffer. */
	EXPECT_INT_EQ(webserver_register_buf("/upnp/test.xml", "0123456789", "text/xml"), 0);
	EXPECT_INT_EQ(webserver_register_buf("/upnp/empty.xml", "", "text/plain"), 0);

	/* Invalid path without '/' in webserver_register_file returns -1 safely. */
	EXPECT_INT_EQ(webserver_register_file("no_slash_path.png", "image/png"), -1);

	/* Test get_info (including checking that content_type is not leaked). */
	UpnpFileInfo *info = UpnpFileInfo_new();
	EXPECT_INT_EQ(CALL_GET_INFO("/upnp/nonexistent.xml", info), -1);
	EXPECT_INT_EQ(CALL_GET_INFO("/upnp/test.xml", info), 0);
	EXPECT_INT_EQ((int)UpnpFileInfo_get_FileLength(info), 10);
	EXPECT_STREQ(UpnpFileInfo_get_ContentType_cstr(info), "text/xml");
	UpnpFileInfo_delete(info);

	/* Test open, partial reads, seek, and EOF read. */
	EXPECT_TRUE(CALL_OPEN("/upnp/test.xml", UPNP_WRITE) == NULL);
	EXPECT_TRUE(CALL_OPEN("/upnp/nonexistent.xml", UPNP_READ) == NULL);

	UpnpWebFileHandle fh = CALL_OPEN("/upnp/test.xml", UPNP_READ);
	EXPECT_TRUE(fh != NULL);

	char buf[16];
	memset(buf, 0, sizeof(buf));
	EXPECT_INT_EQ(CALL_READ(fh, buf, 4), 4);
	EXPECT_STREQ(buf, "0123");

	/* Seek relative +2 -> pos 6. */
	EXPECT_INT_EQ(CALL_SEEK(fh, 2, SEEK_CUR), 0);
	memset(buf, 0, sizeof(buf));
	EXPECT_INT_EQ(CALL_READ(fh, buf, 10), 4);
	EXPECT_STREQ(buf, "6789");

	/* Read at EOF -> returns 0 without UB. */
	EXPECT_INT_EQ(CALL_READ(fh, buf, 4), 0);

	/* Out-of-bounds seeks fail with -1. */
	EXPECT_INT_EQ(CALL_SEEK(fh, -1, SEEK_SET), -1);
	EXPECT_INT_EQ(CALL_SEEK(fh, 11, SEEK_SET), -1);
	EXPECT_INT_EQ(CALL_SEEK(fh, 0, SEEK_END), 0);

	EXPECT_INT_EQ(CALL_CLOSE(fh), 0);
}

int main(void) {
	test_webserver_callbacks();
	return 0;
}
