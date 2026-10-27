/* output_bluos.c - Output module for BluOS players
 *
 * Copyright (C) 2026 Thomas Bagge
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

// Remote-controls a BluOS player (Bluesound, NAD, ...) through its local
// HTTP API (BluOS Custom Integration API v1.7). No audio goes through
// gmrender: the player fetches the URIs itself.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <glib.h>

#include "logging.h"
#include "upnp_connmgr.h"
#include "xmldoc.h"
#include "output_module.h"
#include "output_bluos.h"

#define BLUOS_CMD_TIMEOUT    5    // seconds, for commands.
#define BLUOS_POLL_TIMEOUT   100  // seconds, long-polling advised by the API.
#define BLUOS_RETRY_DELAY    5    // seconds, while the player is unreachable.
#define BLUOS_START_TIMEOUT  15   // seconds for our URI to start playing.
#define BLUOS_END_MARGIN     2    // seconds before the end that count as end.

static gchar *host_ = NULL;
static gint port_ = 11000;
static gchar *rewrite_from_ = NULL;
static gchar *rewrite_to_ = NULL;
static gboolean clear_queue_ = FALSE;

/* Options specific to output_bluos */
static GOptionEntry option_entries[] = {
	{ "bluosout-host", 0, 0, G_OPTION_ARG_STRING, &host_,
	  "Hostname or IP address of the BluOS player.", NULL },
	{ "bluosout-port", 0, 0, G_OPTION_ARG_INT, &port_,
	  "HTTP port of the BluOS player (default 11000).", NULL },
	{ "bluosout-rewrite-from", 0, 0, G_OPTION_ARG_STRING, &rewrite_from_,
	  "URI prefix to replace before sending URIs to the player.", NULL },
	{ "bluosout-rewrite-to", 0, 0, G_OPTION_ARG_STRING, &rewrite_to_,
	  "Replacement for the --bluosout-rewrite-from prefix.", NULL },
	{ "bluosout-clear-queue", 0, 0, G_OPTION_ARG_NONE, &clear_queue_,
	  "Clear the play queue of the player before playing an URI. "
	  "Otherwise, the player briefly plays from it at the end of each "
	  "track.", NULL },
	{ NULL }
};

// Player status, as reported by /Status.
struct bluos_status {
	char state[16];  // play, stream, pause, stop, connecting, ...
	double secs;
	double totlen;
	double db;
	int mute;
	char *track;     // identifies what the player plays; see track_id().
};

// What the UPnP side believes. If the player does something else, it is
// controlled by someone else and we report PLAY_STOPPED.
enum expected_state {
	STATE_STOPPED,
	STATE_PLAYING,
	STATE_PAUSED,
};

enum status_action {
	ACTION_NONE,
	ACTION_STOPPED,
	ACTION_STOP_PLAYER,  // stop the player, then report PLAY_STOPPED.
	ACTION_NEXT_STREAM,
};

// Everything below is protected by mutex_: it is accessed by the commands
// (called by the UPnP layer) and by the status polling thread.
static pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
static char *uri_ = NULL;           // locally strdup()ed
static char *next_uri_ = NULL;      // locally strdup()ed
static char *playing_uri_ = NULL;   // uri_ we last sent to the player.
static char *our_track_ = NULL;     // playing_uri_ as sent, see track_id().
static int started_ = 0;            // our_track_ showed up on the player.
static enum expected_state expected_state_ = STATE_STOPPED;
static struct bluos_status status_ = { "stop", 0, 0, 0, 0, NULL };
static struct timespec status_time_;  // when status_.secs was valid.
static struct timespec command_time_; // when we last asked to play.
static int command_gen_ = 0;          // incremented by every command.

static output_transition_cb_t play_trans_callback_ = NULL;

static size_t collect_response(char *data, size_t size, size_t nmemb,
			       void *userdata) {
	g_string_append_len((GString*) userdata, data, size * nmemb);
	return size * nmemb;
}

// Send a GET request to the player. If "arg" is not NULL, it is
// URL-encoded and appended to "path". If "response" is not NULL, it
// receives the body, to be g_free()d by the caller.
// Returns 0 on success.
static int bluos_get(const char *path, const char *arg, long timeout,
		     char **response, int quiet) {
	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		Log_error("bluos", "Can not initialize curl");
		return -1;
	}
	char *escaped = arg ? curl_easy_escape(curl, arg, 0) : NULL;
	char *url = NULL;
	if (asprintf(&url, "http://%s:%d%s%s", host_, port_, path,
		     escaped ? escaped : "") < 0) {
		curl_free(escaped);
		curl_easy_cleanup(curl);
		return -1;
	}
	GString *body = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long) BLUOS_CMD_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);

	int rc = 0;
	long http_code = 0;
	const CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if (!quiet)
			Log_error("bluos", "%s failed: %s", path,
				  curl_easy_strerror(res));
		rc = -1;
	} else {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		if (http_code != 200) {
			if (!quiet)
				Log_error("bluos", "%s failed: HTTP %ld",
					  path, http_code);
			rc = -1;
		}
	}
	if (rc == 0 && response != NULL) {
		*response = g_string_free(body, FALSE);
	} else {
		g_string_free(body, TRUE);
	}
	free(url);
	curl_free(escaped);
	curl_easy_cleanup(curl);
	return rc;
}

// Commands don't need the response body.
static int bluos_command(const char *path, const char *arg) {
	Log_info("bluos", "%s%s", path, arg ? arg : "");
	return bluos_get(path, arg, BLUOS_CMD_TIMEOUT, NULL, 0);
}

// At the end of our uri, the player goes back to its own play queue, if
// there is anything left in it. With --bluosout-clear-queue, there isn't.
static int bluos_play_url(const char *uri) {
	if (clear_queue_)
		bluos_command("/Clear", NULL);
	return bluos_command("/Play?url=", uri);
}

// Returns a newly allocated copy of "uri", with the --bluosout-rewrite-from
// prefix replaced by --bluosout-rewrite-to.
static char *rewrite_uri(const char *uri) {
	const size_t len = rewrite_from_ ? strlen(rewrite_from_) : 0;
	char *result = NULL;
	if (len > 0 && strncmp(uri, rewrite_from_, len) == 0) {
		if (asprintf(&result, "%s%s", rewrite_to_ ? rewrite_to_ : "",
			     uri + len) < 0) {
			return NULL;
		}
		return result;
	}
	return strdup(uri);
}

static char *get_child_value(struct xmlelement *parent, const char *key) {
	struct xmlelement *element = find_element_in_element(parent, key);
	return element ? get_node_value(element) : NULL;
}

static double get_child_double(struct xmlelement *parent, const char *key) {
	char *value = get_child_value(parent, key);
	const double result = value ? atof(value) : 0;
	free(value);
	return result;
}

// What identifies the track the player is playing: fn for tracks of its
// play queue, streamUrl for the URIs we send.
static char *track_id(struct xmlelement *top) {
	char *value = get_child_value(top, "fn");
	if (value == NULL)
		value = get_child_value(top, "streamUrl");
	return value ? value : strdup("");
}

// Parse a /Status response. The etag is an attribute of the top element,
// which xmldoc can't access, so it is extracted from the text.
static int parse_status(const char *xml, struct bluos_status *status,
			char *etag, size_t etag_size) {
	struct xmldoc *doc = xmldoc_parsexml(xml);
	if (doc == NULL)
		return -1;
	struct xmlelement *top = find_element_in_doc(doc, "status");
	if (top == NULL) {
		xmldoc_free(doc);
		return -1;
	}
	char *state = get_child_value(top, "state");
	snprintf(status->state, sizeof(status->state), "%s",
		 state ? state : "");
	free(state);
	status->secs = get_child_double(top, "secs");
	status->totlen = get_child_double(top, "totlen");
	status->db = get_child_double(top, "db");
	status->mute = (int) get_child_double(top, "mute");
	status->track = track_id(top);
	xmldoc_free(doc);

	etag[0] = '\0';
	const char *start = strstr(xml, "etag=\"");
	if (start != NULL) {
		start += strlen("etag=\"");
		const char *end = strchr(start, '"');
		if (end != NULL && (size_t) (end - start) < etag_size) {
			memcpy(etag, start, end - start);
			etag[end - start] = '\0';
		}
	}
	return 0;
}

static int is_playing(const char *state) {
	return strcmp(state, "play") == 0 || strcmp(state, "stream") == 0;
}

static double seconds_since(const struct timespec *t) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t->tv_sec) + (now.tv_nsec - t->tv_nsec) / 1e9;
}

// The player only reports secs on state changes; in between, we
// extrapolate. Called with mutex_ held.
static double current_secs(void) {
	double secs = status_.secs;
	if (is_playing(status_.state))
		secs += seconds_since(&status_time_);
	if (status_.totlen > 0 && secs > status_.totlen)
		secs = status_.totlen;
	return secs;
}

// Called with mutex_ held, by every command before talking to the player:
// status responses requested before that are ignored, as they might
// predate the command.
static void begin_command(enum expected_state new_state) {
	status_.secs = current_secs();
	clock_gettime(CLOCK_MONOTONIC, &status_time_);
	expected_state_ = new_state;
	command_gen_++;
}

// Called with mutex_ held. Remember "uri" as sent to the player, which
// is expected to start playing it. Returns the (rewritten) uri to send,
// to be free()d by the caller.
static char *begin_new_uri(const char *uri) {
	begin_command(STATE_PLAYING);
	free(playing_uri_);
	playing_uri_ = strdup(uri);
	free(our_track_);
	our_track_ = rewrite_uri(uri);
	started_ = 0;
	snprintf(status_.state, sizeof(status_.state), "%s", "play");
	status_.secs = 0;
	status_.totlen = 0;
	clock_gettime(CLOCK_MONOTONIC, &command_time_);
	return our_track_ ? strdup(our_track_) : NULL;
}

// Whether the player can be resumed with /Play. Called with mutex_ held.
static int is_paused_on_our_track(void) {
	// Only the uri we sent can be resumed.
	if (uri_ == NULL || playing_uri_ == NULL
	    || strcmp(uri_, playing_uri_) != 0)
		return 0;
	// Paused by us, or by someone else while on our track.
	if (expected_state_ == STATE_PAUSED)
		return 1;
	return strcmp(status_.state, "pause") == 0
		&& our_track_ != NULL && status_.track != NULL
		&& strcmp(our_track_, status_.track) == 0;
}

static int output_bluos_play(output_transition_cb_t callback) {
	play_trans_callback_ = callback;
	pthread_mutex_lock(&mutex_);
	const enum expected_state old_state = expected_state_;
	const int resume = is_paused_on_our_track();
	char *uri = NULL;
	if (resume) {
		begin_command(STATE_PLAYING);
		clock_gettime(CLOCK_MONOTONIC, &command_time_);
	} else if (uri_ != NULL) {
		uri = begin_new_uri(uri_);
	}
	pthread_mutex_unlock(&mutex_);

	if (!resume && uri == NULL) {
		Log_error("bluos", "No uri to play");
		return -1;
	}
	const int rc = resume
		? bluos_command("/Play", NULL)
		: bluos_play_url(uri);
	free(uri);
	if (rc != 0) {
		// A new uri we failed to send can't be resumed later.
		pthread_mutex_lock(&mutex_);
		expected_state_ = resume ? old_state : STATE_STOPPED;
		pthread_mutex_unlock(&mutex_);
		return -1;
	}
	return 0;
}

static int output_bluos_stop(void) {
	pthread_mutex_lock(&mutex_);
	begin_command(STATE_STOPPED);
	pthread_mutex_unlock(&mutex_);
	return bluos_command("/Stop", NULL);
}

static int output_bluos_pause(void) {
	pthread_mutex_lock(&mutex_);
	const enum expected_state old_state = expected_state_;
	begin_command(STATE_PAUSED);
	snprintf(status_.state, sizeof(status_.state), "%s", "pause");
	pthread_mutex_unlock(&mutex_);
	if (bluos_command("/Pause", NULL) != 0) {
		pthread_mutex_lock(&mutex_);
		expected_state_ = old_state;
		pthread_mutex_unlock(&mutex_);
		return -1;
	}
	return 0;
}

static int output_bluos_seek(gint64 position_nanos) {
	const int secs = position_nanos / 1000000000LL;
	char path[64];
	snprintf(path, sizeof(path), "/Play?seek=%d", secs);
	pthread_mutex_lock(&mutex_);
	// Seeking needs the track length; without it, the player starts the
	// track over (seen with .m4b files). While our uri is starting, the
	// length is not known yet: control points seek right after play to
	// start at a given position.
	if (started_ && status_.totlen <= 0
	    && strcmp(status_.state, "connecting") != 0) {
		pthread_mutex_unlock(&mutex_);
		Log_error("bluos", "Can't seek: track length unknown to the player");
		return -1;
	}
	begin_command(expected_state_);
	pthread_mutex_unlock(&mutex_);
	if (bluos_command(path, NULL) != 0)
		return -1;
	pthread_mutex_lock(&mutex_);
	status_.secs = secs;
	clock_gettime(CLOCK_MONOTONIC, &status_time_);
	pthread_mutex_unlock(&mutex_);
	return 0;
}

static void output_bluos_set_next_uri(const char *uri) {
	Log_info("bluos", "Set next uri to '%s'", uri);
	pthread_mutex_lock(&mutex_);
	free(next_uri_);
	next_uri_ = (uri && *uri) ? strdup(uri) : NULL;
	pthread_mutex_unlock(&mutex_);
}

static void output_bluos_set_uri(const char *uri,
				 output_update_meta_cb_t meta_cb) {
	(void)meta_cb;  // We don't get meta data from the player (yet).
	Log_info("bluos", "Set uri to '%s'", uri);
	pthread_mutex_lock(&mutex_);
	free(uri_);
	uri_ = (uri && *uri) ? strdup(uri) : NULL;
	const int playing = (expected_state_ == STATE_PLAYING);
	pthread_mutex_unlock(&mutex_);

	// If already playing, play the new uri.
	if (playing)
		output_bluos_play(play_trans_callback_);
}

static int output_bluos_add_options(GOptionContext *ctx)
{
	GOptionGroup *option_group;
	option_group = g_option_group_new("bluosout", "BluOS Output Options",
	                                  "Show BluOS Output Options",
	                                  NULL, NULL);
	g_option_group_add_entries(option_group, option_entries);

	g_option_context_add_group (ctx, option_group);
	return 0;
}

static int output_bluos_get_position(gint64 *track_duration,
				     gint64 *track_pos) {
	pthread_mutex_lock(&mutex_);
	if (expected_state_ == STATE_STOPPED) {
		*track_duration = 0;
		*track_pos = 0;
	} else {
		*track_duration = status_.totlen * 1e9;
		*track_pos = current_secs() * 1e9;
	}
	pthread_mutex_unlock(&mutex_);
	return 0;
}

static int output_bluos_get_volume(float *v) {
	pthread_mutex_lock(&mutex_);
	*v = exp(status_.db / 20 * log(10));
	pthread_mutex_unlock(&mutex_);
	Log_info("bluos", "Query volume fraction: %f", *v);
	return 0;
}
static int output_bluos_set_volume(float value) {
	char path[64];
	const double db = (value > 0) ? 20 * log10(value) : -INFINITY;
	Log_info("bluos", "Set volume fraction to %f", value);
	if (value > 0) {
		snprintf(path, sizeof(path), "/Volume?abs_db=%.1f", db);
	} else {
		snprintf(path, sizeof(path), "/Volume?level=0");
	}
	if (bluos_command(path, NULL) != 0)
		return -1;
	pthread_mutex_lock(&mutex_);
	status_.db = db;
	pthread_mutex_unlock(&mutex_);
	return 0;
}
static int output_bluos_get_mute(int *m) {
	pthread_mutex_lock(&mutex_);
	*m = status_.mute;
	pthread_mutex_unlock(&mutex_);
	return 0;
}
static int output_bluos_set_mute(int m) {
	char path[64];
	Log_info("bluos", "Set mute to %s", m ? "on" : "off");
	snprintf(path, sizeof(path), "/Volume?mute=%d", m ? 1 : 0);
	if (bluos_command(path, NULL) != 0)
		return -1;
	pthread_mutex_lock(&mutex_);
	status_.mute = m;
	pthread_mutex_unlock(&mutex_);
	return 0;
}

// Whether our uri played up to its end; "played" and "totlen" are from
// before the new status. Called with mutex_ held.
static int is_end_of_track(double played, double totlen) {
	return expected_state_ == STATE_PLAYING
		&& totlen > 0 && played >= totlen - BLUOS_END_MARGIN;
}

// Take a new status from the player into account and decide what to tell
// the UPnP side. Takes ownership of status->track.
// Called with mutex_ held.
static enum status_action update_status(struct bluos_status *status) {
	const double played = current_secs();
	const double totlen = status_.totlen;
	free(status_.track);
	status_ = *status;
	clock_gettime(CLOCK_MONOTONIC, &status_time_);

	if (expected_state_ == STATE_STOPPED)
		return ACTION_NONE;

	if (!started_) {
		// Until our uri shows up, the player may still report what
		// it played before (including our uri, if it is played again).
		if (our_track_ != NULL && strcmp(status_.track, our_track_) == 0
		    && strcmp(status_.state, "stop") != 0) {
			started_ = 1;
			return ACTION_NONE;
		}
		if (expected_state_ != STATE_PLAYING
		    || seconds_since(&command_time_) < BLUOS_START_TIMEOUT)
			return ACTION_NONE;
		Log_error("bluos", "Player did not start '%s' (state %s)",
			  playing_uri_, status_.state);
		expected_state_ = STATE_STOPPED;
		return ACTION_STOPPED;
	}

	const int stopped = (strcmp(status_.state, "stop") == 0);
	if (stopped || strcmp(status_.track, our_track_) != 0) {
		// At the end of our uri, the player goes back to its own play
		// queue: it plays the next track there, or stops at its end.
		// Before the end, it is someone else taking over.
		const int track_ended = is_end_of_track(played, totlen);
		if (track_ended && next_uri_ != NULL) {
			free(uri_);
			uri_ = next_uri_;
			next_uri_ = NULL;
			return ACTION_NEXT_STREAM;
		}
		expected_state_ = STATE_STOPPED;
		if (track_ended) {
			Log_info("bluos", "End of track");
			return stopped ? ACTION_STOPPED : ACTION_STOP_PLAYER;
		}
		if (stopped) {
			Log_info("bluos", "Player stopped");
		} else {
			Log_info("bluos", "Player switched to '%s': stopping",
				 status_.track);
		}
		return ACTION_STOPPED;
	}

	if ((expected_state_ == STATE_PLAYING
	     && strcmp(status_.state, "pause") == 0)
	    || (expected_state_ == STATE_PAUSED && is_playing(status_.state))) {
		// Paused or resumed by someone else. The UPnP side can't
		// follow that; best we can do is to say we stopped.
		Log_info("bluos", "Player state changed to %s: stopping",
			 status_.state);
		expected_state_ = STATE_STOPPED;
		return ACTION_STOPPED;
	}
	return ACTION_NONE;
}

static void report_transition(enum PlayFeedback fb) {
	if (play_trans_callback_)
		play_trans_callback_(fb);
}

// Follow the player state with long-polling on /Status. The transition
// callback takes the UPnP lock, which is held while our commands are
// called: it must only be called from here, without mutex_ held.
static void *thread_poll_status(void *userdata) {
	(void)userdata;
	char etag[64] = "";
	int unreachable = 0;
	for (;;) {
		struct timespec start;
		clock_gettime(CLOCK_MONOTONIC, &start);
		char path[128];
		if (etag[0]) {
			snprintf(path, sizeof(path),
				 "/Status?timeout=%d&etag=%s",
				 BLUOS_POLL_TIMEOUT, etag);
		} else {
			snprintf(path, sizeof(path), "/Status");
		}
		pthread_mutex_lock(&mutex_);
		const int gen = command_gen_;
		pthread_mutex_unlock(&mutex_);

		char *xml = NULL;
		char new_etag[64];
		struct bluos_status status;
		if (bluos_get(path, NULL, BLUOS_POLL_TIMEOUT + 10, &xml,
			      unreachable) != 0
		    || parse_status(xml, &status, new_etag,
				    sizeof(new_etag)) != 0) {
			g_free(xml);
			if (!unreachable)
				Log_error("bluos", "Player %s:%d unreachable",
					  host_, port_);
			unreachable = 1;
			etag[0] = '\0';
			pthread_mutex_lock(&mutex_);
			const int was_playing =
				(expected_state_ != STATE_STOPPED);
			expected_state_ = STATE_STOPPED;
			pthread_mutex_unlock(&mutex_);
			if (was_playing)
				report_transition(PLAY_STOPPED);
			sleep(BLUOS_RETRY_DELAY);
			continue;
		}
		g_free(xml);
		if (unreachable)
			Log_info("bluos", "Player %s:%d reachable again",
				 host_, port_);
		unreachable = 0;

		enum status_action action = ACTION_NONE;
		char *next_uri = NULL;
		pthread_mutex_lock(&mutex_);
		if (gen == command_gen_) {
			snprintf(etag, sizeof(etag), "%s", new_etag);
			action = update_status(&status);
			if (action == ACTION_NEXT_STREAM)
				next_uri = begin_new_uri(uri_);
		} else {
			// Might predate a command: ask again.
			free(status.track);
		}
		pthread_mutex_unlock(&mutex_);

		if (action == ACTION_NEXT_STREAM) {
			if (next_uri != NULL
			    && bluos_play_url(next_uri) == 0) {
				report_transition(PLAY_STARTED_NEXT_STREAM);
			} else {
				pthread_mutex_lock(&mutex_);
				expected_state_ = STATE_STOPPED;
				pthread_mutex_unlock(&mutex_);
				report_transition(PLAY_STOPPED);
			}
			free(next_uri);
		} else if (action == ACTION_STOP_PLAYER) {
			// Don't let the player go on with its own play queue.
			bluos_command("/Stop", NULL);
			report_transition(PLAY_STOPPED);
		} else if (action == ACTION_STOPPED) {
			report_transition(PLAY_STOPPED);
		}

		// The API asks for at least one second between requests.
		const double elapsed = seconds_since(&start);
		if (elapsed < 1)
			usleep((1 - elapsed) * 1000000);
	}
	return NULL;
}

static void register_mime_types(void) {
	static const char *const mime_types[] = {
		"audio/flac", "audio/x-flac",
		"audio/mpeg",
		"audio/aac", "audio/x-aac", "audio/mp4", "audio/x-m4a",
		"audio/ogg", "audio/x-ogg", "application/ogg", "audio/opus",
		"audio/wav", "audio/x-wav",
		"audio/x-alac",
		NULL
	};
	for (int i = 0; mime_types[i] != NULL; i++)
		register_mime_type(mime_types[i]);
}

static int output_bluos_init(void)
{
	if (host_ == NULL) {
		Log_error("bluos", "Missing --bluosout-host");
		return 1;
	}
	register_mime_types();
	curl_global_init(CURL_GLOBAL_DEFAULT);
	clock_gettime(CLOCK_MONOTONIC, &status_time_);

	// Get the volume now, the UPnP side asks for it at start.
	char *xml = NULL;
	char etag[64];
	struct bluos_status status;
	if (bluos_get("/Status", NULL, BLUOS_CMD_TIMEOUT, &xml, 0) == 0
	    && parse_status(xml, &status, etag, sizeof(etag)) == 0) {
		Log_info("bluos", "Player %s:%d: state %s, %.1f dB",
			 host_, port_, status.state, status.db);
		status_ = status;
	} else {
		Log_error("bluos", "Player %s:%d not reachable (yet)",
			  host_, port_);
	}
	g_free(xml);

	pthread_t thread;
	pthread_create(&thread, NULL, thread_poll_status, NULL);
	return 0;
}

struct output_module bluos_output = {
	.shortname = "bluos",
	.description = "BluOS player (remote-controlled over HTTP)",
	.add_options = output_bluos_add_options,

	.init        = output_bluos_init,
	.set_uri     = output_bluos_set_uri,
	.set_next_uri= output_bluos_set_next_uri,
	.play        = output_bluos_play,
	.stop        = output_bluos_stop,
	.pause       = output_bluos_pause,
	.seek        = output_bluos_seek,

	.get_position = output_bluos_get_position,
	.get_volume  = output_bluos_get_volume,
	.set_volume  = output_bluos_set_volume,
	.get_mute  = output_bluos_get_mute,
	.set_mute  = output_bluos_set_mute,
};
