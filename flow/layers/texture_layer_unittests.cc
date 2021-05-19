// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/flow/layers/texture_layer.h"

#include "flutter/flow/testing/layer_test.h"
#include "flutter/flow/testing/mock_layer.h"
#include "flutter/flow/testing/mock_texture.h"
#include "flutter/fml/macros.h"
#include "flutter/testing/mock_canvas.h"

namespace flutter {
namespace testing {

using TextureLayerTest = LayerTest;

TEST_F(TextureLayerTest, InvalidTexture) {
  const SkPoint layer_offset = SkPoint::Make(0.0f, 0.0f);
  const SkSize layer_size = SkSize::Make(8.0f, 8.0f);
  auto layer = std::make_shared<TextureLayer>(layer_offset, layer_size, 0,
                                              false, SkSamplingOptions());

  layer->Preroll(preroll_context(), SkMatrix());
  EXPECT_EQ(layer->paint_bounds(),
            (SkRect::MakeSize(layer_size)
                 .makeOffset(layer_offset.fX, layer_offset.fY)));
  EXPECT_TRUE(layer->needs_painting(paint_context()));

  layer->Paint(paint_context());
  EXPECT_EQ(mock_canvas().draw_calls(), std::vector<MockCanvas::DrawCall>());
}

#ifndef NDEBUG
TEST_F(TextureLayerTest, PaintingEmptyLayerDies) {
  const SkPoint layer_offset = SkPoint::Make(0.0f, 0.0f);
  const SkSize layer_size = SkSize::Make(0.0f, 0.0f);
  const int64_t texture_id = 0;
  auto mock_texture = std::make_shared<MockTexture>(texture_id);
  auto layer = std::make_shared<TextureLayer>(
      layer_offset, layer_size, texture_id, false, SkSamplingOptions());

  // Ensure the texture is located by the Layer.
  preroll_context()->texture_registry.RegisterTexture(mock_texture);

  layer->Preroll(preroll_context(), SkMatrix());
  EXPECT_EQ(layer->paint_bounds(), kEmptyRect);
  EXPECT_FALSE(layer->needs_painting(paint_context()));

  EXPECT_DEATH_IF_SUPPORTED(layer->Paint(paint_context()),
                            "needs_painting\\(context\\)");
}

TEST_F(TextureLayerTest, PaintBeforePreollDies) {
  const SkPoint layer_offset = SkPoint::Make(0.0f, 0.0f);
  const SkSize layer_size = SkSize::Make(8.0f, 8.0f);
  const int64_t texture_id = 0;
  auto mock_texture = std::make_shared<MockTexture>(texture_id);
  auto layer = std::make_shared<TextureLayer>(
      layer_offset, layer_size, texture_id, false,
      SkSamplingOptions(SkFilterMode::kLinear));

  // Ensure the texture is located by the Layer.
  preroll_context()->texture_registry.RegisterTexture(mock_texture);

  EXPECT_DEATH_IF_SUPPORTED(layer->Paint(paint_context()),
                            "needs_painting\\(context\\)");
}
#endif

TEST_F(TextureLayerTest, PaintingWithLowFilterQuality) {
  const SkPoint layer_offset = SkPoint::Make(0.0f, 0.0f);
  const SkSize layer_size = SkSize::Make(8.0f, 8.0f);
  const int64_t texture_id = 0;
  auto mock_texture = std::make_shared<MockTexture>(texture_id);
  auto layer = std::make_shared<TextureLayer>(
      layer_offset, layer_size, texture_id, false,
      SkSamplingOptions(SkFilterMode::kLinear));

  // Ensure the texture is located by the Layer.
  preroll_context()->texture_registry.RegisterTexture(mock_texture);

  layer->Preroll(preroll_context(), SkMatrix());
  EXPECT_EQ(layer->paint_bounds(),
            (SkRect::MakeSize(layer_size)
                 .makeOffset(layer_offset.fX, layer_offset.fY)));
  EXPECT_TRUE(layer->needs_painting(paint_context()));

  layer->Paint(paint_context());
  EXPECT_EQ(mock_texture->paint_calls(),
            std::vector({MockTexture::PaintCall{
                mock_canvas(), layer->paint_bounds(), false, nullptr,
                SkSamplingOptions(SkFilterMode::kLinear)}}));
  EXPECT_EQ(mock_canvas().draw_calls(), std::vector<MockCanvas::DrawCall>());
}

}  // namespace testing
}  // namespace flutter
