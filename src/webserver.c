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
#include <error.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <upnp/upnp.h>
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
	char local_fname[PATH_MAX];
	struct stat buf;
	struct virtual_file *entry;
	int rc;

	snprintf(local_fname, PATH_MAX, "%s%s", PKG_DATADIR,
	         strrchr(path, '/'));

	Log_info("webserver", "Provide %s (%s) from %s", path, content_type,
		 local_fname);

	rc = stat(local_fname, &buf);
	if (rc) {
		error(0, errno, "Could not stat '%s'", local_fname);
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
		fread(cbuf, buf.st_size, 1, in);
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

static int webserver_get_info(const char *filename, struct File_Info *info)
{
	struct virtual_file *virtfile = virtual_files;

	Log_info("webserver", "%s:(filename='%s',info=%p)", __FUNCTION__,
		 filename, info);

	while (virtfile != NULL) {
		if (strcmp(filename, virtfile->virtual_fname) == 0) {
			info->file_length = virtfile->len;
			info->last_modified = 0;
			info->is_directory = 0;
			info->is_readable = 1;
			info->content_type =
			    ixmlCloneDOMString(virtfile->content_type);
			return 0;
		}
		virtfile = virtfile->next;
	}

	Log_error("webserver", "Not found.");
	return -1;
}

static UpnpWebFileHandle
webserver_open(const char *filename, enum UpnpOpenFileMode mode)
{
	struct virtual_file *virtfile = virtual_files;

	if (mode != UPNP_READ) {
		Log_error("webserver",
			  "%s: ignoring request to open file for writing.",
			  filename);
		return NULL;
	}

	while (virtfile != NULL) {
		if (strcmp(filename, virtfile->virtual_fname) == 0) {
			WebServerFile *file = malloc(sizeof(WebServerFile));
			file->pos = 0;
			file->len = virtfile->len;
			file->contents = virtfile->contents;
			return file;
		}
		virtfile = virtfile->next;
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
		error(0, errno, "%s failed", __FUNCTION__);
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

	if (newpos < 0 || newpos > file->len) {
		error(0, errno, "%s seek failed", __FUNCTION__);
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

struct UpnpVirtualDirCallbacks virtual_dir_callbacks = {
	webserver_get_info,
	webserver_open,
	webserver_read,
	webserver_write,
	webserver_seek,
	webserver_close
};
