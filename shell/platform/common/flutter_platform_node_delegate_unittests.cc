// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter_platform_node_delegate.h"

#include "flutter/third_party/accessibility/ax/ax_action_data.h"
#include "gtest/gtest.h"

#include "test_accessibility_bridge.h"

namespace flutter {
namespace testing {

TEST(FlutterPlatformNodeDelegateTest, canPerfomActions) {
  TestAccessibilityBridgeDelegate* delegate =
      new TestAccessibilityBridgeDelegate();
  std::unique_ptr<TestAccessibilityBridgeDelegate> ptr(delegate);
  std::shared_ptr<AccessibilityBridge> bridge =
      std::make_shared<AccessibilityBridge>(std::move(ptr));
  FlutterSemanticsNode root;
  root.id = 0;
  root.flags = FlutterSemanticsFlag::kFlutterSemanticsFlagIsTextField;
  root.actions = static_cast<FlutterSemanticsAction>(0);
  root.text_selection_base = -1;
  root.text_selection_extent = -1;
  root.label = "root";
  root.hint = "";
  root.value = "";
  root.increased_value = "";
  root.decreased_value = "";
  root.child_count = 0;
  root.custom_accessibility_actions_count = 0;
  bridge->AddFlutterSemanticsNodeUpdate(&root);

  bridge->CommitUpdates();

  auto accessibility = bridge->GetFlutterPlatformNodeDelegateFromID(0).lock();
  // Performs an AXAction.
  ui::AXActionData action_data;
  action_data.action = ax::mojom::Action::kDoDefault;
  accessibility->AccessibilityPerformAction(action_data);
  EXPECT_EQ(delegate->performed_actions.size(), size_t{1});
  EXPECT_EQ(delegate->performed_actions[0],
            FlutterSemanticsAction::kFlutterSemanticsActionTap);

  action_data.action = ax::mojom::Action::kFocus;
  accessibility->AccessibilityPerformAction(action_data);
  EXPECT_EQ(delegate->performed_actions.size(), size_t{2});
  EXPECT_EQ(
      delegate->performed_actions[1],
      FlutterSemanticsAction::kFlutterSemanticsActionDidGainAccessibilityFocus);

  action_data.action = ax::mojom::Action::kScrollToMakeVisible;
  accessibility->AccessibilityPerformAction(action_data);
  EXPECT_EQ(delegate->performed_actions.size(), size_t{3});
  EXPECT_EQ(delegate->performed_actions[2],
            FlutterSemanticsAction::kFlutterSemanticsActionShowOnScreen);
}

TEST(FlutterPlatformNodeDelegateTest, canGetAXNode) {
  // Set up a flutter accessibility node.
  std::shared_ptr<AccessibilityBridge> bridge =
      std::make_shared<AccessibilityBridge>(
          std::make_unique<TestAccessibilityBridgeDelegate>());
  FlutterSemanticsNode root;
  root.id = 0;
  root.flags = FlutterSemanticsFlag::kFlutterSemanticsFlagIsTextField;
  root.actions = static_cast<FlutterSemanticsAction>(0);
  root.text_selection_base = -1;
  root.text_selection_extent = -1;
  root.label = "root";
  root.hint = "";
  root.value = "";
  root.increased_value = "";
  root.decreased_value = "";
  root.child_count = 0;
  root.custom_accessibility_actions_count = 0;
  bridge->AddFlutterSemanticsNodeUpdate(&root);

  bridge->CommitUpdates();

  auto accessibility = bridge->GetFlutterPlatformNodeDelegateFromID(0).lock();
  EXPECT_EQ(accessibility->GetData().id, 0);
}

TEST(FlutterPlatformNodeDelegateTest, canCalculateBoundsCorrectly) {
  std::shared_ptr<AccessibilityBridge> bridge =
      std::make_shared<AccessibilityBridge>(
          std::make_unique<TestAccessibilityBridgeDelegate>());
  FlutterSemanticsNode root;
  root.id = 0;
  root.label = "root";
  root.hint = "";
  root.value = "";
  root.increased_value = "";
  root.decreased_value = "";
  root.child_count = 1;
  int32_t children[] = {1};
  root.children_in_traversal_order = children;
  root.custom_accessibility_actions_count = 0;
  root.rect = {0, 0, 100, 100};  // LTRB
  root.transform = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  bridge->AddFlutterSemanticsNodeUpdate(&root);

  FlutterSemanticsNode child1;
  child1.id = 1;
  child1.label = "child 1";
  child1.hint = "";
  child1.value = "";
  child1.increased_value = "";
  child1.decreased_value = "";
  child1.child_count = 0;
  child1.custom_accessibility_actions_count = 0;
  child1.rect = {0, 0, 50, 50};  // LTRB
  child1.transform = {0.5, 0, 0, 0, 0.5, 0, 0, 0, 1};
  bridge->AddFlutterSemanticsNodeUpdate(&child1);

  bridge->CommitUpdates();
  auto child1_node = bridge->GetFlutterPlatformNodeDelegateFromID(1).lock();
  ui::AXOffscreenResult result;
  gfx::Rect bounds =
      child1_node->GetBoundsRect(ui::AXCoordinateSystem::kScreenDIPs,
                                 ui::AXClippingBehavior::kClipped, &result);
  EXPECT_EQ(bounds.x(), 0);
  EXPECT_EQ(bounds.y(), 0);
  EXPECT_EQ(bounds.width(), 25);
  EXPECT_EQ(bounds.height(), 25);
  EXPECT_EQ(result, ui::AXOffscreenResult::kOnscreen);
}

TEST(FlutterPlatformNodeDelegateTest, canCalculateOffScreenBoundsCorrectly) {
  std::shared_ptr<AccessibilityBridge> bridge =
      std::make_shared<AccessibilityBridge>(
          std::make_unique<TestAccessibilityBridgeDelegate>());
  FlutterSemanticsNode root;
  root.id = 0;
  root.label = "root";
  root.hint = "";
  root.value = "";
  root.increased_value = "";
  root.decreased_value = "";
  root.child_count = 1;
  int32_t children[] = {1};
  root.children_in_traversal_order = children;
  root.custom_accessibility_actions_count = 0;
  root.rect = {0, 0, 100, 100};  // LTRB
  root.transform = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  bridge->AddFlutterSemanticsNodeUpdate(&root);

  FlutterSemanticsNode child1;
  child1.id = 1;
  child1.label = "child 1";
  child1.hint = "";
  child1.value = "";
  child1.increased_value = "";
  child1.decreased_value = "";
  child1.child_count = 0;
  child1.custom_accessibility_actions_count = 0;
  child1.rect = {90, 90, 100, 100};  // LTRB
  child1.transform = {2, 0, 0, 0, 2, 0, 0, 0, 1};
  bridge->AddFlutterSemanticsNodeUpdate(&child1);

  bridge->CommitUpdates();
  auto child1_node = bridge->GetFlutterPlatformNodeDelegateFromID(1).lock();
  ui::AXOffscreenResult result;
  gfx::Rect bounds =
      child1_node->GetBoundsRect(ui::AXCoordinateSystem::kScreenDIPs,
                                 ui::AXClippingBehavior::kUnclipped, &result);
  EXPECT_EQ(bounds.x(), 180);
  EXPECT_EQ(bounds.y(), 180);
  EXPECT_EQ(bounds.width(), 20);
  EXPECT_EQ(bounds.height(), 20);
  EXPECT_EQ(result, ui::AXOffscreenResult::kOffscreen);
}

}  // namespace testing
}  // namespace flutter
