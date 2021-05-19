// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/flow/layers/transform_layer.h"

#include "flutter/flow/testing/diff_context_test.h"
#include "flutter/flow/testing/layer_test.h"
#include "flutter/flow/testing/mock_layer.h"
#include "flutter/fml/macros.h"
#include "flutter/testing/mock_canvas.h"

namespace flutter {
namespace testing {

using TransformLayerTest = LayerTest;

#ifndef NDEBUG
TEST_F(TransformLayerTest, PaintingEmptyLayerDies) {
  auto layer = std::make_shared<TransformLayer>(SkMatrix());  // identity

  layer->Preroll(preroll_context(), SkMatrix());
  EXPECT_EQ(layer->paint_bounds(), SkRect::MakeEmpty());
  EXPECT_FALSE(layer->needs_painting(paint_context()));

  EXPECT_DEATH_IF_SUPPORTED(layer->Paint(paint_context()),
                            "needs_painting\\(context\\)");
}

TEST_F(TransformLayerTest, PaintBeforePreollDies) {
  SkPath child_path;
  child_path.addRect(5.0f, 6.0f, 20.5f, 21.5f);
  auto mock_layer = std::make_shared<MockLayer>(child_path, SkPaint());
  auto layer = std::make_shared<TransformLayer>(SkMatrix());  // identity
  layer->Add(mock_layer);

  EXPECT_DEATH_IF_SUPPORTED(layer->Paint(paint_context()),
                            "needs_painting\\(context\\)");
}
#endif

TEST_F(TransformLayerTest, Identity) {
  SkPath child_path;
  child_path.addRect(5.0f, 6.0f, 20.5f, 21.5f);
  SkRect cull_rect = SkRect::MakeXYWH(2.0f, 2.0f, 14.0f, 14.0f);
  auto mock_layer = std::make_shared<MockLayer>(child_path, SkPaint());
  auto layer = std::make_shared<TransformLayer>(SkMatrix());  // identity
  layer->Add(mock_layer);

  preroll_context()->cull_rect = cull_rect;
  layer->Preroll(preroll_context(), SkMatrix());
  EXPECT_EQ(mock_layer->paint_bounds(), child_path.getBounds());
  EXPECT_EQ(layer->paint_bounds(), mock_layer->paint_bounds());
  EXPECT_TRUE(mock_layer->needs_painting(paint_context()));
  EXPECT_TRUE(layer->needs_painting(paint_context()));
  EXPECT_EQ(mock_layer->parent_matrix(), SkMatrix());  // identity
  EXPECT_EQ(mock_layer->parent_cull_rect(), cull_rect);
  EXPECT_EQ(mock_layer->parent_mutators(), std::vector({Mutator(SkMatrix())}));

  layer->Paint(paint_context());
  EXPECT_EQ(mock_canvas().draw_calls(),
            std::vector({MockCanvas::DrawCall{
                0, MockCanvas::DrawPathData{child_path, SkPaint()}}}));
}

TEST_F(TransformLayerTest, Simple) {
  SkPath child_path;
  child_path.addRect(5.0f, 6.0f, 20.5f, 21.5f);
  SkRect cull_rect = SkRect::MakeXYWH(2.0f, 2.0f, 14.0f, 14.0f);
  SkMatrix initial_transform = SkMatrix::Translate(-0.5f, -0.5f);
  SkMatrix layer_transform = SkMatrix::Translate(2.5f, 2.5f);
  SkMatrix inverse_layer_transform;
  EXPECT_TRUE(layer_transform.invert(&inverse_layer_transform));

  auto mock_layer = std::make_shared<MockLayer>(child_path, SkPaint());
  auto layer = std::make_shared<TransformLayer>(layer_transform);
  layer->Add(mock_layer);

  preroll_context()->cull_rect = cull_rect;
  layer->Preroll(preroll_context(), initial_transform);
  EXPECT_EQ(mock_layer->paint_bounds(), child_path.getBounds());
  EXPECT_EQ(layer->paint_bounds(),
            layer_transform.mapRect(mock_layer->paint_bounds()));
  EXPECT_TRUE(mock_layer->needs_painting(paint_context()));
  EXPECT_TRUE(layer->needs_painting(paint_context()));
  EXPECT_EQ(mock_layer->parent_matrix(),
            SkMatrix::Concat(initial_transform, layer_transform));
  EXPECT_EQ(mock_layer->parent_cull_rect(),
            inverse_layer_transform.mapRect(cull_rect));
  EXPECT_EQ(mock_layer->parent_mutators(),
            std::vector({Mutator(layer_transform)}));

  layer->Paint(paint_context());
  EXPECT_EQ(
      mock_canvas().draw_calls(),
      std::vector({MockCanvas::DrawCall{0, MockCanvas::SaveData{1}},
                   MockCanvas::DrawCall{
                       1, MockCanvas::ConcatMatrixData{SkM44(layer_transform)}},
                   MockCanvas::DrawCall{
                       1, MockCanvas::DrawPathData{child_path, SkPaint()}},
                   MockCanvas::DrawCall{1, MockCanvas::RestoreData{0}}}));
}

TEST_F(TransformLayerTest, Nested) {
  SkPath child_path;
  child_path.addRect(5.0f, 6.0f, 20.5f, 21.5f);
  SkRect cull_rect = SkRect::MakeXYWH(2.0f, 2.0f, 14.0f, 14.0f);
  SkMatrix initial_transform = SkMatrix::Translate(-0.5f, -0.5f);
  SkMatrix layer1_transform = SkMatrix::Translate(2.5f, 2.5f);
  SkMatrix layer2_transform = SkMatrix::Translate(2.5f, 2.5f);
  SkMatrix inverse_layer1_transform, inverse_layer2_transform;
  EXPECT_TRUE(layer1_transform.invert(&inverse_layer1_transform));
  EXPECT_TRUE(layer2_transform.invert(&inverse_layer2_transform));

  auto mock_layer = std::make_shared<MockLayer>(child_path, SkPaint());
  auto layer1 = std::make_shared<TransformLayer>(layer1_transform);
  auto layer2 = std::make_shared<TransformLayer>(layer2_transform);
  layer1->Add(layer2);
  layer2->Add(mock_layer);

  preroll_context()->cull_rect = cull_rect;
  layer1->Preroll(preroll_context(), initial_transform);
  EXPECT_EQ(mock_layer->paint_bounds(), child_path.getBounds());
  EXPECT_EQ(layer2->paint_bounds(),
            layer2_transform.mapRect(mock_layer->paint_bounds()));
  EXPECT_EQ(layer1->paint_bounds(),
            layer1_transform.mapRect(layer2->paint_bounds()));
  EXPECT_TRUE(mock_layer->needs_painting(paint_context()));
  EXPECT_TRUE(layer2->needs_painting(paint_context()));
  EXPECT_TRUE(layer1->needs_painting(paint_context()));
  EXPECT_EQ(
      mock_layer->parent_matrix(),
      SkMatrix::Concat(SkMatrix::Concat(initial_transform, layer1_transform),
                       layer2_transform));
  EXPECT_EQ(mock_layer->parent_cull_rect(),
            inverse_layer2_transform.mapRect(
                inverse_layer1_transform.mapRect(cull_rect)));
  EXPECT_EQ(
      mock_layer->parent_mutators(),
      std::vector({Mutator(layer2_transform), Mutator(layer1_transform)}));

  layer1->Paint(paint_context());
  EXPECT_EQ(mock_canvas().draw_calls(),
            std::vector(
                {MockCanvas::DrawCall{0, MockCanvas::SaveData{1}},
                 MockCanvas::DrawCall{
                     1, MockCanvas::ConcatMatrixData{SkM44(layer1_transform)}},
                 MockCanvas::DrawCall{1, MockCanvas::SaveData{2}},
                 MockCanvas::DrawCall{
                     2, MockCanvas::ConcatMatrixData{SkM44(layer2_transform)}},
                 MockCanvas::DrawCall{
                     2, MockCanvas::DrawPathData{child_path, SkPaint()}},
                 MockCanvas::DrawCall{2, MockCanvas::RestoreData{1}},
                 MockCanvas::DrawCall{1, MockCanvas::RestoreData{0}}}));
}

TEST_F(TransformLayerTest, NestedSeparated) {
  SkPath child_path;
  child_path.addRect(5.0f, 6.0f, 20.5f, 21.5f);
  SkRect cull_rect = SkRect::MakeXYWH(2.0f, 2.0f, 14.0f, 14.0f);
  SkMatrix initial_transform = SkMatrix::Translate(-0.5f, -0.5f);
  SkMatrix layer1_transform = SkMatrix::Translate(2.5f, 2.5f);
  SkMatrix layer2_transform = SkMatrix::Translate(2.5f, 2.5f);
  SkMatrix inverse_layer1_transform, inverse_layer2_transform;
  EXPECT_TRUE(layer1_transform.invert(&inverse_layer1_transform));
  EXPECT_TRUE(layer2_transform.invert(&inverse_layer2_transform));

  auto mock_layer1 =
      std::make_shared<MockLayer>(child_path, SkPaint(SkColors::kBlue));
  auto mock_layer2 =
      std::make_shared<MockLayer>(child_path, SkPaint(SkColors::kGreen));
  auto layer1 = std::make_shared<TransformLayer>(layer1_transform);
  auto layer2 = std::make_shared<TransformLayer>(layer2_transform);
  layer1->Add(mock_layer1);
  layer1->Add(layer2);
  layer2->Add(mock_layer2);

  preroll_context()->cull_rect = cull_rect;
  layer1->Preroll(preroll_context(), initial_transform);
  SkRect expected_layer1_bounds = layer2->paint_bounds();
  expected_layer1_bounds.join(mock_layer1->paint_bounds());
  layer1_transform.mapRect(&expected_layer1_bounds);
  EXPECT_EQ(mock_layer2->paint_bounds(), child_path.getBounds());
  EXPECT_EQ(layer2->paint_bounds(),
            layer2_transform.mapRect(mock_layer2->paint_bounds()));
  EXPECT_EQ(mock_layer1->paint_bounds(), child_path.getBounds());
  EXPECT_EQ(layer1->paint_bounds(), expected_layer1_bounds);
  EXPECT_TRUE(mock_layer2->needs_painting(paint_context()));
  EXPECT_TRUE(layer2->needs_painting(paint_context()));
  EXPECT_TRUE(mock_layer1->needs_painting(paint_context()));
  EXPECT_TRUE(layer1->needs_painting(paint_context()));
  EXPECT_EQ(mock_layer1->parent_matrix(),
            SkMatrix::Concat(initial_transform, layer1_transform));
  EXPECT_EQ(
      mock_layer2->parent_matrix(),
      SkMatrix::Concat(SkMatrix::Concat(initial_transform, layer1_transform),
                       layer2_transform));
  EXPECT_EQ(mock_layer1->parent_cull_rect(),
            inverse_layer1_transform.mapRect(cull_rect));
  EXPECT_EQ(mock_layer2->parent_cull_rect(),
            inverse_layer2_transform.mapRect(
                inverse_layer1_transform.mapRect(cull_rect)));
  EXPECT_EQ(mock_layer1->parent_mutators(),
            std::vector({Mutator(layer1_transform)}));
  EXPECT_EQ(
      mock_layer2->parent_mutators(),
      std::vector({Mutator(layer2_transform), Mutator(layer1_transform)}));

  layer1->Paint(paint_context());
  EXPECT_EQ(mock_canvas().draw_calls(),
            std::vector(
                {MockCanvas::DrawCall{0, MockCanvas::SaveData{1}},
                 MockCanvas::DrawCall{
                     1, MockCanvas::ConcatMatrixData{SkM44(layer1_transform)}},
                 MockCanvas::DrawCall{
                     1, MockCanvas::DrawPathData{child_path,
                                                 SkPaint(SkColors::kBlue)}},
                 MockCanvas::DrawCall{1, MockCanvas::SaveData{2}},
                 MockCanvas::DrawCall{
                     2, MockCanvas::ConcatMatrixData{SkM44(layer2_transform)}},
                 MockCanvas::DrawCall{
                     2, MockCanvas::DrawPathData{child_path,
                                                 SkPaint(SkColors::kGreen)}},
                 MockCanvas::DrawCall{2, MockCanvas::RestoreData{1}},
                 MockCanvas::DrawCall{1, MockCanvas::RestoreData{0}}}));
}

#ifdef FLUTTER_ENABLE_DIFF_CONTEXT

using TransformLayerLayerDiffTest = DiffContextTest;

TEST_F(TransformLayerLayerDiffTest, Transform) {
  auto path1 = SkPath().addRect(SkRect::MakeLTRB(0, 0, 50, 50));
  auto m1 = std::make_shared<MockLayer>(path1);

  auto transform1 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(10, 10));
  transform1->Add(m1);

  MockLayerTree t1;
  t1.root()->Add(transform1);

  auto damage = DiffLayerTree(t1, MockLayerTree());
  EXPECT_EQ(damage.frame_damage, SkIRect::MakeLTRB(10, 10, 60, 60));

  auto transform2 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(20, 20));
  transform2->Add(m1);
  transform2->AssignOldLayer(transform1.get());

  MockLayerTree t2;
  t2.root()->Add(transform2);

  damage = DiffLayerTree(t2, t1);
  EXPECT_EQ(damage.frame_damage, SkIRect::MakeLTRB(10, 10, 70, 70));

  auto transform3 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(20, 20));
  transform3->Add(m1);
  transform3->AssignOldLayer(transform2.get());

  MockLayerTree t3;
  t3.root()->Add(transform3);

  damage = DiffLayerTree(t3, t2);
  EXPECT_EQ(damage.frame_damage, SkIRect::MakeEmpty());
}

TEST_F(TransformLayerLayerDiffTest, TransformNested) {
  auto path1 = SkPath().addRect(SkRect::MakeLTRB(0, 0, 50, 50));
  auto m1 = CreateContainerLayer(std::make_shared<MockLayer>(path1));
  auto m2 = CreateContainerLayer(std::make_shared<MockLayer>(path1));
  auto m3 = CreateContainerLayer(std::make_shared<MockLayer>(path1));

  auto transform1 = std::make_shared<TransformLayer>(SkMatrix::Scale(2.0, 2.0));

  auto transform1_1 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(10, 10));
  transform1_1->Add(m1);
  transform1->Add(transform1_1);

  auto transform1_2 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(100, 100));
  transform1_2->Add(m2);
  transform1->Add(transform1_2);

  auto transform1_3 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(200, 200));
  transform1_3->Add(m3);
  transform1->Add(transform1_3);

  MockLayerTree l1;
  l1.root()->Add(transform1);

  auto damage = DiffLayerTree(l1, MockLayerTree());
  EXPECT_EQ(damage.frame_damage, SkIRect::MakeLTRB(20, 20, 500, 500));

  auto transform2 = std::make_shared<TransformLayer>(SkMatrix::Scale(2.0, 2.0));

  auto transform2_1 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(10, 10));
  transform2_1->Add(m1);
  transform2_1->AssignOldLayer(transform1_1.get());
  transform2->Add(transform2_1);

  // Offset 1px from transform1_2 so that they're not the same
  auto transform2_2 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(100, 101));
  transform2_2->Add(m2);
  transform2_2->AssignOldLayer(transform1_2.get());
  transform2->Add(transform2_2);

  auto transform2_3 =
      std::make_shared<TransformLayer>(SkMatrix::Translate(200, 200));
  transform2_3->Add(m3);
  transform2_3->AssignOldLayer(transform1_3.get());
  transform2->Add(transform2_3);

  MockLayerTree l2;
  l2.root()->Add(transform2);

  damage = DiffLayerTree(l2, l1);

  // transform2 has not transform1 assigned as old layer, so it should be
  // invalidated completely
  EXPECT_EQ(damage.frame_damage, SkIRect::MakeLTRB(20, 20, 500, 500));

  // now diff the tree properly, the only difference being transform2_2 and
  // transform_2_1
  transform2->AssignOldLayer(transform1.get());
  damage = DiffLayerTree(l2, l1);

  EXPECT_EQ(damage.frame_damage, SkIRect::MakeLTRB(200, 200, 300, 302));
}

#endif

}  // namespace testing
}  // namespace flutter
