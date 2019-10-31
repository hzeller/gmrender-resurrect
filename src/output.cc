// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
/* output.c - Output module frontend
 *
 * Copyright (C) 2007 Ivo Clarysse,  (C) 2012 Henner Zeller, (C) 2019 Tucker Kern
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

#include <assert.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

#include <glib.h>

#include "logging.h"
#include "output_module.h"
#ifdef HAVE_GST
#include "output_gstreamer.h"
#endif
#include "output.h"

#define TAG "output"

typedef struct output_entry_t
{
  std::string shortname;
  std::string description;
  OutputModule* (*create)(Output::playback_callback_t, Output::metadata_callback_t);
  OutputModule::Options& options;
} output_entry_t;

static std::vector<output_entry_t> modules = 
{
#ifdef HAVE_GST
  {"gst", "GStreamer multimedia framework", GstreamerOutput::create, GstreamerOutput::Options::get()}
#else
// this will be a runtime error, but there is not much point in waiting till then.
#error "No output configured. You need to ./configure --with-gstreamer"
#endif
};

static OutputModule* output_module = NULL;

int Output::add_options(GOptionContext* ctx)
{
  for (const auto& module : modules)
  {
    for (auto option : module.options.get_option_groups())
      g_option_context_add_group(ctx, option);
  }
  
  return 0;
}

void Output::dump_modules(void) {
  
  if (modules.size() == 0)
  {
    printf("No outputs available.\n");
    return;
  }

  printf("Available outputs:\n"); 
  for (auto& module : modules)
    printf("\t%s - %s%s\n", module.shortname.c_str(), module.description.c_str(), (&module == &modules.front()) ? " (default)" : "");
}

int Output::loop() 
{
  static GMainLoop* main_loop = NULL;

  // Define a signal handler to shutdown the loop
  auto signal_handler = [](int sig) -> void
  {
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

int Output::init(const char* shortname, Output::playback_callback_t play_callback, Output::metadata_callback_t metadata_callback)
{
  if (modules.size() == 0)
  {
    Log_error(TAG, "No outputs available.");
    return -1;
  }

  // Default to first entry if no name provided
  std::string name(shortname ? shortname : modules.front().shortname);

  // Locate module by shortname
  auto it = std::find_if(modules.begin(), modules.end(), [name](output_entry_t& entry)
  {
    return entry.shortname == name;
  });

  if (it == modules.end())
  {
    Log_error(TAG, "No such output: '%s'", name.c_str());
    return -1;
  }

  const output_entry_t& entry = *it;

  Log_info(TAG, "Using output: %s (%s)", entry.shortname.c_str(), entry.description.c_str());

  output_module = entry.create(play_callback, metadata_callback);

  assert(output_module != NULL);
  
  output_module->initalize(entry.options);

  // Free the modules list
  modules.clear();

  return 0;
}

Output::mime_type_set_t Output::get_supported_media(void)
{
  assert(output_module);

  return output_module->get_supported_media();
}

void Output::set_uri(const char *uri)
{
  assert(output_module);

  output_module->set_uri(uri);
}

void Output::set_next_uri(const char *uri) 
{
  assert(output_module);

  output_module->set_next_uri(uri);
}

int Output::play() 
{
  assert(output_module);

  return output_module->play();
}

int Output::pause(void) 
{
  assert(output_module);

  return output_module->pause();
}

int Output::stop(void) 
{
  assert(output_module);

  return output_module->stop();
}

int Output::seek(int64_t position_nanos) 
{
  assert(output_module);

  return output_module->seek(position_nanos);
}

int Output::get_position(int64_t& duration_ns, int64_t& position_ns)
{
  assert(output_module);

  OutputModule::track_state_t state;
  if (output_module->get_position(state) == OutputModule::Success)
  {
    duration_ns = state.duration_ns;
    position_ns = state.position_ns;

    return 0;
  }

  return -1;
}

int Output::get_volume(float& value) 
{
  assert(output_module);

  return output_module->get_volume(value);
}

int Output::set_volume(float value) 
{
  assert(output_module);

  return output_module->set_volume(value);
}

int Output::get_mute(bool& value) 
{
  assert(output_module);

  return output_module->get_mute(value);
}

int Output::set_mute(bool value) 
{
  assert(output_module);

  return output_module->set_mute(value);
}