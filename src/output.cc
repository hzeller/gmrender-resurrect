// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output.c - Output module frontend
 *
 * Copyright (C) 2007 Ivo Clarysse,  (C) 2012 Henner Zeller, (C) 2019 Tucker
 * Kern
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
#include "output.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <glib.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "logging.h"
#include "output_module.h"
#ifdef HAVE_GST
#  include "output_gstreamer.h"
#endif

#define TAG "output"

static OutputModule* output_module = NULL;

/**
  @brief  Describes an available output module by name, description. Provides an
  interface for construction of the output and a means to query the output for
  associated command line options.
*/
struct OutputEntry {
  std::string shortname;
  std::string description;
  std::function<OutputModule*(Output::PlaybackCallback,
                              Output::MetadataCallback)> create;
  OutputModule::Options& options;
};

static const std::vector<OutputEntry>& GetAvailableModules() {
  static std::vector<OutputEntry> modules = {
#ifdef HAVE_GST
      {"gst", "GStreamer multimedia framework", GstreamerOutput::Create,
       GstreamerOutput::Options::Get()}
#else
// this will be a runtime error, but there is not much point in waiting till
// then.
#error "No output configured. You need to ./configure --with-gstreamer"
#endif
  };

  return modules;
}

int Output::AddOptions(GOptionContext* ctx) {
  for (const auto& module : GetAvailableModules()) {
    for (auto option : module.options.GetOptionGroups())
      g_option_context_add_group(ctx, option);
  }

  return 0;
}

void Output::DumpModules(void) {
  const std::vector<OutputEntry>& modules = GetAvailableModules();

  if (modules.size() == 0) {
    printf("No outputs available.\n");
    return;
  }

  printf("Available outputs:\n");
  for (auto& module : modules)
    printf("\t%s - %s%s\n", module.shortname.c_str(),
           module.description.c_str(),
           (&module == &modules.front()) ? " (default)" : "");
}

int Output::Loop() {
  static GMainLoop* main_loop = NULL;

  // Define a signal handler to shutdown the loop
  static auto signal_handler = [](int sig) -> void {
    if (main_loop) {
      // TODO(hzeller): revisit - this is not safe to do.
      g_main_loop_quit(main_loop);
    }
  };

  // Create a main loop that runs the default GLib main context
  main_loop = g_main_loop_new(NULL, FALSE);

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  g_main_loop_run(main_loop);

  return 0;
}

int Output::Init(const char* shortname, Output::PlaybackCallback play_callback,
                 Output::MetadataCallback metadata_callback) {
  const std::vector<OutputEntry>& modules = GetAvailableModules();

  if (modules.size() == 0) {
    Log_error(TAG, "No outputs available.");
    return -1;
  }

  // Default to first entry if no name provided
  std::string name(shortname ? shortname : modules.front().shortname);

  // Locate module by shortname
  auto found = std::find_if(
      modules.begin(), modules.end(),
      [&name](const OutputEntry& entry) { return entry.shortname == name; });

  if (found == modules.end()) {
    Log_error(TAG, "No such output: '%s'", name.c_str());
    return -1;
  }

  const OutputEntry& entry = *found;

  Log_info(TAG, "Using output: %s (%s)", entry.shortname.c_str(),
           entry.description.c_str());

  output_module = entry.create(play_callback, metadata_callback);

  assert(output_module != NULL);

  output_module->Initalize(entry.options);

  return 0;
}

Output::MimeTypeSet Output::GetSupportedMedia(void) {
  assert(output_module);

  return output_module->GetSupportedMedia();
}

void Output::SetUri(const char* uri) {
  assert(output_module);

  output_module->SetUri(uri);
}

void Output::SetNextUri(const char* uri) {
  assert(output_module);

  output_module->SetNextUri(uri);
}

int Output::Play() {
  assert(output_module);

  return output_module->Play();
}

int Output::Pause(void) {
  assert(output_module);

  return output_module->Pause();
}

int Output::Stop(void) {
  assert(output_module);

  return output_module->Stop();
}

int Output::Seek(int64_t position_nanos) {
  assert(output_module);

  return output_module->Seek(position_nanos);
}

int Output::GetPosition(int64_t* duration_ns, int64_t* position_ns) {
  assert(output_module);
  assert(duration_ns && position_ns);

  OutputModule::TrackState state;
  if (output_module->GetPosition(&state) == OutputModule::kSuccess) {
    *duration_ns = state.duration_ns;
    *position_ns = state.position_ns;

    return 0;
  }

  return -1;
}

int Output::GetVolume(float* value) {
  assert(output_module);
  assert(value);

  return output_module->GetVolume(value);
}

int Output::SetVolume(float value) {
  assert(output_module);

  return output_module->SetVolume(value);
}

int Output::GetMute(bool* value) {
  assert(output_module);
  assert(value);

  return output_module->GetMute(value);
}

int Output::SetMute(bool value) {
  assert(output_module);

  return output_module->SetMute(value);
}
