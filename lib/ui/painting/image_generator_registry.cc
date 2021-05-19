// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "flutter/fml/trace_event.h"
#include "flutter/lib/ui/painting/image_generator_registry.h"
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "third_party/skia/src/codec/SkCodecImageGenerator.h"
#ifdef OS_MACOSX
#include "third_party/skia/include/ports/SkImageGeneratorCG.h"
#elif OS_WIN
#include "third_party/skia/include/ports/SkImageGeneratorWIC.h"
#endif

namespace flutter {

ImageGeneratorRegistry::ImageGeneratorRegistry() : weak_factory_(this) {
  AddFactory(
      [](sk_sp<SkData> buffer) {
        return BuiltinSkiaCodecImageGenerator::MakeFromData(buffer);
      },
      0);

  // todo(bdero): https://github.com/flutter/flutter/issues/82603
#ifdef OS_MACOSX
  AddFactory(
      [](sk_sp<SkData> buffer) {
        auto generator = SkImageGeneratorCG::MakeFromEncodedCG(buffer);
        return BuiltinSkiaImageGenerator::MakeFromGenerator(
            std::move(generator));
      },
      0);
#elif OS_WIN
  AddFactory(
      [](sk_sp<SkData> buffer) {
        auto generator = SkImageGeneratorWIC::MakeFromEncodedWIC(buffer);
        return BuiltinSkiaImageGenerator::MakeFromGenerator(
            std::move(generator));
      },
      0);
#endif
}

ImageGeneratorRegistry::~ImageGeneratorRegistry() = default;

void ImageGeneratorRegistry::AddFactory(ImageGeneratorFactory factory,
                                        int32_t priority) {
  image_generator_factories_.insert(
      {factory, priority, fml::tracing::TraceNonce()});
}

std::unique_ptr<ImageGenerator>
ImageGeneratorRegistry::CreateCompatibleGenerator(sk_sp<SkData> buffer) {
  for (auto& factory : image_generator_factories_) {
    std::unique_ptr<ImageGenerator> result = factory.callback(buffer);
    if (result) {
      return result;
    }
  }
  return nullptr;
}

fml::WeakPtr<ImageGeneratorRegistry> ImageGeneratorRegistry::GetWeakPtr()
    const {
  return weak_factory_.GetWeakPtr();
}

}  // namespace flutter
