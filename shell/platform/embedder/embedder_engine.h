// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_

#include <memory>
#include <unordered_map>

#include "flutter/fml/macros.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/embedder/embedder_external_texture_resolver.h"
#include "flutter/shell/platform/embedder/embedder_thread_host.h"
namespace flutter {

struct ShellArgs;

// The object that is returned to the embedder as an opaque pointer to the
// instance of the Flutter engine.
class EmbedderEngine {
 public:
  EmbedderEngine(std::unique_ptr<EmbedderThreadHost> thread_host,
                 TaskRunners task_runners,
                 Settings settings,
                 RunConfiguration run_configuration,
                 Shell::CreateCallback<PlatformView> on_create_platform_view,
                 Shell::CreateCallback<Rasterizer> on_create_rasterizer,
                 std::unique_ptr<EmbedderExternalTextureResolver>
                     external_texture_resolver);

  ~EmbedderEngine();

  bool LaunchShell();

  bool CollectShell();

  const TaskRunners& GetTaskRunners() const;

  bool NotifyCreated();

  bool NotifyDestroyed();

  bool RunRootIsolate();

  bool IsValid() const;

  bool SetViewportMetrics(flutter::ViewportMetrics metrics);

  bool DispatchPointerDataPacket(
      std::unique_ptr<flutter::PointerDataPacket> packet);

  //----------------------------------------------------------------------------
  /// @brief      Notifies the platform view that the embedder has sent it a key
  ///             data packet. A key data packet contains one key event. This
  ///             call originates in the platform view and the shell has
  ///             forwarded the same to the engine on the UI task runner here.
  ///             The platform view will decide whether to handle this event,
  ///             and send the result using `callback`, which will be called
  ///             exactly once.
  ///
  /// @param[in]  packet    The key data packet.
  /// @param[in]  callback  Called when the framework has decided whether
  ///                       to handle this key data.
  ///
  bool DispatchKeyDataPacket(std::unique_ptr<flutter::KeyDataPacket> packet,
                             KeyDataResponse callback);

  bool SendPlatformMessage(std::unique_ptr<PlatformMessage> message);

  bool RegisterTexture(int64_t texture);

  bool UnregisterTexture(int64_t texture);

  bool MarkTextureFrameAvailable(int64_t texture);

  bool SetSemanticsEnabled(bool enabled);

  bool SetAccessibilityFeatures(int32_t flags);

  bool DispatchSemanticsAction(int id,
                               flutter::SemanticsAction action,
                               fml::MallocMapping args);

  bool OnVsyncEvent(intptr_t baton,
                    fml::TimePoint frame_start_time,
                    fml::TimePoint frame_target_time);

  bool ReloadSystemFonts();

  bool PostRenderThreadTask(const fml::closure& task);

  bool RunTask(const FlutterTask* task);

  bool PostTaskOnEngineManagedNativeThreads(
      std::function<void(FlutterNativeThreadType)> closure) const;

  Shell& GetShell();

 private:
  const std::unique_ptr<EmbedderThreadHost> thread_host_;
  TaskRunners task_runners_;
  RunConfiguration run_configuration_;
  std::unique_ptr<ShellArgs> shell_args_;
  std::unique_ptr<Shell> shell_;
  std::unique_ptr<EmbedderExternalTextureResolver> external_texture_resolver_;

  FML_DISALLOW_COPY_AND_ASSIGN(EmbedderEngine);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_EMBEDDER_EMBEDDER_ENGINE_H_
