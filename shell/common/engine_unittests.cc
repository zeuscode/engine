// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/common/engine.h"

#include <cstring>

#include "flutter/runtime/dart_vm_lifecycle.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/testing/fixture_test.h"
#include "flutter/testing/testing.h"
#include "gmock/gmock.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

///\note Deprecated MOCK_METHOD macros used until this issue is resolved:
// https://github.com/google/googletest/issues/2490

namespace flutter {

namespace {
class MockDelegate : public Engine::Delegate {
 public:
  MOCK_METHOD2(OnEngineUpdateSemantics,
               void(SemanticsNodeUpdates, CustomAccessibilityActionUpdates));
  MOCK_METHOD1(OnEngineHandlePlatformMessage,
               void(std::unique_ptr<PlatformMessage>));
  MOCK_METHOD0(OnPreEngineRestart, void());
  MOCK_METHOD0(OnRootIsolateCreated, void());
  MOCK_METHOD2(UpdateIsolateDescription, void(const std::string, int64_t));
  MOCK_METHOD1(SetNeedsReportTimings, void(bool));
  MOCK_METHOD1(ComputePlatformResolvedLocale,
               std::unique_ptr<std::vector<std::string>>(
                   const std::vector<std::string>&));
  MOCK_METHOD1(RequestDartDeferredLibrary, void(intptr_t));
  MOCK_METHOD0(GetCurrentTimePoint, fml::TimePoint());
};

class MockResponse : public PlatformMessageResponse {
 public:
  MOCK_METHOD1(Complete, void(std::unique_ptr<fml::Mapping> data));
  MOCK_METHOD0(CompleteEmpty, void());
};

class MockRuntimeDelegate : public RuntimeDelegate {
 public:
  MOCK_METHOD0(DefaultRouteName, std::string());
  MOCK_METHOD1(ScheduleFrame, void(bool));
  MOCK_METHOD1(Render, void(std::unique_ptr<flutter::LayerTree>));
  MOCK_METHOD2(UpdateSemantics,
               void(SemanticsNodeUpdates, CustomAccessibilityActionUpdates));
  MOCK_METHOD1(HandlePlatformMessage, void(std::unique_ptr<PlatformMessage>));
  MOCK_METHOD0(GetFontCollection, FontCollection&());
  MOCK_METHOD0(OnRootIsolateCreated, void());
  MOCK_METHOD2(UpdateIsolateDescription, void(const std::string, int64_t));
  MOCK_METHOD1(SetNeedsReportTimings, void(bool));
  MOCK_METHOD1(ComputePlatformResolvedLocale,
               std::unique_ptr<std::vector<std::string>>(
                   const std::vector<std::string>&));
  MOCK_METHOD1(RequestDartDeferredLibrary, void(intptr_t));
};

class MockRuntimeController : public RuntimeController {
 public:
  MockRuntimeController(RuntimeDelegate& client, TaskRunners p_task_runners)
      : RuntimeController(client, p_task_runners) {}
  MOCK_METHOD0(IsRootIsolateRunning, bool());
  MOCK_METHOD1(DispatchPlatformMessage, bool(std::unique_ptr<PlatformMessage>));
  MOCK_METHOD3(LoadDartDeferredLibraryError,
               void(intptr_t, const std::string, bool));
  MOCK_CONST_METHOD0(GetDartVM, DartVM*());
  MOCK_METHOD2(NotifyIdle, bool(int64_t, size_t));
};

std::unique_ptr<PlatformMessage> MakePlatformMessage(
    const std::string& channel,
    const std::map<std::string, std::string>& values,
    fml::RefPtr<PlatformMessageResponse> response) {
  rapidjson::Document document;
  auto& allocator = document.GetAllocator();
  document.SetObject();

  for (const auto& pair : values) {
    rapidjson::Value key(pair.first.c_str(), strlen(pair.first.c_str()),
                         allocator);
    rapidjson::Value value(pair.second.c_str(), strlen(pair.second.c_str()),
                           allocator);
    document.AddMember(key, value, allocator);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.GetString());

  std::unique_ptr<PlatformMessage> message = std::make_unique<PlatformMessage>(
      channel, fml::MallocMapping::Copy(data, buffer.GetSize()), response);
  return message;
}

class EngineTest : public testing::FixtureTest {
 public:
  EngineTest()
      : thread_host_("EngineTest",
                     ThreadHost::Type::Platform | ThreadHost::Type::IO |
                         ThreadHost::Type::UI | ThreadHost::Type::RASTER),
        task_runners_({
            "EngineTest",
            thread_host_.platform_thread->GetTaskRunner(),  // platform
            thread_host_.raster_thread->GetTaskRunner(),    // raster
            thread_host_.ui_thread->GetTaskRunner(),        // ui
            thread_host_.io_thread->GetTaskRunner()         // io
        }) {}

  void PostUITaskSync(const std::function<void()>& function) {
    fml::AutoResetWaitableEvent latch;
    task_runners_.GetUITaskRunner()->PostTask([&] {
      function();
      latch.Signal();
    });
    latch.Wait();
  }

 protected:
  void SetUp() override {
    settings_ = CreateSettingsForFixture();
    dispatcher_maker_ = [](PointerDataDispatcher::Delegate&) {
      return nullptr;
    };
  }

  MockDelegate delegate_;
  PointerDataDispatcherMaker dispatcher_maker_;
  ThreadHost thread_host_;
  TaskRunners task_runners_;
  Settings settings_;
  std::unique_ptr<Animator> animator_;
  fml::WeakPtr<IOManager> io_manager_;
  std::unique_ptr<RuntimeController> runtime_controller_;
  std::shared_ptr<fml::ConcurrentTaskRunner> image_decoder_task_runner_;
};
}  // namespace

TEST_F(EngineTest, Create) {
  PostUITaskSync([this] {
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(runtime_controller_));
    EXPECT_TRUE(engine);
  });
}

TEST_F(EngineTest, DispatchPlatformMessageUnknown) {
  PostUITaskSync([this] {
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);
    EXPECT_CALL(*mock_runtime_controller, IsRootIsolateRunning())
        .WillRepeatedly(::testing::Return(false));
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    fml::RefPtr<PlatformMessageResponse> response =
        fml::MakeRefCounted<MockResponse>();
    std::unique_ptr<PlatformMessage> message =
        std::make_unique<PlatformMessage>("foo", response);
    engine->DispatchPlatformMessage(std::move(message));
  });
}

TEST_F(EngineTest, DispatchPlatformMessageInitialRoute) {
  PostUITaskSync([this] {
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);
    EXPECT_CALL(*mock_runtime_controller, IsRootIsolateRunning())
        .WillRepeatedly(::testing::Return(false));
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    fml::RefPtr<PlatformMessageResponse> response =
        fml::MakeRefCounted<MockResponse>();
    std::map<std::string, std::string> values{
        {"method", "setInitialRoute"},
        {"args", "test_initial_route"},
    };
    std::unique_ptr<PlatformMessage> message =
        MakePlatformMessage("flutter/navigation", values, response);
    engine->DispatchPlatformMessage(std::move(message));
    EXPECT_EQ(engine->InitialRoute(), "test_initial_route");
  });
}

TEST_F(EngineTest, DispatchPlatformMessageInitialRouteIgnored) {
  PostUITaskSync([this] {
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);
    EXPECT_CALL(*mock_runtime_controller, IsRootIsolateRunning())
        .WillRepeatedly(::testing::Return(true));
    EXPECT_CALL(*mock_runtime_controller, DispatchPlatformMessage(::testing::_))
        .WillRepeatedly(::testing::Return(true));
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    fml::RefPtr<PlatformMessageResponse> response =
        fml::MakeRefCounted<MockResponse>();
    std::map<std::string, std::string> values{
        {"method", "setInitialRoute"},
        {"args", "test_initial_route"},
    };
    std::unique_ptr<PlatformMessage> message =
        MakePlatformMessage("flutter/navigation", values, response);
    engine->DispatchPlatformMessage(std::move(message));
    EXPECT_EQ(engine->InitialRoute(), "");
  });
}

TEST_F(EngineTest, SpawnSharesFontLibrary) {
  PostUITaskSync([this] {
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);
    auto vm_ref = DartVMRef::Create(settings_);
    EXPECT_CALL(*mock_runtime_controller, GetDartVM())
        .WillRepeatedly(::testing::Return(vm_ref.get()));
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    auto spawn =
        engine->Spawn(delegate_, dispatcher_maker_, settings_, nullptr);
    EXPECT_TRUE(spawn != nullptr);
    EXPECT_EQ(&engine->GetFontCollection(), &spawn->GetFontCollection());
  });
}

TEST_F(EngineTest, PassesLoadDartDeferredLibraryErrorToRuntime) {
  PostUITaskSync([this] {
    intptr_t error_id = 123;
    const std::string error_message = "error message";
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);
    EXPECT_CALL(*mock_runtime_controller, IsRootIsolateRunning())
        .WillRepeatedly(::testing::Return(true));
    EXPECT_CALL(*mock_runtime_controller,
                LoadDartDeferredLibraryError(error_id, error_message, true))
        .Times(1);
    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    engine->LoadDartDeferredLibraryError(error_id, error_message, true);
  });
}

TEST_F(EngineTest, NotifyIdleMoreThanOncePerFiveSeconds) {
  PostUITaskSync([this] {
    MockRuntimeDelegate client;
    auto mock_runtime_controller =
        std::make_unique<MockRuntimeController>(client, task_runners_);

    // Verify we get an initial call with the bytes, since it's 10 seconds.
    EXPECT_CALL(*mock_runtime_controller, NotifyIdle(200, 100)).Times(1);
    // Verify that we do not get called again, since it's only been 3 more
    // seconds.
    EXPECT_CALL(*mock_runtime_controller, NotifyIdle(400, 0)).Times(1);
    // Verify that we get called again, since it's now been 6 seconds.
    EXPECT_CALL(*mock_runtime_controller, NotifyIdle(600, 300 + 500)).Times(1);
    // Set up mocking of the timepoint logic. Calls at 10, 13, and 16 seconds.
    EXPECT_CALL(delegate_, GetCurrentTimePoint())
        .Times(3)
        .WillOnce(::testing::Return(
            fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromSeconds(10))))
        .WillOnce(::testing::Return(
            fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromSeconds(13))))
        .WillOnce(::testing::Return(
            fml::TimePoint::FromEpochDelta(fml::TimeDelta::FromSeconds(16))));

    auto engine = std::make_unique<Engine>(
        /*delegate=*/delegate_,
        /*dispatcher_maker=*/dispatcher_maker_,
        /*image_decoder_task_runner=*/image_decoder_task_runner_,
        /*task_runners=*/task_runners_,
        /*settings=*/settings_,
        /*animator=*/std::move(animator_),
        /*io_manager=*/io_manager_,
        /*font_collection=*/std::make_shared<FontCollection>(),
        /*runtime_controller=*/std::move(mock_runtime_controller));

    // Verifications are above, since we std::moved the unique_ptr for the
    // mock.
    engine->HintFreed(100);
    engine->NotifyIdle(200);

    engine->HintFreed(300);
    engine->NotifyIdle(400);

    engine->HintFreed(500);
    engine->NotifyIdle(600);
  });
}

}  // namespace flutter
