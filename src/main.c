/* main.c - Main program routines
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

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <glib.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <getopt.h>

#ifndef HAVE_LIBUPNP
# error "To have gmrender any useful, you need to have libupnp installed."
#endif

#include <upnp.h>
#include <ithread.h>

// For version strings of upnp and gstreamer
#include <upnpconfig.h>
#ifdef HAVE_GST
#  include <gst/gst.h>
#endif

#include "git-version.h"
#include "logging.h"
#include "output.h"
#include "upnp_service.h"
#include "upnp_control.h"
#include "upnp_device.h"
#include "upnp_renderer.h"
#include "upnp_transport.h"
#include "upnp_connmgr.h"

static int show_version = FALSE;
static int show_devicedesc = FALSE;
static int show_connmgr_scpd = FALSE;
static int show_control_scpd = FALSE;
static int show_transport_scpd = FALSE;
static int show_outputs = FALSE;
static int daemon_mode = FALSE;

// IP-address seems strange in libupnp: they actually don't bind to
// that address, but to INADDR_ANY (miniserver.c in upnp library).
// Apparently they just use this for the advertisement ? Anyway, 0.0.0.0 would
// not work.
static const char *ip_address = NULL;
static int listen_port = 49494;

#ifdef GMRENDER_UUID
// Compile-time uuid.
static const char *uuid = GMRENDER_UUID;
#else
static const char *uuid = "GMediaRender-1_0-000-000-002";
#endif
static const char *friendly_name = PACKAGE_NAME;
static const char *output = NULL;
static const char *pid_file = NULL;
static const char *log_file = NULL;
static const char *mime_filter = NULL;

/* Generic GMediaRender options */
static struct option option_entries[] = {
	/* These options set a flag. */
	{"version", no_argument, &show_version, 1},
	{"ip-address",   required_argument, NULL, 0},
	{"port",     required_argument,       0, 'p'},
	{"uuid",  required_argument, 0, 'u'},
	{"friendly-name",  required_argument, 0, 'f'},
	{"output",    required_argument, NULL, 0},
	{"pid-file",    required_argument, 0, 'P'},
	{"daemon",    no_argument, &daemon_mode, 1},
	{"mime-filter",    required_argument, 0, 0},
	{"logfile",    required_argument, 0, 0},
	{"list-outputs",    no_argument, &show_outputs, 1},
	{"dump-devicedesc",    no_argument, &show_devicedesc, 1},
	{"dump-connmgr-scpd",    no_argument, &show_connmgr_scpd, 1},
	{"dump-control-scpd",    no_argument, &show_control_scpd, 1},
	{"dump-transport-scpd",    no_argument, &show_transport_scpd, 1},
	{0, 0, 0, 0}
};

// Fill buffer with version information. Returns pointer to beginning of string.
static const char *GetVersionInfo(char *buffer, size_t len) {
#ifdef HAVE_GST
	snprintf(buffer, len, "gmediarender %s "
		 "(libupnp-%s; glib-%d.%d.%d; gstreamer-%d.%d.%d)",
		 GM_COMPILE_VERSION, UPNP_VERSION_STRING,
		 GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
		 GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
#else
	snprintf(buffer, len, "gmediarender %s "
		 "(libupnp-%s; glib-%d.%d.%d; without gstreamer.)",
		 GM_COMPILE_VERSION, UPNP_VERSION_STRING,
		 GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
#endif
	return buffer;
}

static void do_show_version(void)
{
	char version[1024];
	GetVersionInfo(version, sizeof(version));

	printf("%s; %s\n"
	       "This is free software. "
	       "You may redistribute copies of it under the terms of\n"
	       "the GNU General Public License "
	       "<http://www.gnu.org/licenses/gpl.html>.\n"
	       "There is NO WARRANTY, to the extent permitted by law.\n",
	       PACKAGE_STRING, version);
}

static int process_cmdline(int *argc, char **argv[])
{
	int c;
	while (1) {
		int option_index = 0;

		c = getopt_long (*argc, *argv, "I:p:u:f:o:P:d",
			option_entries, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
		break;

		switch (c) {
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (option_entries[option_index].flag != 0)
				break;
			if (!strcmp(option_entries[option_index].name, "mime-filter"))
				mime_filter = optarg;
			if (!strcmp(option_entries[option_index].name, "logfile"))
				log_file = optarg;
		break;
		case 'I':
			ip_address = optarg;
		break;
		case 'p':
			listen_port = atoi(optarg);
		break;
		case 'u':
			uuid = optarg;
		break;
		case 'f':
			friendly_name = optarg;
		break;
		case 'o':
			output = optarg;
		break;
		case 'P':
			pid_file = optarg;
		break;
		case 'd':
			daemon_mode = 1;
		break;
		}
	}
	*argc -= optind - 1;
	int i;
	for (i = 0; i < *argc; i++)
		(*argv)[i + 1] = (*argv)[i + optind];
	return 1;
}

static void log_variable_change(void *userdata, int var_num,
				const char *variable_name,
				const char *old_value,
				const char *variable_value) {
	(void)var_num;
	(void)old_value;

	const char *category = (const char*) userdata;
	int needs_newline = variable_value[strlen(variable_value) - 1] != '\n';
	// Silly terminal codes. Set to empty strings if not needed.
	const char *var_start = Log_color_allowed() ? "\033[1m\033[34m" : "";
	const char *var_end = Log_color_allowed() ? "\033[0m" : "";
	Log_info(category, "%s%s%s: %s%s",
		 var_start, variable_name, var_end,
		 variable_value, needs_newline ? "\n" : "");
}

static void init_logging(const char *log_file) {
	char version[1024];
	GetVersionInfo(version, sizeof(version));

	if (log_file != NULL) {
		Log_init(log_file);
		Log_info("main", "%s log started [ %s ]",
			 PACKAGE_STRING, version);

	} else {
		fprintf(stderr, "%s started [ %s ].\nLogging switched off. "
			"Enable with --logfile=<filename> "
			"(or --logfile=stdout for console)\n",
			PACKAGE_STRING, version);
	}
}

int main(int argc, char **argv)
{
	int rc;
	struct upnp_device_descriptor *upnp_renderer;

#if !GLIB_CHECK_VERSION(2,32,0)
	g_thread_init (NULL);  // Was necessary < glib 2.32, deprecated since.
#endif

	if (!process_cmdline(&argc, &argv)) {
		return EXIT_FAILURE;
	}

	if (show_version) {
		do_show_version();
		exit(EXIT_SUCCESS);
	}
	if (show_connmgr_scpd) {
		upnp_renderer_dump_connmgr_scpd();
		exit(EXIT_SUCCESS);
	}
	if (show_control_scpd) {
		upnp_renderer_dump_control_scpd();
		exit(EXIT_SUCCESS);
	}
	if (show_transport_scpd) {
		upnp_renderer_dump_transport_scpd();
		exit(EXIT_SUCCESS);
	}

	if (show_outputs) {
		output_dump_modules();
		exit(EXIT_SUCCESS);
	}

	init_logging(log_file);

	// Now we're going to start threads etc, which means we need
	// to become a daemon before that.

	// We need to open the pid-file now because relative filenames will
	// break if we're becoming a daemon and cwd changes.
	FILE *pid_file_stream = NULL;
	if (pid_file) {
		pid_file_stream = fopen(pid_file, "w");
	}
	// TODO: check for availability of daemon() in configure.
	if (daemon_mode) {
		if (daemon(0, 0) < 0) {
			perror("Becoming daemon: ");
			return EXIT_FAILURE;
		}
	}
	if (pid_file_stream) {
		fprintf(pid_file_stream, "%d\n", getpid());
		fclose(pid_file_stream);
	}

	upnp_renderer = upnp_renderer_descriptor(friendly_name, uuid, mime_filter);
	if (upnp_renderer == NULL) {
		return EXIT_FAILURE;
	}

	output_add_options(&argc, &argv);
	rc = output_init(output);
	if (rc != 0) {
		Log_error("main",
			  "ERROR: Failed to initialize Output subsystem");
		return EXIT_FAILURE;
	}

	struct upnp_device *device;
	if (listen_port != 0 &&
	    (listen_port < 49152 || listen_port > 65535)) {
		// Somewhere obscure internally in libupnp, they clamp the
		// port to be outside of the IANA range, so at least 49152.
		// Instead of surprising the user by ignoring lower port
		// numbers, complain loudly.
		Log_error("main", "Parameter error: --port needs to be in "
			  "range [49152..65535] (but was set to %d)",
			  listen_port);
		return EXIT_FAILURE;
	}
	device = upnp_device_init(upnp_renderer, ip_address, listen_port);
	if (device == NULL) {
		Log_error("main", "ERROR: Failed to initialize UPnP device");
		return EXIT_FAILURE;
	}

	upnp_transport_init(device);
	upnp_control_init(device);

	if (show_devicedesc) {
		// This can only be run after all services have been
		// initialized.
		char *buf = upnp_create_device_desc(upnp_renderer);
		assert(buf != NULL);
		fputs(buf, stdout);
		exit(EXIT_SUCCESS);
	}

	if (Log_info_enabled()) {
		upnp_transport_register_variable_listener(log_variable_change,
							  (void*) "transport");
		upnp_control_register_variable_listener(log_variable_change,
							(void*) "control");
	}

	// Write both to the log (which might be disabled) and console.
	Log_info("main", "Ready for rendering.");
	fprintf(stderr, "Ready for rendering.\n");

	output_loop();

	// We're here, because the loop exited. Probably due to catching
	// a signal.
	Log_info("main", "Exiting.");
	upnp_device_shutdown(device);

	return EXIT_SUCCESS;
}
