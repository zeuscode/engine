// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERVIEWCONTROLLER_INTERNAL_H_
#define FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERVIEWCONTROLLER_INTERNAL_H_

#include "flutter/fml/memory/weak_ptr.h"

#import "flutter/shell/platform/darwin/ios/framework/Headers/FlutterViewController.h"
#import "flutter/shell/platform/darwin/ios/framework/Source/FlutterRestorationPlugin.h"

namespace flutter {
class FlutterPlatformViewsController;
}

FLUTTER_DARWIN_EXPORT
extern NSNotificationName const FlutterViewControllerWillDealloc;

FLUTTER_DARWIN_EXPORT
extern NSNotificationName const FlutterViewControllerHideHomeIndicator;

FLUTTER_DARWIN_EXPORT
extern NSNotificationName const FlutterViewControllerShowHomeIndicator;

@interface FlutterViewController ()

@property(nonatomic, readonly) BOOL isPresentingViewController;
- (fml::WeakPtr<FlutterViewController>)getWeakPtr;
- (std::shared_ptr<flutter::FlutterPlatformViewsController>&)platformViewsController;
- (FlutterRestorationPlugin*)restorationPlugin;
// Send touches to the Flutter Engine while forcing the change type to be cancelled.
// The `phase`s in `touches` are ignored.
- (void)forceTouchesCancelled:(NSSet*)touches;

@end

#endif  // FLUTTER_SHELL_PLATFORM_DARWIN_IOS_FRAMEWORK_SOURCE_FLUTTERVIEWCONTROLLER_INTERNAL_H_
