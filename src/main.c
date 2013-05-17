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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <glib.h>

#ifdef HAVE_LIBUPNP
# include <upnp/ithread.h>
#else
# error "To have gmrender any useful, you need to have libupnp installed."
#endif

#include "logging.h"
//#include "output_gstreamer.h"
#include "output.h"
#include "upnp.h"
#include "upnp_device.h"
#include "upnp_renderer.h"
#include "upnp_transport.h"
#include "upnp_control.h"

static gboolean show_version = FALSE;
static gboolean show_devicedesc = FALSE;
static gboolean show_connmgr_scpd = FALSE;
static gboolean show_control_scpd = FALSE;
static gboolean show_transport_scpd = FALSE;
static gboolean show_outputs = FALSE;
static gboolean daemon_mode = FALSE;
static const gchar *ip_address = NULL;
#ifdef GMRENDER_UUID
// Compile-time uuid.
static const gchar *uuid = GMRENDER_UUID;
#else
static const gchar *uuid = "GMediaRender-1_0-000-000-002";
#endif
static const gchar *friendly_name = PACKAGE_NAME;
static const gchar *output = NULL;
static const gchar *pid_file = NULL;
 
/* Generic GMediaRender options */
static GOptionEntry option_entries[] = {
	{ "version", 0, 0, G_OPTION_ARG_NONE, &show_version,
	  "Output version information and exit", NULL },
	{ "ip-address", 'I', 0, G_OPTION_ARG_STRING, &ip_address,
	  "IP address on which to listen.", NULL },
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
	puts( PACKAGE_STRING "\n"
        	"This is free software. "
		"You may redistribute copies of it under the terms of\n"
		"the GNU General Public License "
		"<http://www.gnu.org/licenses/gpl.html>.\n"
		"There is NO WARRANTY, to the extent permitted by law."
	);
}

static int process_cmdline(int argc, char **argv)
{
	int result = -1;
	GOptionContext *ctx;
	GError *err = NULL;
	int rc;

	ctx = g_option_context_new("- GMediaRender");
	g_option_context_add_main_entries(ctx, option_entries, NULL);

	rc = output_add_options(ctx);
	if (rc != 0) {
		fprintf(stderr, "Failed to add output options\n");
		goto out;
	}

	if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
		g_print ("Failed to initialize: %s\n", err->message);
		g_error_free (err);
		goto out;
	}


	result = 0;

out:
	return result;
}

// Sample variable change display code. Eventually to push out changes
// on a socket for further consumption.
static void sample_variable_change_cb(void *prefix, int var_num,
				      const char *variable_name,
				      const char *old_value,
				      const char *variable_value) {
	const char *log_text = (const char *) prefix;
	const time_t now = time(NULL);
	char time_buf[128];
	ctime_r(&now, time_buf);
	time_buf[strlen(time_buf) - 1] = '\0';

	if (strcmp(log_text, "transport") == 0 &&
	    (var_num == TRANSPORT_VAR_NEXT_AV_URI_META ||
	     var_num == TRANSPORT_VAR_CUR_TRACK_META ||
	     var_num == TRANSPORT_VAR_AV_URI_META)) {
		// Noisy XML, don't print fully.
		fprintf(stderr, "[%s | %s] %03d %s: <...>\n",
			time_buf, log_text,
			var_num, variable_name);
	} else {
		fprintf(stderr, "[%s | %s] %03d %s: %s\n",
			time_buf, log_text, var_num,
			variable_name, variable_value);
	}
}

int main(int argc, char **argv)
{
	int rc;
	int result = EXIT_FAILURE;
	struct upnp_device_descriptor *upnp_renderer;

	rc = process_cmdline(argc, argv);
	if (rc != 0) {
		goto out;
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

	if (pid_file && pid_file[0] != '/') {
		// We need to canonicalize the filename because our
		// cwd will change after becoming a daemon.
		char *buf = (char*) malloc(PATH_MAX);  // will leak. Ok.
		char *result = getcwd(buf, PATH_MAX);
		result = strcat(result, "/");
		pid_file = strcat(result, pid_file);
	}
	if (daemon_mode) {
		daemon(0, 0);  // TODO: check for daemon() in configure.
	}
	if (pid_file) {
		FILE *p = fopen(pid_file, "w+");
		if (p) {
			fprintf(p, "%d\n", getpid());
			fclose(p);
		} else {
			perror("Failed to write pid file");
		}
	}

#if !GLIB_CHECK_VERSION(2, 32, 0)
	// Only older version of glib require this.
	if (!g_thread_get_initialized()) {
		g_thread_init(NULL);
	}
#endif

	upnp_renderer = upnp_renderer_descriptor(friendly_name, uuid);
	if (upnp_renderer == NULL) {
		goto out;
	}

	if (show_devicedesc) {
		char *buf;
		buf = upnp_get_device_desc(upnp_renderer);
		assert(buf != NULL);
		fputs(buf, stdout);
		exit(EXIT_SUCCESS);
	}

	rc = output_init(output);
	if (rc != 0) {
		fprintf(stderr,"ERROR: Failed to initialize Output subsystem\n");
		goto out;
	}

	struct upnp_device *device;
	device = upnp_device_init(upnp_renderer, ip_address);
	if (device == NULL) {
		fprintf(stderr,"ERROR: Failed to initialize UPnP device\n");
		goto out;
	}

	upnp_transport_init(device);
	upnp_transport_register_variable_listener(sample_variable_change_cb,
						  (void*) "transport");
	upnp_control_init(device);
	upnp_control_register_variable_listener(sample_variable_change_cb,
						(void*) "control");

	printf("Ready for rendering..\n");
	output_loop();
	result = EXIT_SUCCESS;

out:
	return result;
}
