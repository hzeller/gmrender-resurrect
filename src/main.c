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

#define _GNU_SOURCE

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

#ifndef HAVE_LIBUPNP
# error "To have gmrender any useful, you need to have libupnp installed."
#endif

#include <upnp/upnp.h>
#include <upnp/ithread.h>

// For version strings of upnp and gstreamer
#include <upnp/upnpconfig.h>
#ifdef HAVE_GST
#  include <gst/gst.h>
#endif

#include "git-version.h"
#include "logging.h"
#include "output.h"
#include "upnp.h"
#include "upnp_control.h"
#include "upnp_device.h"
#include "upnp_renderer.h"
#include "upnp_transport.h"

static gboolean show_version = FALSE;
static gboolean show_devicedesc = FALSE;
static gboolean show_connmgr_scpd = FALSE;
static gboolean show_control_scpd = FALSE;
static gboolean show_transport_scpd = FALSE;
static gboolean show_outputs = FALSE;
static gboolean daemon_mode = FALSE;

// IP-address seems strange in libupnp: they actually don't bind to
// that address, but to INADDR_ANY (miniserver.c in upnp library).
// Apparently they just use this for the advertisement ? Anyway, 0.0.0.0 would
// not work.
static const gchar *ip_address = NULL;
static int listen_port = 49494;

#ifdef GMRENDER_UUID
// Compile-time uuid.
static const gchar *uuid = GMRENDER_UUID;
#else
static const gchar *uuid = "GMediaRender-1_0-000-000-002";
#endif
static const gchar *friendly_name = PACKAGE_NAME;
static const gchar *output = NULL;
static const gchar *pid_file = NULL;
static const gchar *log_file = NULL;
static const gchar *mime_filter = NULL;

/* Generic GMediaRender options */
static GOptionEntry option_entries[] = {
	{ "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
	  "Output version information and exit", NULL },
	{ "ip-address", 'I', 0, G_OPTION_ARG_STRING, &ip_address,
	  "The local IP address the service is running and advertised "
	  "(only one, 0.0.0.0 won't work)", NULL },
	// The following is not very reliable, as libupnp does not set
	// SO_REUSEADDR by default, so it might increment (sending patch).
	{ "port", 'p', 0, G_OPTION_ARG_INT, &listen_port,
	  "Port to listen to; [49152..65535] (libupnp does not use "
	  "SO_REUSEADDR, so might increment)", NULL },
	{ "uuid", 'u', 0, G_OPTION_ARG_STRING, &uuid,
	  "UUID to advertise", NULL },
	{ "friendly-name", 'f', 0, G_OPTION_ARG_STRING, &friendly_name,
	  "Friendly name to advertise.", NULL },
	{ "output", 'o', 0, G_OPTION_ARG_STRING, &output,
	  "Output module to use.", NULL },
	{ "pid-file", 'P', 0, G_OPTION_ARG_STRING, &pid_file,
	  "File the process ID should be written to.", NULL },
	{ "daemon", 'd', 0, G_OPTION_ARG_NONE, &daemon_mode,
	  "Run as daemon.", NULL },
	{ "mime-filter", 0, 0, G_OPTION_ARG_STRING, &mime_filter,
	  "Top-level MIME type to advertise support for. e.g. audio,video,image", NULL },
	{ "logfile", 0, 0, G_OPTION_ARG_STRING, &log_file,
	  "Debug log filename. Use 'stdout' or 'stderr' to log to console.", NULL },
	{ "list-outputs", 0, 0, G_OPTION_ARG_NONE, &show_outputs,
	  "List available output modules and exit", NULL },
	{ "dump-devicedesc", 0, 0, G_OPTION_ARG_NONE, &show_devicedesc,
	  "Dump device descriptor XML and exit.", NULL },
	{ "dump-connmgr-scpd", 0, 0, G_OPTION_ARG_NONE, &show_connmgr_scpd,
	  "Dump Connection Manager service description XML and exit.", NULL },
	{ "dump-control-scpd", 0, 0, G_OPTION_ARG_NONE, &show_control_scpd,
	  "Dump Rendering Control service description XML and exit.", NULL },
	{ "dump-transport-scpd", 0, 0, G_OPTION_ARG_NONE, &show_transport_scpd,
	  "Dump A/V Transport service description XML and exit.", NULL },
	{ NULL }
};

static void do_show_version(void)
{
	puts( PACKAGE_STRING "; " GM_COMPILE_VERSION "\n"
        	"This is free software. "
		"You may redistribute copies of it under the terms of\n"
		"the GNU General Public License "
		"<http://www.gnu.org/licenses/gpl.html>.\n"
		"There is NO WARRANTY, to the extent permitted by law."
	);
}

static gboolean process_cmdline(int argc, char **argv)
{
	GOptionContext *ctx;
	GError *err = NULL;
	int rc;

	ctx = g_option_context_new("- GMediaRender");
	g_option_context_add_main_entries(ctx, option_entries, NULL);

	rc = output_add_options(ctx);
	if (rc != 0) {
		fprintf(stderr, "Failed to add output options\n");
		return FALSE;
	}

	if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
		fprintf(stderr, "Failed to initialize: %s\n", err->message);
		g_error_free (err);
		return FALSE;
	}

	return TRUE;
}

static void log_variable_change(void *userdata, int var_num,
				const char *variable_name,
				const char *old_value,
				const char *variable_value) {
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

#ifdef HAVE_GST
	snprintf(version, sizeof(version), "[ gmediarender %s "
		 "(libupnp-%s; glib-%d.%d.%d; gstreamer-%d.%d.%d) ]",
		 GM_COMPILE_VERSION, UPNP_VERSION_STRING,
		 GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
		 GST_VERSION_MAJOR, GST_VERSION_MINOR, GST_VERSION_MICRO);
#else
	snprintf(version, sizeof(version), "[ gmediarender %s "
		 "(libupnp-%s; glib-%d.%d.%d; without gstreamer.) ]",
		 GM_COMPILE_VERSION, UPNP_VERSION_STRING,
		 GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
#endif

	if (log_file != NULL) {
		Log_init(log_file);
		Log_info("main", "%s log started %s", PACKAGE_STRING, version);

	} else {
		fprintf(stderr, "%s started %s.\nLogging switched off. "
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

	if (!process_cmdline(argc, argv)) {
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

	upnp_renderer = upnp_renderer_descriptor(friendly_name, uuid);
	if (upnp_renderer == NULL) {
		return EXIT_FAILURE;
	}

	upnp_renderer_set_mime_filter(mime_filter);

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
