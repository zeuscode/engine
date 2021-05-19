// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/lib/ui/painting/image_generator_registry.h"

#include "flutter/fml/mapping.h"
#include "flutter/shell/common/shell_test.h"
#include "flutter/testing/testing.h"

namespace flutter {
namespace testing {

static sk_sp<SkData> LoadValidImageFixture() {
  auto fixture_mapping = OpenFixtureAsMapping("DashInNooglerHat.jpg");

  // Remap to sk_sp<SkData>.
  SkData::ReleaseProc on_release = [](const void* ptr, void* context) -> void {
    delete reinterpret_cast<fml::FileMapping*>(context);
  };
  auto data = SkData::MakeWithProc(fixture_mapping->GetMapping(),
                                   fixture_mapping->GetSize(), on_release,
                                   fixture_mapping.get());

  if (data) {
    fixture_mapping.release();
  }

  return data;
}

TEST_F(ShellTest, CreateCompatibleReturnsBuiltinImageGeneratorForValidImage) {
  auto data = LoadValidImageFixture();

  // Fetch the generator and query for basic info
  ImageGeneratorRegistry registry;
  auto result = registry.CreateCompatibleGenerator(data);
  auto info = result->GetInfo();
  ASSERT_EQ(info.width(), 3024);
  ASSERT_EQ(info.height(), 4032);
}

TEST_F(ShellTest, CreateCompatibleReturnsNullptrForInvalidImage) {
  ImageGeneratorRegistry registry;
  auto result = registry.CreateCompatibleGenerator(SkData::MakeEmpty());
  ASSERT_EQ(result, nullptr);
}

class FakeImageGenerator : public ImageGenerator {
 public:
  FakeImageGenerator(int identifiableFakeWidth)
      : info_(SkImageInfo::Make(identifiableFakeWidth,
                                identifiableFakeWidth,
                                SkColorType::kRGBA_8888_SkColorType,
                                SkAlphaType::kOpaque_SkAlphaType)){};
  ~FakeImageGenerator() = default;
  const SkImageInfo& GetInfo() const { return info_; }

  unsigned int GetFrameCount() const { return 1; }

  const ImageGenerator::FrameInfo GetFrameInfo(unsigned int frame_index) const {
    return {std::nullopt, 0, SkCodecAnimation::DisposalMethod::kKeep};
  }

  SkISize GetScaledDimensions(float scale) const {
    return SkISize::Make(info_.width(), info_.height());
  }

  bool GetPixels(const SkImageInfo& info,
                 void* pixels,
                 size_t row_bytes,
                 unsigned int frame_index,
                 std::optional<unsigned int> prior_frame) const {
    return false;
  };

 private:
  SkImageInfo info_;
};

TEST_F(ShellTest, PositivePriorityTakesPrecedentOverDefaultGenerators) {
  ImageGeneratorRegistry registry;

  const int fake_width = 1337;
  registry.AddFactory(
      [fake_width](sk_sp<SkData> buffer) {
        return std::make_unique<FakeImageGenerator>(fake_width);
      },
      1);

  // Fetch the generator and query for basic info.
  auto result = registry.CreateCompatibleGenerator(LoadValidImageFixture());
  ASSERT_EQ(result->GetInfo().width(), fake_width);
}

TEST_F(ShellTest, DefaultGeneratorsTakePrecedentOverNegativePriority) {
  ImageGeneratorRegistry registry;

  registry.AddFactory(
      [](sk_sp<SkData> buffer) {
        return std::make_unique<FakeImageGenerator>(1337);
      },
      -1);

  // Fetch the generator and query for basic info.
  auto result = registry.CreateCompatibleGenerator(LoadValidImageFixture());
  // If the real width of the image pops out, then the default generator was
  // returned rather than the fake one.
  ASSERT_EQ(result->GetInfo().width(), 3024);
}

}  // namespace testing
}  // namespace flutter
