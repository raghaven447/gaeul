/**
 *  Copyright 2020 SK Telecom Co., Ltd.
 *    Author: Jeongseok Kim <jeongseok.kim@sk.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

#include "config.h"

#include <glib.h>
#include <glib-unix.h>

#include "gaeul/application.h"
#include "gaeul/mjpeg/mjpeg-application.h"

static gboolean
intr_handler (gpointer user_data)
{
  GApplication *app = user_data;

  g_debug ("releasing app");
  g_application_release (app);

  return G_SOURCE_REMOVE;
}

int
main (int argc, char **argv)
{
  g_autoptr (GApplication) app = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GOptionContext) context = NULL;

  const gchar *config = NULL;
  const gchar *dbus_type = NULL;
  GOptionEntry entries[] = {
    {"config", 'c', 0, G_OPTION_ARG_FILENAME, &config, NULL, NULL},
    {"dbus-type", 0, 0, G_OPTION_ARG_STRING, &dbus_type, NULL, NULL},
    {NULL}
  };

  context = g_option_context_new (NULL);

  g_option_context_set_help_enabled (context, FALSE);
  g_option_context_add_main_entries (context, entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return -1;
  }

  g_debug ("dbus-type: %s", dbus_type);
  app = G_APPLICATION (g_object_new (GAEUL_TYPE_MJPEG_APPLICATION,
          "application-id", GAEUL_MJPEG_APPLICATION_SCHEMA_ID,
          "config-path", config, "dbus-type",
          gaeul_application_dbus_type_get_by_name (dbus_type), NULL));

  g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, app);

  g_application_hold (app);

  return gaeul_application_run (GAEUL_APPLICATION (app), argc, argv);
}
