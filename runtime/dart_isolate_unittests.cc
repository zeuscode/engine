// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/runtime/dart_isolate.h"

#include "flutter/fml/mapping.h"
#include "flutter/fml/synchronization/count_down_latch.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/fml/thread.h"
#include "flutter/runtime/dart_vm.h"
#include "flutter/runtime/dart_vm_lifecycle.h"
#include "flutter/runtime/isolate_configuration.h"
#include "flutter/testing/dart_isolate_runner.h"
#include "flutter/testing/fixture_test.h"
#include "flutter/testing/testing.h"
#include "third_party/tonic/converter/dart_converter.h"
#include "third_party/tonic/scopes/dart_isolate_scope.h"

namespace flutter {
namespace testing {

class DartIsolateTest : public FixtureTest {
 public:
  DartIsolateTest() {}

  void Wait() { latch_.Wait(); }

  void Signal() { latch_.Signal(); }

 private:
  fml::AutoResetWaitableEvent latch_;

  FML_DISALLOW_COPY_AND_ASSIGN(DartIsolateTest);
};

TEST_F(DartIsolateTest, RootIsolateCreationAndShutdown) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );

  auto isolate_configuration =
      IsolateConfiguration::InferFromSettings(settings);

  auto weak_isolate = DartIsolate::CreateRunningRootIsolate(
      vm_data->GetSettings(),              // settings
      vm_data->GetIsolateSnapshot(),       // isolate snapshot
      std::move(task_runners),             // task runners
      nullptr,                             // window
      {},                                  // snapshot delegate
      {},                                  // hint freed delegate
      {},                                  // io manager
      {},                                  // unref queue
      {},                                  // image decoder
      {},                                  // image generator registry
      "main.dart",                         // advisory uri
      "main",                              // advisory entrypoint,
      DartIsolate::Flags{},                // flags
      settings.isolate_create_callback,    // isolate create callback
      settings.isolate_shutdown_callback,  // isolate shutdown callback
      "main",                              // dart entrypoint
      std::nullopt,                        // dart entrypoint library
      std::move(isolate_configuration),    // isolate configuration
      nullptr                              // Volatile path tracker
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::Running);
  ASSERT_TRUE(root_isolate->Shutdown());
}

TEST_F(DartIsolateTest, SpawnIsolate) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );

  auto isolate_configuration =
      IsolateConfiguration::InferFromSettings(settings);

  auto weak_isolate = DartIsolate::CreateRunningRootIsolate(
      vm_data->GetSettings(),              // settings
      vm_data->GetIsolateSnapshot(),       // isolate snapshot
      std::move(task_runners),             // task runners
      nullptr,                             // window
      {},                                  // snapshot delegate
      {},                                  // hint freed delegate
      {},                                  // io manager
      {},                                  // unref queue
      {},                                  // image decoder
      {},                                  // image generator registry
      "main.dart",                         // advisory uri
      "main",                              // advisory entrypoint,
      DartIsolate::Flags{},                // flags
      settings.isolate_create_callback,    // isolate create callback
      settings.isolate_shutdown_callback,  // isolate shutdown callback
      "main",                              // dart entrypoint
      std::nullopt,                        // dart entrypoint library
      std::move(isolate_configuration),    // isolate configuration
      nullptr                              // Volatile path tracker
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::Running);

  auto spawn_configuration = IsolateConfiguration::InferFromSettings(settings);

  auto weak_spawn = root_isolate->SpawnIsolate(
      /*settings=*/vm_data->GetSettings(),
      /*platform_configuration=*/nullptr,
      /*snapshot_delegate=*/{},
      /*hint_freed_delegate=*/{},
      /*advisory_script_uri=*/"main.dart",
      /*advisory_script_entrypoint=*/"main",
      /*flags=*/DartIsolate::Flags{},
      /*isolate_create_callback=*/settings.isolate_create_callback,
      /*isolate_shutdown_callback=*/settings.isolate_shutdown_callback,
      /*dart_entrypoint=*/"main",
      /*dart_entrypoint_library=*/std::nullopt,
      /*isolate_configration=*/std::move(spawn_configuration));
  auto spawn = weak_spawn.lock();
  ASSERT_TRUE(spawn);
  ASSERT_EQ(spawn->GetPhase(), DartIsolate::Phase::Running);

  // TODO(74520): Remove conditional once isolate groups are supported by JIT.
  if (DartVM::IsRunningPrecompiledCode()) {
    Dart_IsolateGroup isolate_group;
    {
      auto isolate_scope = tonic::DartIsolateScope(root_isolate->isolate());
      isolate_group = Dart_CurrentIsolateGroup();
    }
    {
      auto isolate_scope = tonic::DartIsolateScope(root_isolate->isolate());
      Dart_IsolateGroup spawn_isolate_group = Dart_CurrentIsolateGroup();
      ASSERT_TRUE(isolate_group != nullptr);
      ASSERT_EQ(isolate_group, spawn_isolate_group);
    }
  }

  ASSERT_TRUE(spawn->Shutdown());
  ASSERT_TRUE(spawn->IsShuttingDown());
  ASSERT_TRUE(root_isolate->Shutdown());
}

TEST_F(DartIsolateTest, IsolateShutdownCallbackIsInIsolateScope) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate_configuration =
      IsolateConfiguration::InferFromSettings(settings);
  auto weak_isolate = DartIsolate::CreateRunningRootIsolate(
      vm_data->GetSettings(),              // settings
      vm_data->GetIsolateSnapshot(),       // isolate snapshot
      std::move(task_runners),             // task runners
      nullptr,                             // window
      {},                                  // snapshot delegate
      {},                                  // hint freed delegate
      {},                                  // io manager
      {},                                  // unref queue
      {},                                  // image decoder
      {},                                  // image generator registry
      "main.dart",                         // advisory uri
      "main",                              // advisory entrypoint
      DartIsolate::Flags{},                // flags
      settings.isolate_create_callback,    // isolate create callback
      settings.isolate_shutdown_callback,  // isolate shutdown callback
      "main",                              // dart entrypoint
      std::nullopt,                        // dart entrypoint library
      std::move(isolate_configuration),    // isolate configuration
      nullptr                              // Volatile path tracker
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::Running);
  size_t destruction_callback_count = 0;
  root_isolate->AddIsolateShutdownCallback([&destruction_callback_count]() {
    ASSERT_NE(Dart_CurrentIsolate(), nullptr);
    destruction_callback_count++;
  });
  ASSERT_TRUE(root_isolate->Shutdown());
  ASSERT_EQ(destruction_callback_count, 1u);
}

TEST_F(DartIsolateTest, IsolateCanLoadAndRunDartCode) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners, "main",
                                      {}, GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
}

TEST_F(DartIsolateTest, IsolateCannotLoadAndRunUnknownDartEntrypoint) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate =
      RunDartCodeInIsolate(vm_ref, settings, task_runners, "thisShouldNotExist",
                           {}, GetDefaultKernelFilePath());
  ASSERT_FALSE(isolate);
}

TEST_F(DartIsolateTest, CanRunDartCodeCodeSynchronously) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners, "main",
                                      {}, GetDefaultKernelFilePath());

  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  ASSERT_TRUE(isolate->RunInIsolateScope([]() -> bool {
    if (tonic::LogIfError(::Dart_Invoke(Dart_RootLibrary(),
                                        tonic::ToDart("sayHi"), 0, nullptr))) {
      return false;
    }
    return true;
  }));
}

TEST_F(DartIsolateTest, CanRegisterNativeCallback) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
                      FML_LOG(ERROR) << "Hello from Dart!";
                      Signal();
                    })));
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "canRegisterNativeCallback", {},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  Wait();
}

TEST_F(DartIsolateTest, CanSaveCompilationTrace) {
  if (DartVM::IsRunningPrecompiledCode()) {
    // Can only save compilation traces in JIT modes.
    GTEST_SKIP();
    return;
  }
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
                      ASSERT_TRUE(tonic::DartConverter<bool>::FromDart(
                          Dart_GetNativeArgument(args, 0)));
                      Signal();
                    })));

  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanSaveCompilationTrace", {},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);

  Wait();
}

class DartSecondaryIsolateTest : public FixtureTest {
 public:
  DartSecondaryIsolateTest() : latch_(3) {}

  void LatchCountDown() { latch_.CountDown(); }

  void LatchWait() { latch_.Wait(); }

  void ChildShutdownSignal() { child_shutdown_latch_.Signal(); }

  void ChildShutdownWait() { child_shutdown_latch_.Wait(); }

  void RootIsolateShutdownSignal() { root_isolate_shutdown_latch_.Signal(); }

  bool RootIsolateIsSignaled() {
    return root_isolate_shutdown_latch_.IsSignaledForTest();
  }

 private:
  fml::CountDownLatch latch_;
  fml::AutoResetWaitableEvent child_shutdown_latch_;
  fml::AutoResetWaitableEvent root_isolate_shutdown_latch_;

  FML_DISALLOW_COPY_AND_ASSIGN(DartSecondaryIsolateTest);
};

TEST_F(DartSecondaryIsolateTest, CanLaunchSecondaryIsolates) {
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
                      LatchCountDown();
                    })));
  AddNativeCallback(
      "PassMessage", CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
        auto message = tonic::DartConverter<std::string>::FromDart(
            Dart_GetNativeArgument(args, 0));
        ASSERT_EQ("Hello from code is secondary isolate.", message);
        LatchCountDown();
      })));
  auto settings = CreateSettingsForFixture();
  settings.root_isolate_shutdown_callback = [this]() {
    RootIsolateShutdownSignal();
  };
  settings.isolate_shutdown_callback = [this]() { ChildShutdownSignal(); };
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanLaunchSecondaryIsolate", {},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  ChildShutdownWait();  // wait for child isolate to shutdown first
  ASSERT_FALSE(RootIsolateIsSignaled());
  LatchWait();  // wait for last NotifyNative called by main isolate
  // root isolate will be auto-shutdown
}

TEST_F(DartIsolateTest, CanRecieveArguments) {
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
                      ASSERT_TRUE(tonic::DartConverter<bool>::FromDart(
                          Dart_GetNativeArgument(args, 0)));
                      Signal();
                    })));

  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanRecieveArguments", {"arg1"},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);

  Wait();
}

TEST_F(DartIsolateTest, CanCreateServiceIsolate) {
#if (FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_DEBUG) && \
    (FLUTTER_RUNTIME_MODE != FLUTTER_RUNTIME_MODE_PROFILE)
  GTEST_SKIP();
#endif
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  fml::AutoResetWaitableEvent service_isolate_latch;
  auto settings = CreateSettingsForFixture();
  settings.enable_observatory = true;
  settings.observatory_port = 0;
  settings.observatory_host = "127.0.0.1";
  settings.enable_service_port_fallback = true;
  settings.service_isolate_create_callback = [&service_isolate_latch]() {
    service_isolate_latch.Signal();
  };
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );

  auto isolate_configuration =
      IsolateConfiguration::InferFromSettings(settings);
  auto weak_isolate = DartIsolate::CreateRunningRootIsolate(
      vm_data->GetSettings(),              // settings
      vm_data->GetIsolateSnapshot(),       // isolate snapshot
      std::move(task_runners),             // task runners
      nullptr,                             // window
      {},                                  // snapshot delegate
      {},                                  // hint freed delegate
      {},                                  // io manager
      {},                                  // unref queue
      {},                                  // image decoder
      {},                                  // image generator registry
      "main.dart",                         // advisory uri
      "main",                              // advisory entrypoint,
      DartIsolate::Flags{},                // flags
      settings.isolate_create_callback,    // isolate create callback
      settings.isolate_shutdown_callback,  // isolate shutdown callback
      "main",                              // dart entrypoint
      std::nullopt,                        // dart entrypoint library
      std::move(isolate_configuration),    // isolate configuration
      nullptr                              // Volatile path tracker
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::Running);
  service_isolate_latch.Wait();
  ASSERT_TRUE(root_isolate->Shutdown());
}

TEST_F(DartIsolateTest,
       RootIsolateCreateCallbackIsMadeOnceAndBeforeIsolateRunning) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  size_t create_callback_count = 0u;
  settings.root_isolate_create_callback =
      [&create_callback_count](const auto& isolate) {
        ASSERT_EQ(isolate.GetPhase(), DartIsolate::Phase::Ready);
        create_callback_count++;
        ASSERT_NE(::Dart_CurrentIsolate(), nullptr);
      };
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  {
    auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners, "main",
                                        {}, GetDefaultKernelFilePath());
    ASSERT_TRUE(isolate);
    ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  }
  ASSERT_EQ(create_callback_count, 1u);
}

TEST_F(DartIsolateTest,
       IsolateCreateCallbacksTakeInstanceSettingsInsteadOfVMSettings) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto vm_settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(vm_settings);
  auto instance_settings = vm_settings;
  size_t create_callback_count = 0u;
  instance_settings.root_isolate_create_callback =
      [&create_callback_count](const auto& isolate) {
        ASSERT_EQ(isolate.GetPhase(), DartIsolate::Phase::Ready);
        create_callback_count++;
        ASSERT_NE(::Dart_CurrentIsolate(), nullptr);
      };
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  {
    auto isolate = RunDartCodeInIsolate(vm_ref, instance_settings, task_runners,
                                        "main", {}, GetDefaultKernelFilePath());
    ASSERT_TRUE(isolate);
    ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  }
  ASSERT_EQ(create_callback_count, 1u);
}

TEST_F(DartIsolateTest, InvalidLoadingUnitFails) {
  if (!DartVM::IsRunningPrecompiledCode()) {
    FML_LOG(INFO) << "Split AOT does not work in JIT mode";
    return;
  }
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate_configuration =
      IsolateConfiguration::InferFromSettings(settings);
  auto weak_isolate = DartIsolate::CreateRunningRootIsolate(
      vm_data->GetSettings(),              // settings
      vm_data->GetIsolateSnapshot(),       // isolate snapshot
      std::move(task_runners),             // task runners
      nullptr,                             // window
      {},                                  // snapshot delegate
      {},                                  // hint freed delegate
      {},                                  // io manager
      {},                                  // unref queue
      {},                                  // image decoder
      {},                                  // image generator registry
      "main.dart",                         // advisory uri
      "main",                              // advisory entrypoint
      DartIsolate::Flags{},                // flags
      settings.isolate_create_callback,    // isolate create callback
      settings.isolate_shutdown_callback,  // isolate shutdown callback
      "main",                              // dart entrypoint
      std::nullopt,                        // dart entrypoint library
      std::move(isolate_configuration),    // isolate configuration
      nullptr                              // volatile path tracker
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::Running);

  auto isolate_data = std::make_unique<const fml::NonOwnedMapping>(
      split_aot_symbols_.vm_isolate_data, 0);
  auto isolate_instructions = std::make_unique<const fml::NonOwnedMapping>(
      split_aot_symbols_.vm_isolate_instrs, 0);

  // Invalid loading unit should fail gracefully with error message.
  ASSERT_FALSE(root_isolate->LoadLoadingUnit(3, std::move(isolate_data),
                                             std::move(isolate_instructions)));
  ASSERT_TRUE(root_isolate->Shutdown());
}

// TODO(garyq): Re-enable this test, and resolve dart-side hanging future and
// threading. See https://github.com/flutter/flutter/issues/72312
TEST_F(DartIsolateTest, DISABLED_ValidLoadingUnitSucceeds) {
  if (!DartVM::IsRunningPrecompiledCode()) {
    FML_LOG(INFO) << "Split AOT does not work in JIT mode";
    return;
  }

  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([this](Dart_NativeArguments args) {
                      FML_LOG(ERROR) << "Hello from Dart!";
                      Signal();
                    })));
  AddNativeCallback(
      "NotifySuccess", CREATE_NATIVE_ENTRY([this](Dart_NativeArguments args) {
        auto bool_handle = Dart_GetNativeArgument(args, 0);
        ASSERT_FALSE(tonic::LogIfError(bool_handle));
        ASSERT_TRUE(tonic::DartConverter<bool>::FromDart(bool_handle));
        Signal();
      }));
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "canCallDeferredLibrary", {},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  Wait();

  auto isolate_data = std::make_unique<const fml::NonOwnedMapping>(
      split_aot_symbols_.vm_isolate_data, 0);
  auto isolate_instructions = std::make_unique<const fml::NonOwnedMapping>(
      split_aot_symbols_.vm_isolate_instrs, 0);

  ASSERT_TRUE(isolate->get()->LoadLoadingUnit(2, std::move(isolate_data),
                                              std::move(isolate_instructions)));
  Wait();
}

TEST_F(DartIsolateTest, DartPluginRegistrantIsCalled) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());

  std::vector<std::string> messages;
  fml::AutoResetWaitableEvent latch;

  AddNativeCallback(
      "PassMessage",
      CREATE_NATIVE_ENTRY(([&latch, &messages](Dart_NativeArguments args) {
        auto message = tonic::DartConverter<std::string>::FromDart(
            Dart_GetNativeArgument(args, 0));
        messages.push_back(message);
        latch.Signal();
      })));

  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "mainForPluginRegistrantTest", {},
                                      GetDefaultKernelFilePath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  latch.Wait();
  ASSERT_EQ(messages.size(), 1u);
  ASSERT_EQ(messages[0], "_PluginRegistrant.register() was called");
}

}  // namespace testing
}  // namespace flutter
