// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6
import 'dart:typed_data';
import 'dart:ui';

/// A scenario to run for testing.
abstract class Scenario {
  /// Creates a new scenario using a specific FlutterWindow instance.
  Scenario(this.dispatcher);

  /// The window used by this scenario. May be mocked.
  final PlatformDispatcher dispatcher;

  /// [true] if a screenshot is taken in the next frame.
  bool _didScheduleScreenshot = false;

  /// Called by the program when a frame is ready to be drawn.
  ///
  /// See [PlatformDispatcher.onBeginFrame] for more details.
  void onBeginFrame(Duration duration) {}

  /// Called by the program when the microtasks from [onBeginFrame] have been
  /// flushed.
  ///
  /// See [PlatformDispatcher.onDrawFrame] for more details.
  void onDrawFrame() {
    Future<void>.delayed(const Duration(seconds: 1), () {
      if (_didScheduleScreenshot) {
        dispatcher.sendPlatformMessage('take_screenshot', null, null);
      } else {
        _didScheduleScreenshot = true;
        dispatcher.scheduleFrame();
      }
    });
  }

  /// Called when the current scenario has been unmount due to a
  /// new scenario being mount.
  void unmount() {
    _didScheduleScreenshot = false;
  }

  /// Called by the program when the window metrics have changed.
  ///
  /// See [PlatformDispatcher.onMetricsChanged].
  void onMetricsChanged() {}

  /// Called by the program when a pointer event is received.
  ///
  /// See [PlatformDispatcher.onPointerDataPacket].
  void onPointerDataPacket(PointerDataPacket packet) {}

  /// Called by the program when an engine side platform channel message is
  /// received.
  ///
  /// See [PlatformDispatcher.onPlatformMessage].
  void onPlatformMessage(
    String name,
    ByteData data,
    PlatformMessageResponseCallback callback,
  ) {}
}
