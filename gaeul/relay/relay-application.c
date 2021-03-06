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

#include "gaeul.h"
#include "stream-authenticator.h"
#include "gaeul/relay/relay-application.h"
#include "gaeul/relay/relay-generated.h"

#include <hwangsae/hwangsae.h>

typedef enum
{
  PROP_SINK_PORT = 1,
  PROP_SOURCE_PORT,
  PROP_EXTERNAL_IP,

  /*< private > */
  PROP_LAST
} GaeulRelayApplicationProperty;

static GParamSpec *properties[PROP_LAST] = { NULL };

struct _GaeulRelayApplication
{
  GaeulApplication parent;

  HwangsaeRelay *relay;
  GaeulStreamAuthenticator *auth;

  guint sink_port;
  guint source_port;
  gchar *external_ip;

  GSettings *settings;
  Gaeul2DBusRelay *dbus_service;
};

/* *INDENT-OFF* */
G_DEFINE_TYPE (GaeulRelayApplication, gaeul_relay_application, GAEUL_TYPE_APPLICATION)
/* *INDENT-ON* */

static void
gaeul_relay_application_on_io_error (GaeulRelayApplication * app,
    GInetSocketAddress * address, GError * error)
{
  g_autofree gchar *ip = NULL;

  ip = g_inet_address_to_string (g_inet_socket_address_get_address (address));

  g_debug ("(%s:%d) %s", ip, g_inet_socket_address_get_port (address),
      error->message);
}

static void
gaeul_relay_application_activate (GApplication * app)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (app);
  g_autofree gchar *master_uri = NULL;
  g_autofree gchar *master_username = NULL;

  self->settings = gaeul_gsettings_new (GAEUL_RELAY_APPLICATION_SCHEMA_ID,
      gaeul_application_get_config_path (GAEUL_APPLICATION (self)));

  g_settings_bind (self->settings, "uid", self, "uid", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "sink-port", self, "sink-port",
      G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "source-port", self, "source-port",
      G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "external-ip", self, "external-ip",
      G_SETTINGS_BIND_DEFAULT);

  self->relay =
      hwangsae_relay_new (self->external_ip, self->sink_port,
      self->source_port);
  self->auth = gaeul_stream_authenticator_new (self->relay);

  g_object_set (self->relay, "authentication",
      g_settings_get_boolean (self->settings, "authentication"), NULL);

  master_uri = g_settings_get_string (self->settings, "master-uri");
  master_username = g_settings_get_string (self->settings, "master-username");
  if (master_uri && strlen (master_uri) > 0) {
    g_object_set (self->relay, "master-uri", master_uri,
        "master-username", master_username, NULL);
  }

  g_signal_connect_swapped (self->relay, "io-error",
      G_CALLBACK (gaeul_relay_application_on_io_error), self);

  g_debug ("Starting hwangsae (sink-uri: %s, source-uri: %s)",
      hwangsae_relay_get_sink_uri (self->relay),
      hwangsae_relay_get_source_uri (self->relay));

  hwangsae_relay_set_latency (self->relay, HWANGSAE_CALLER_DIRECTION_SINK,
      g_settings_get_uint (self->settings, "sink-latency"));
  hwangsae_relay_set_latency (self->relay, HWANGSAE_CALLER_DIRECTION_SRC,
      g_settings_get_uint (self->settings, "source-latency"));

  hwangsae_relay_start (self->relay);

  G_APPLICATION_CLASS (gaeul_relay_application_parent_class)->activate (app);
}

static void
gaeul_relay_application_shutdown (GApplication * app)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (app);

  g_clear_object (&self->auth);
  g_clear_object (&self->relay);
  g_clear_object (&self->settings);

  G_APPLICATION_CLASS (gaeul_relay_application_parent_class)->shutdown (app);
}

static void
gaeul_relay_application_dispose (GObject * object)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (object);

  g_clear_object (&self->settings);
  g_clear_object (&self->auth);
  g_clear_object (&self->relay);

  G_OBJECT_CLASS (gaeul_relay_application_parent_class)->dispose (object);
}

static void
gaeul_relay_application_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (object);

  switch (prop_id) {
    case PROP_SINK_PORT:
      g_value_set_uint (value, self->sink_port);
      break;
    case PROP_SOURCE_PORT:
      g_value_set_uint (value, self->source_port);
      break;
    case PROP_EXTERNAL_IP:
      g_value_set_string (value, self->external_ip);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gaeul_relay_application_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (object);

  switch (prop_id) {
    case PROP_SINK_PORT:
      self->sink_port = g_value_get_uint (value);
      break;
    case PROP_SOURCE_PORT:
      self->source_port = g_value_get_uint (value);
      break;
    case PROP_EXTERNAL_IP:
      g_free (self->external_ip);
      self->external_ip = g_value_dup_string (value);
      g_debug ("external-ip: %s", self->external_ip);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gaeul_relay_application_handle_add_sink_token (GaeulRelayApplication * self,
    GDBusMethodInvocation * invocation, const gchar * username)
{
  gaeul_stream_authenticator_add_sink_token (self->auth, username);
  gaeul2_dbus_relay_complete_add_sink_token (self->dbus_service, invocation);

  g_info ("a sink token (%s) is added", username);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_add_source_token (GaeulRelayApplication * self,
    GDBusMethodInvocation * invocation, const gchar * username,
    const gchar * resource)
{
  gaeul_stream_authenticator_add_source_token (self->auth, username, resource);
  gaeul2_dbus_relay_complete_add_source_token (self->dbus_service, invocation);

  g_info ("a pair of tokens (%s,%s) is added", username, resource);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_set_sink_token_credentials (
    GaeulRelayApplication * self, GDBusMethodInvocation * invocation,
    const gchar * username, const gchar * passphrase, guint pbkeylen)
{
  gaeul_stream_authenticator_set_sink_credentials (self->auth, username,
      passphrase, pbkeylen);
  gaeul2_dbus_relay_complete_set_sink_token_credentials (self->dbus_service,
      invocation);

  g_info ("credentials for token (%s) set", username);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_set_source_token_credentials (
    GaeulRelayApplication * self, GDBusMethodInvocation * invocation,
    const gchar * username, const gchar * resource, const gchar * passphrase,
    guint pbkeylen)
{
  gaeul_stream_authenticator_set_source_credentials (self->auth, username,
      resource, passphrase, pbkeylen);
  gaeul2_dbus_relay_complete_set_source_token_credentials (self->dbus_service,
      invocation);

  g_info ("credentials for token (%s,%s) set", username, resource);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_remove_sink_token (GaeulRelayApplication * self,
    GDBusMethodInvocation * invocation, const gchar * username)
{
  gaeul_stream_authenticator_remove_sink_token (self->auth, username);
  gaeul2_dbus_relay_complete_remove_sink_token (self->dbus_service, invocation);

  g_info ("a sink token (%s) is added", username);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_remove_source_token (GaeulRelayApplication *
    self, GDBusMethodInvocation * invocation, const gchar * username,
    const gchar * resource)
{
  gaeul_stream_authenticator_remove_source_token (self->auth, username,
      resource);
  gaeul2_dbus_relay_complete_remove_source_token (self->dbus_service,
      invocation);

  g_info ("a pair of tokens (%s,%s) is removed", username, resource);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_list_sink_tokens (GaeulRelayApplication *
    self, GDBusMethodInvocation * invocation)
{
  GVariant *tokens = NULL;

  tokens = gaeul_stream_authenticator_list_sink_tokens (self->auth);

  gaeul2_dbus_relay_complete_list_sink_tokens (self->dbus_service,
      invocation, tokens);

  return TRUE;
}

static gboolean
gaeul_relay_application_handle_list_source_tokens (GaeulRelayApplication *
    self, GDBusMethodInvocation * invocation)
{
  GVariant *tokens = NULL;

  tokens = gaeul_stream_authenticator_list_source_tokens (self->auth);

  gaeul2_dbus_relay_complete_list_source_tokens (self->dbus_service,
      invocation, tokens);

  return TRUE;
}



static gboolean
gaeul_relay_application_dbus_register (GApplication * app,
    GDBusConnection * connection, const gchar * object_path, GError ** error)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (app);

  g_debug ("registering D-Bus (%s)", object_path);

  if (!self->dbus_service) {
    self->dbus_service = gaeul2_dbus_relay_skeleton_new ();

    g_signal_connect_swapped (self->dbus_service, "handle-add-sink-token",
        (GCallback) gaeul_relay_application_handle_add_sink_token, self);
    g_signal_connect_swapped (self->dbus_service, "handle-add-source-token",
        (GCallback) gaeul_relay_application_handle_add_source_token, self);
    g_signal_connect_swapped (self->dbus_service,
        "handle-set-sink-token-credentials",
        (GCallback) gaeul_relay_application_handle_set_sink_token_credentials,
        self);
    g_signal_connect_swapped (self->dbus_service,
        "handle-set-source-token-credentials",
        (GCallback) gaeul_relay_application_handle_set_source_token_credentials,
        self);
    g_signal_connect_swapped (self->dbus_service, "handle-remove-sink-token",
        (GCallback) gaeul_relay_application_handle_remove_sink_token, self);
    g_signal_connect_swapped (self->dbus_service, "handle-remove-source-token",
        (GCallback) gaeul_relay_application_handle_remove_source_token, self);
    g_signal_connect_swapped (self->dbus_service, "handle-list-sink-tokens",
        (GCallback) gaeul_relay_application_handle_list_sink_tokens, self);
    g_signal_connect_swapped (self->dbus_service, "handle-list-source-tokens",
        (GCallback) gaeul_relay_application_handle_list_source_tokens, self);
  }

  if (!G_APPLICATION_CLASS (gaeul_relay_application_parent_class)->dbus_register
      (app, connection, object_path, error)) {
    return FALSE;
  }

  return
      g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON
      (self->dbus_service), connection, "/org/hwangsaeul/Gaeul2/Relay", error);
}

static void
gaeul_relay_application_dbus_unregister (GApplication * app,
    GDBusConnection * connection, const gchar * object_path)
{
  GaeulRelayApplication *self = GAEUL_RELAY_APPLICATION (app);

  if (self->dbus_service) {
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON
        (self->dbus_service));
    g_clear_object (&self->dbus_service);
  }

  G_APPLICATION_CLASS (gaeul_relay_application_parent_class)->dbus_unregister
      (app, connection, object_path);
}

static void
gaeul_relay_application_class_init (GaeulRelayApplicationClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  object_class->get_property = gaeul_relay_application_get_property;
  object_class->set_property = gaeul_relay_application_set_property;
  object_class->dispose = gaeul_relay_application_dispose;

  properties[PROP_SINK_PORT] =
      g_param_spec_uint ("sink-port", "sink-port",
      "sink-port", 1, 65535, 7777, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_SOURCE_PORT] =
      g_param_spec_uint ("source-port", "source-port",
      "source-port", 1, 65535, 8888,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_EXTERNAL_IP] =
      g_param_spec_string ("external-ip", "external-ip", "external-ip",
      NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties),
      properties);

  app_class->activate = gaeul_relay_application_activate;
  app_class->shutdown = gaeul_relay_application_shutdown;
  app_class->dbus_register = gaeul_relay_application_dbus_register;
  app_class->dbus_unregister = gaeul_relay_application_dbus_unregister;
}

static void
gaeul_relay_application_init (GaeulRelayApplication * self)
{
}
