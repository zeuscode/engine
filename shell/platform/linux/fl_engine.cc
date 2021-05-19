// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux/public/flutter_linux/fl_engine.h"

#include <gmodule.h>

#include <cstring>

#include "flutter/shell/platform/linux/fl_binary_messenger_private.h"
#include "flutter/shell/platform/linux/fl_dart_project_private.h"
#include "flutter/shell/platform/linux/fl_engine_private.h"
#include "flutter/shell/platform/linux/fl_plugin_registrar_private.h"
#include "flutter/shell/platform/linux/fl_renderer.h"
#include "flutter/shell/platform/linux/fl_renderer_headless.h"
#include "flutter/shell/platform/linux/fl_settings_plugin.h"
#include "flutter/shell/platform/linux/public/flutter_linux/fl_plugin_registry.h"

// Unique number associated with platform tasks.
static constexpr size_t kPlatformTaskRunnerIdentifier = 1;

struct _FlEngine {
  GObject parent_instance;

  // Thread the GLib main loop is running on.
  GThread* thread;

  FlDartProject* project;
  FlRenderer* renderer;
  FlBinaryMessenger* binary_messenger;
  FlSettingsPlugin* settings_plugin;
  FlTaskRunner* task_runner;
  FlutterEngineAOTData aot_data;
  FLUTTER_API_SYMBOL(FlutterEngine) engine;
  FlutterEngineProcTable embedder_api;

  // Function to call when a platform message is received.
  FlEnginePlatformMessageHandler platform_message_handler;
  gpointer platform_message_handler_data;
  GDestroyNotify platform_message_handler_destroy_notify;

  // Function to call when a semantic node is received.
  FlEngineUpdateSemanticsNodeHandler update_semantics_node_handler;
  gpointer update_semantics_node_handler_data;
  GDestroyNotify update_semantics_node_handler_destroy_notify;
};

G_DEFINE_QUARK(fl_engine_error_quark, fl_engine_error)

static void fl_engine_plugin_registry_iface_init(
    FlPluginRegistryInterface* iface);

G_DEFINE_TYPE_WITH_CODE(
    FlEngine,
    fl_engine,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(fl_plugin_registry_get_type(),
                          fl_engine_plugin_registry_iface_init))

// Parse a locale into its components.
static void parse_locale(const gchar* locale,
                         gchar** language,
                         gchar** territory,
                         gchar** codeset,
                         gchar** modifier) {
  gchar* l = g_strdup(locale);

  // Locales are in the form "language[_territory][.codeset][@modifier]"
  gchar* match = strrchr(l, '@');
  if (match != nullptr) {
    *modifier = g_strdup(match + 1);
    *match = '\0';
  } else {
    *modifier = nullptr;
  }

  match = strrchr(l, '.');
  if (match != nullptr) {
    *codeset = g_strdup(match + 1);
    *match = '\0';
  } else {
    *codeset = nullptr;
  }

  match = strrchr(l, '_');
  if (match != nullptr) {
    *territory = g_strdup(match + 1);
    *match = '\0';
  } else {
    *territory = nullptr;
  }

  *language = l;
}

// Passes locale information to the Flutter engine.
static void setup_locales(FlEngine* self) {
  const gchar* const* languages = g_get_language_names();
  g_autoptr(GPtrArray) locales_array = g_ptr_array_new_with_free_func(g_free);
  // Helper array to take ownership of the strings passed to Flutter.
  g_autoptr(GPtrArray) locale_strings = g_ptr_array_new_with_free_func(g_free);
  for (int i = 0; languages[i] != nullptr; i++) {
    gchar *language, *territory, *codeset, *modifier;
    parse_locale(languages[i], &language, &territory, &codeset, &modifier);
    if (language != nullptr) {
      g_ptr_array_add(locale_strings, language);
    }
    if (territory != nullptr) {
      g_ptr_array_add(locale_strings, territory);
    }
    if (codeset != nullptr) {
      g_ptr_array_add(locale_strings, codeset);
    }
    if (modifier != nullptr) {
      g_ptr_array_add(locale_strings, modifier);
    }

    FlutterLocale* locale =
        static_cast<FlutterLocale*>(g_malloc0(sizeof(FlutterLocale)));
    g_ptr_array_add(locales_array, locale);
    locale->struct_size = sizeof(FlutterLocale);
    locale->language_code = language;
    locale->country_code = territory;
    locale->script_code = codeset;
    locale->variant_code = modifier;
  }
  FlutterLocale** locales =
      reinterpret_cast<FlutterLocale**>(locales_array->pdata);
  FlutterEngineResult result = self->embedder_api.UpdateLocales(
      self->engine, const_cast<const FlutterLocale**>(locales),
      locales_array->len);
  if (result != kSuccess) {
    g_warning("Failed to set up Flutter locales");
  }
}

// Called when engine needs a backing store for a specific #FlutterLayer.
static bool compositor_create_backing_store_callback(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out,
    void* user_data) {
  g_return_val_if_fail(FL_IS_RENDERER(user_data), false);
  return fl_renderer_create_backing_store(FL_RENDERER(user_data), config,
                                          backing_store_out);
}

// Called when the backing store is to be released.
static bool compositor_collect_backing_store_callback(
    const FlutterBackingStore* renderer,
    void* user_data) {
  g_return_val_if_fail(FL_IS_RENDERER(user_data), false);
  return fl_renderer_collect_backing_store(FL_RENDERER(user_data), renderer);
}

// Called when embedder should composite contents of each layer onto the screen.
static bool compositor_present_layers_callback(const FlutterLayer** layers,
                                               size_t layers_count,
                                               void* user_data) {
  g_return_val_if_fail(FL_IS_RENDERER(user_data), false);
  return fl_renderer_present_layers(FL_RENDERER(user_data), layers,
                                    layers_count);
}

// Flutter engine rendering callbacks.

static void* fl_engine_gl_proc_resolver(void* user_data, const char* name) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  return fl_renderer_get_proc_address(self->renderer, name);
}

static bool fl_engine_gl_make_current(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  g_autoptr(GError) error = nullptr;
  gboolean result = fl_renderer_make_current(self->renderer, &error);
  if (!result) {
    g_warning("%s", error->message);
  }
  return result;
}

static bool fl_engine_gl_clear_current(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  g_autoptr(GError) error = nullptr;
  gboolean result = fl_renderer_clear_current(self->renderer, &error);
  if (!result) {
    g_warning("%s", error->message);
  }
  return result;
}

static uint32_t fl_engine_gl_get_fbo(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  return fl_renderer_get_fbo(self->renderer);
}

static bool fl_engine_gl_present(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  g_autoptr(GError) error = nullptr;
  gboolean result = fl_renderer_present(self->renderer, &error);
  if (!result) {
    g_warning("%s", error->message);
  }
  return result;
}

static bool fl_engine_gl_make_resource_current(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  g_autoptr(GError) error = nullptr;
  gboolean result = fl_renderer_make_resource_current(self->renderer, &error);
  if (!result) {
    g_warning("%s", error->message);
  }
  return result;
}

// Called by the engine to determine if it is on the GTK thread.
static bool fl_engine_runs_task_on_current_thread(void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);
  return self->thread == g_thread_self();
}

// Called when the engine has a task to perform in the GTK thread.
static void fl_engine_post_task(FlutterTask task,
                                uint64_t target_time_nanos,
                                void* user_data) {
  FlEngine* self = static_cast<FlEngine*>(user_data);

  fl_task_runner_post_task(self->task_runner, task, target_time_nanos);
}

// Called when a platform message is received from the engine.
static void fl_engine_platform_message_cb(const FlutterPlatformMessage* message,
                                          void* user_data) {
  FlEngine* self = FL_ENGINE(user_data);

  gboolean handled = FALSE;
  if (self->platform_message_handler != nullptr) {
    g_autoptr(GBytes) data =
        g_bytes_new(message->message, message->message_size);
    handled = self->platform_message_handler(
        self, message->channel, data, message->response_handle,
        self->platform_message_handler_data);
  }

  if (!handled) {
    fl_engine_send_platform_message_response(self, message->response_handle,
                                             nullptr, nullptr);
  }
}

// Called when a semantic node update is received from the engine.
static void fl_engine_update_semantics_node_cb(const FlutterSemanticsNode* node,
                                               void* user_data) {
  FlEngine* self = FL_ENGINE(user_data);

  if (self->update_semantics_node_handler != nullptr) {
    self->update_semantics_node_handler(
        self, node, self->update_semantics_node_handler_data);
  }
}

// Called when a response to a sent platform message is received from the
// engine.
static void fl_engine_platform_message_response_cb(const uint8_t* data,
                                                   size_t data_length,
                                                   void* user_data) {
  g_autoptr(GTask) task = G_TASK(user_data);
  g_task_return_pointer(task, g_bytes_new(data, data_length),
                        reinterpret_cast<GDestroyNotify>(g_bytes_unref));
}

// Implements FlPluginRegistry::get_registrar_for_plugin.
static FlPluginRegistrar* fl_engine_get_registrar_for_plugin(
    FlPluginRegistry* registry,
    const gchar* name) {
  FlEngine* self = FL_ENGINE(registry);

  return fl_plugin_registrar_new(nullptr, self->binary_messenger);
}

static void fl_engine_plugin_registry_iface_init(
    FlPluginRegistryInterface* iface) {
  iface->get_registrar_for_plugin = fl_engine_get_registrar_for_plugin;
}

static void fl_engine_dispose(GObject* object) {
  FlEngine* self = FL_ENGINE(object);

  if (self->engine != nullptr) {
    self->embedder_api.Shutdown(self->engine);
    self->engine = nullptr;
  }

  if (self->aot_data != nullptr) {
    self->embedder_api.CollectAOTData(self->aot_data);
    self->aot_data = nullptr;
  }

  g_clear_object(&self->project);
  g_clear_object(&self->renderer);
  g_clear_object(&self->binary_messenger);
  g_clear_object(&self->settings_plugin);
  g_clear_object(&self->task_runner);

  if (self->platform_message_handler_destroy_notify) {
    self->platform_message_handler_destroy_notify(
        self->platform_message_handler_data);
  }
  self->platform_message_handler_data = nullptr;
  self->platform_message_handler_destroy_notify = nullptr;

  if (self->update_semantics_node_handler_destroy_notify) {
    self->update_semantics_node_handler_destroy_notify(
        self->update_semantics_node_handler_data);
  }
  self->update_semantics_node_handler_data = nullptr;
  self->update_semantics_node_handler_destroy_notify = nullptr;

  G_OBJECT_CLASS(fl_engine_parent_class)->dispose(object);
}

static void fl_engine_class_init(FlEngineClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_engine_dispose;
}

static void fl_engine_init(FlEngine* self) {
  self->thread = g_thread_self();

  self->embedder_api.struct_size = sizeof(FlutterEngineProcTable);
  FlutterEngineGetProcAddresses(&self->embedder_api);

  self->binary_messenger = fl_binary_messenger_new(self);
}

FlEngine* fl_engine_new(FlDartProject* project, FlRenderer* renderer) {
  g_return_val_if_fail(FL_IS_DART_PROJECT(project), nullptr);
  g_return_val_if_fail(FL_IS_RENDERER(renderer), nullptr);

  FlEngine* self = FL_ENGINE(g_object_new(fl_engine_get_type(), nullptr));
  self->project = FL_DART_PROJECT(g_object_ref(project));
  self->renderer = FL_RENDERER(g_object_ref(renderer));
  return self;
}

G_MODULE_EXPORT FlEngine* fl_engine_new_headless(FlDartProject* project) {
  g_autoptr(FlRendererHeadless) renderer = fl_renderer_headless_new();
  return fl_engine_new(project, FL_RENDERER(renderer));
}

gboolean fl_engine_start(FlEngine* self, GError** error) {
  g_return_val_if_fail(FL_IS_ENGINE(self), FALSE);

  self->task_runner = fl_task_runner_new(self);

  FlutterRendererConfig config = {};
  config.type = kOpenGL;
  config.open_gl.struct_size = sizeof(FlutterOpenGLRendererConfig);
  config.open_gl.gl_proc_resolver = fl_engine_gl_proc_resolver;
  config.open_gl.make_current = fl_engine_gl_make_current;
  config.open_gl.clear_current = fl_engine_gl_clear_current;
  config.open_gl.fbo_callback = fl_engine_gl_get_fbo;
  config.open_gl.present = fl_engine_gl_present;
  config.open_gl.make_resource_current = fl_engine_gl_make_resource_current;

  FlutterTaskRunnerDescription platform_task_runner = {};
  platform_task_runner.struct_size = sizeof(FlutterTaskRunnerDescription);
  platform_task_runner.user_data = self;
  platform_task_runner.runs_task_on_current_thread_callback =
      fl_engine_runs_task_on_current_thread;
  platform_task_runner.post_task_callback = fl_engine_post_task;
  platform_task_runner.identifier = kPlatformTaskRunnerIdentifier;

  FlutterCustomTaskRunners custom_task_runners = {};
  custom_task_runners.struct_size = sizeof(FlutterCustomTaskRunners);
  custom_task_runners.platform_task_runner = &platform_task_runner;
  custom_task_runners.render_task_runner = &platform_task_runner;

  g_autoptr(GPtrArray) command_line_args =
      fl_dart_project_get_switches(self->project);
  // FlutterProjectArgs expects a full argv, so when processing it for flags
  // the first item is treated as the executable and ignored. Add a dummy value
  // so that all switches are used.
  g_ptr_array_insert(command_line_args, 0, g_strdup("flutter"));

  gchar** dart_entrypoint_args =
      fl_dart_project_get_dart_entrypoint_arguments(self->project);

  FlutterProjectArgs args = {};
  args.struct_size = sizeof(FlutterProjectArgs);
  args.assets_path = fl_dart_project_get_assets_path(self->project);
  args.icu_data_path = fl_dart_project_get_icu_data_path(self->project);
  args.command_line_argc = command_line_args->len;
  args.command_line_argv =
      reinterpret_cast<const char* const*>(command_line_args->pdata);
  args.platform_message_callback = fl_engine_platform_message_cb;
  args.update_semantics_node_callback = fl_engine_update_semantics_node_cb;
  args.custom_task_runners = &custom_task_runners;
  args.shutdown_dart_vm_when_done = true;
  args.dart_entrypoint_argc =
      dart_entrypoint_args != nullptr ? g_strv_length(dart_entrypoint_args) : 0;
  args.dart_entrypoint_argv =
      reinterpret_cast<const char* const*>(dart_entrypoint_args);

  FlutterCompositor compositor = {};
  compositor.struct_size = sizeof(FlutterCompositor);
  compositor.user_data = self->renderer;
  compositor.create_backing_store_callback =
      compositor_create_backing_store_callback;
  compositor.collect_backing_store_callback =
      compositor_collect_backing_store_callback;
  compositor.present_layers_callback = compositor_present_layers_callback;
  args.compositor = &compositor;

  if (self->embedder_api.RunsAOTCompiledDartCode()) {
    FlutterEngineAOTDataSource source = {};
    source.type = kFlutterEngineAOTDataSourceTypeElfPath;
    source.elf_path = fl_dart_project_get_aot_library_path(self->project);
    if (self->embedder_api.CreateAOTData(&source, &self->aot_data) !=
        kSuccess) {
      g_set_error(error, fl_engine_error_quark(), FL_ENGINE_ERROR_FAILED,
                  "Failed to create AOT data");
      return FALSE;
    }
    args.aot_data = self->aot_data;
  }

  FlutterEngineResult result = self->embedder_api.Initialize(
      FLUTTER_ENGINE_VERSION, &config, &args, self, &self->engine);
  if (result != kSuccess) {
    g_set_error(error, fl_engine_error_quark(), FL_ENGINE_ERROR_FAILED,
                "Failed to initialize Flutter engine");
    return FALSE;
  }

  result = self->embedder_api.RunInitialized(self->engine);
  if (result != kSuccess) {
    g_set_error(error, fl_engine_error_quark(), FL_ENGINE_ERROR_FAILED,
                "Failed to run Flutter engine");
    return FALSE;
  }

  setup_locales(self);

  self->settings_plugin = fl_settings_plugin_new(self->binary_messenger);
  fl_settings_plugin_start(self->settings_plugin);

  result = self->embedder_api.UpdateSemanticsEnabled(self->engine, TRUE);
  if (result != kSuccess)
    g_warning("Failed to enable accessibility features on Flutter engine");

  return TRUE;
}

FlutterEngineProcTable* fl_engine_get_embedder_api(FlEngine* self) {
  return &(self->embedder_api);
}

void fl_engine_set_platform_message_handler(
    FlEngine* self,
    FlEnginePlatformMessageHandler handler,
    gpointer user_data,
    GDestroyNotify destroy_notify) {
  g_return_if_fail(FL_IS_ENGINE(self));
  g_return_if_fail(handler != nullptr);

  if (self->platform_message_handler_destroy_notify) {
    self->platform_message_handler_destroy_notify(
        self->platform_message_handler_data);
  }

  self->platform_message_handler = handler;
  self->platform_message_handler_data = user_data;
  self->platform_message_handler_destroy_notify = destroy_notify;
}

void fl_engine_set_update_semantics_node_handler(
    FlEngine* self,
    FlEngineUpdateSemanticsNodeHandler handler,
    gpointer user_data,
    GDestroyNotify destroy_notify) {
  g_return_if_fail(FL_IS_ENGINE(self));

  if (self->update_semantics_node_handler_destroy_notify) {
    self->update_semantics_node_handler_destroy_notify(
        self->update_semantics_node_handler_data);
  }

  self->update_semantics_node_handler = handler;
  self->update_semantics_node_handler_data = user_data;
  self->update_semantics_node_handler_destroy_notify = destroy_notify;
}

gboolean fl_engine_send_platform_message_response(
    FlEngine* self,
    const FlutterPlatformMessageResponseHandle* handle,
    GBytes* response,
    GError** error) {
  g_return_val_if_fail(FL_IS_ENGINE(self), FALSE);
  g_return_val_if_fail(handle != nullptr, FALSE);

  if (self->engine == nullptr) {
    g_set_error(error, fl_engine_error_quark(), FL_ENGINE_ERROR_FAILED,
                "No engine to send response to");
    return FALSE;
  }

  gsize data_length = 0;
  const uint8_t* data = nullptr;
  if (response != nullptr) {
    data =
        static_cast<const uint8_t*>(g_bytes_get_data(response, &data_length));
  }
  FlutterEngineResult result = self->embedder_api.SendPlatformMessageResponse(
      self->engine, handle, data, data_length);

  if (result != kSuccess) {
    g_set_error(error, fl_engine_error_quark(), FL_ENGINE_ERROR_FAILED,
                "Failed to send platform message response");
    return FALSE;
  }

  return TRUE;
}

void fl_engine_send_platform_message(FlEngine* self,
                                     const gchar* channel,
                                     GBytes* message,
                                     GCancellable* cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data) {
  g_return_if_fail(FL_IS_ENGINE(self));

  GTask* task = nullptr;
  FlutterPlatformMessageResponseHandle* response_handle = nullptr;
  if (callback != nullptr) {
    task = g_task_new(self, cancellable, callback, user_data);

    if (self->engine == nullptr) {
      g_task_return_new_error(task, fl_engine_error_quark(),
                              FL_ENGINE_ERROR_FAILED, "No engine to send to");
      return;
    }

    FlutterEngineResult result =
        self->embedder_api.PlatformMessageCreateResponseHandle(
            self->engine, fl_engine_platform_message_response_cb, task,
            &response_handle);
    if (result != kSuccess) {
      g_task_return_new_error(task, fl_engine_error_quark(),
                              FL_ENGINE_ERROR_FAILED,
                              "Failed to create response handle");
      g_object_unref(task);
      return;
    }
  } else if (self->engine == nullptr) {
    return;
  }

  FlutterPlatformMessage fl_message = {};
  fl_message.struct_size = sizeof(fl_message);
  fl_message.channel = channel;
  fl_message.message =
      message != nullptr
          ? static_cast<const uint8_t*>(g_bytes_get_data(message, nullptr))
          : nullptr;
  fl_message.message_size = message != nullptr ? g_bytes_get_size(message) : 0;
  fl_message.response_handle = response_handle;
  FlutterEngineResult result =
      self->embedder_api.SendPlatformMessage(self->engine, &fl_message);

  if (result != kSuccess && task != nullptr) {
    g_task_return_new_error(task, fl_engine_error_quark(),
                            FL_ENGINE_ERROR_FAILED,
                            "Failed to send platform messages");
    g_object_unref(task);
  }

  if (response_handle != nullptr) {
    self->embedder_api.PlatformMessageReleaseResponseHandle(self->engine,
                                                            response_handle);
  }
}

GBytes* fl_engine_send_platform_message_finish(FlEngine* self,
                                               GAsyncResult* result,
                                               GError** error) {
  g_return_val_if_fail(FL_IS_ENGINE(self), FALSE);
  g_return_val_if_fail(g_task_is_valid(result, self), FALSE);

  return static_cast<GBytes*>(g_task_propagate_pointer(G_TASK(result), error));
}

void fl_engine_send_window_metrics_event(FlEngine* self,
                                         size_t width,
                                         size_t height,
                                         double pixel_ratio) {
  g_return_if_fail(FL_IS_ENGINE(self));

  if (self->engine == nullptr) {
    return;
  }

  FlutterWindowMetricsEvent event = {};
  event.struct_size = sizeof(FlutterWindowMetricsEvent);
  event.width = width;
  event.height = height;
  event.pixel_ratio = pixel_ratio;
  self->embedder_api.SendWindowMetricsEvent(self->engine, &event);
}

void fl_engine_send_mouse_pointer_event(FlEngine* self,
                                        FlutterPointerPhase phase,
                                        size_t timestamp,
                                        double x,
                                        double y,
                                        double scroll_delta_x,
                                        double scroll_delta_y,
                                        int64_t buttons) {
  g_return_if_fail(FL_IS_ENGINE(self));

  if (self->engine == nullptr) {
    return;
  }

  FlutterPointerEvent fl_event = {};
  fl_event.struct_size = sizeof(fl_event);
  fl_event.phase = phase;
  fl_event.timestamp = timestamp;
  fl_event.x = x;
  fl_event.y = y;
  if (scroll_delta_x != 0 || scroll_delta_y != 0) {
    fl_event.signal_kind = kFlutterPointerSignalKindScroll;
  }
  fl_event.scroll_delta_x = scroll_delta_x;
  fl_event.scroll_delta_y = scroll_delta_y;
  fl_event.device_kind = kFlutterPointerDeviceKindMouse;
  fl_event.buttons = buttons;
  self->embedder_api.SendPointerEvent(self->engine, &fl_event, 1);
}

void fl_engine_dispatch_semantics_action(FlEngine* self,
                                         uint64_t id,
                                         FlutterSemanticsAction action,
                                         GBytes* data) {
  g_return_if_fail(FL_IS_ENGINE(self));

  if (self->engine == nullptr) {
    return;
  }

  const uint8_t* action_data = nullptr;
  size_t action_data_length = 0;
  if (data != nullptr) {
    action_data = static_cast<const uint8_t*>(
        g_bytes_get_data(data, &action_data_length));
  }

  self->embedder_api.DispatchSemanticsAction(self->engine, id, action,
                                             action_data, action_data_length);
}

G_MODULE_EXPORT FlBinaryMessenger* fl_engine_get_binary_messenger(
    FlEngine* self) {
  g_return_val_if_fail(FL_IS_ENGINE(self), nullptr);
  return self->binary_messenger;
}

FlTaskRunner* fl_engine_get_task_runner(FlEngine* self) {
  g_return_val_if_fail(FL_IS_ENGINE(self), nullptr);
  return self->task_runner;
}

void fl_engine_execute_task(FlEngine* self, FlutterTask* task) {
  g_return_if_fail(FL_IS_ENGINE(self));
  self->embedder_api.RunTask(self->engine, task);
}
