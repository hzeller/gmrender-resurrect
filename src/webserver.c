/* webserver.c - Web server callback routines
 *
 * Copyright (C) 2005-2007   Ivo Clarysse
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <upnp/upnp.h>
#include <upnp/upnptools.h>  // UpnpGetErrorMessage
#include <upnp/ithread.h>

#include "logging.h"
#include "webserver.h"

typedef struct {
	off_t pos;
	const char *contents;
	size_t len;
} WebServerFile;

struct virtual_file;

static struct virtual_file {
	const char *virtual_fname;
	const char *contents;
	const char *content_type;
	size_t len;
	struct virtual_file *next;
} *virtual_files = NULL;

int webserver_register_buf(const char *path, const char *contents,
			   const char *content_type)
{
	struct virtual_file *entry;

	Log_info("webserver", "Provide %s (%s) from buffer",
		 path, content_type);

	assert(path != NULL);
	assert(contents != NULL);
	assert(content_type != NULL);

	entry = malloc(sizeof(struct virtual_file));
	if (entry == NULL) {
		return -1;
	}
	entry->len = strlen(contents);
	entry->contents = contents;
	entry->virtual_fname = path;
	entry->content_type = content_type;
	entry->next = virtual_files;
	virtual_files = entry;

	return 0;
}

int webserver_register_file(const char *path, const char *content_type)
{
	char local_fname[512];  // PATH_MAX, but that is not defined everywhere
	struct stat buf;
	struct virtual_file *entry;
	int rc;

	snprintf(local_fname, sizeof(local_fname), "%s%s", PKG_DATADIR,
	         strrchr(path, '/'));

	Log_info("webserver", "Provide %s (%s) from %s", path, content_type,
		 local_fname);

	rc = stat(local_fname, &buf);
	if (rc) {
		Log_error("webserver", "Could not stat '%s': %s",
			  local_fname, strerror(errno));
		return -1;
	}

	entry = malloc(sizeof(struct virtual_file));
	if (entry == NULL) {
		return -1;
	}
	if (buf.st_size) {
		char *cbuf;
		FILE *in;
		in = fopen(local_fname, "r");
		if (in == NULL) {
			free(entry);
			return -1;
		}
		cbuf = malloc(buf.st_size);
		if (cbuf == NULL) {
			free(entry);
			return -1;
		}
		if (fread(cbuf, buf.st_size, 1, in) != 1) {
			free(entry);
			free(cbuf);
			return -1;
		}
		fclose(in);
		entry->len = buf.st_size;
		entry->contents = cbuf;

	} else {
		entry->len = 0;
		entry->contents = NULL;
	}
	entry->virtual_fname = path;
	entry->content_type = content_type;
	entry->next = virtual_files;
	virtual_files = entry;

	return 0;
}

static int webserver_get_info(const char *filename, UpnpFileInfo *info)
{
	struct virtual_file *virtfile = virtual_files;

	while (virtfile != NULL) {
		if (strcmp(filename, virtfile->virtual_fname) == 0) {
			UpnpFileInfo_set_FileLength(info, virtfile->len);
			UpnpFileInfo_set_LastModified(info, 0);
			UpnpFileInfo_set_IsDirectory(info, 0);
			UpnpFileInfo_set_IsReadable(info, 1);
			UpnpFileInfo_set_ContentType(info,
			    ixmlCloneDOMString(virtfile->content_type));
			Log_info("webserver", "Access %s (%s) len=%zd",
				 filename, UpnpFileInfo_get_ContentType(info), virtfile->len);
			return 0;
		}
		virtfile = virtfile->next;
	}

	Log_info("webserver", "404 Not found. (attempt to access "
		 "non-existent '%s')", filename);

	return -1;
}

static UpnpWebFileHandle
webserver_open(const char *filename, enum UpnpOpenFileMode mode)
{
	if (mode != UPNP_READ) {
		Log_error("webserver",
			  "%s: ignoring request to open file for writing.",
			  filename);
		return NULL;
	}

	for (struct virtual_file *vf = virtual_files; vf; vf = vf->next) {
		if (strcmp(filename, vf->virtual_fname) == 0) {
			WebServerFile *file = malloc(sizeof(WebServerFile));
			file->pos = 0;
			file->len = vf->len;
			file->contents = vf->contents;
			return file;
		}
	}

	return NULL;
}

static inline int minimum(int a, int b)
{
	return (a<b)?a:b;
}

static int webserver_read(UpnpWebFileHandle fh, char *buf, size_t buflen)
{
	WebServerFile *file = (WebServerFile *) fh;
	ssize_t len = -1;

	len = minimum(buflen, file->len - file->pos);
	memcpy(buf, file->contents + file->pos, len);

	if (len < 0) {
		Log_error("webserver", "In %s: %s",
			  __FUNCTION__, strerror(errno));

	} else {
		file->pos += len;
	}

	return len;
}

static int webserver_write(UpnpWebFileHandle fh, char *buf, size_t buflen)
{
	return -1;
}

static int webserver_seek(UpnpWebFileHandle fh, off_t offset, int origin)
{
	WebServerFile *file = (WebServerFile *) fh;
	off_t newpos = -1;

	switch (origin) {
	case SEEK_SET:
		newpos = offset;
		break;
	case SEEK_CUR:
		newpos = file->pos + offset;
		break;
	case SEEK_END:
		newpos = file->len + offset;
		break;
	}

	if (newpos < 0 || newpos > (off_t) file->len) {
		Log_error("webserver", "in %s: seek failed with %s",
			  __FUNCTION__, strerror(errno));
		return -1;
	}

	file->pos = newpos;
	return 0;
}

static int webserver_close(UpnpWebFileHandle fh)
{
	WebServerFile *file = (WebServerFile *) fh;

	free(file);

	return 0;
}

#if (UPNP_VERSION < 10607)
// Older versions had a nice struct to register callbacks, just as you would
// expect from a proper C API
static struct UpnpVirtualDirCallbacks virtual_dir_callbacks = {
	webserver_get_info,
	webserver_open,
	webserver_read,
	webserver_write,
	webserver_seek,
	webserver_close
};

gboolean webserver_register_callbacks(void) {
  int rc = UpnpSetVirtualDirCallbacks(&virtual_dir_callbacks);
  if (UPNP_E_SUCCESS != rc) {
    Log_error("webserver", "UpnpSetVirtualDirCallbacks() Error: %s (%d)",
	      UpnpGetErrorMessage(rc), rc);
    return FALSE;
  }
  return TRUE;
}
#else
// With version 1.6.7 and above, the UPNP library maintainers made a questionable
// API choice to register the callbacks one-by-one, instead of having a
// struct with the callbacks which is certainly a better and 'typical' C API
// choice. They removed the UpnpVirtualDirCallbacks in the course of that.
// Because it breaks code, it has been reverted in version 1.6.16
// (see http://sourceforge.net/p/pupnp/bugs/29/ ), but we essentially have a
// broken version between 1.6.7 ... 1.6.16.
// Assuming that they will go on with this broken idea and eventually remove
// the support for the VirtualDirCallbacks in new major versions, we use the
// newer (may I emphasize: questionable) API to register the callbacks.
gboolean webserver_register_callbacks(void) {
  gboolean result =
    (UpnpVirtualDir_set_GetInfoCallback(webserver_get_info) == UPNP_E_SUCCESS
     && UpnpVirtualDir_set_OpenCallback(webserver_open) == UPNP_E_SUCCESS
     && UpnpVirtualDir_set_ReadCallback(webserver_read) == UPNP_E_SUCCESS
     && UpnpVirtualDir_set_WriteCallback(webserver_write) == UPNP_E_SUCCESS
     && UpnpVirtualDir_set_SeekCallback(webserver_seek) == UPNP_E_SUCCESS
     && UpnpVirtualDir_set_CloseCallback(webserver_close) == UPNP_E_SUCCESS);
  return result;
}
#endif
