// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6
import 'dart:math' as math;

import 'package:test/bootstrap/browser.dart';
import 'package:test/test.dart';
import 'package:ui/ui.dart' hide TextStyle;
import 'package:ui/src/engine.dart';
import 'screenshot.dart';

void main() {
  internalBootstrapBrowserTest(() => testMain);
}

void testMain() async {
  const double screenWidth = 500.0;
  const double screenHeight = 500.0;
  const Rect screenRect = Rect.fromLTWH(0, 0, screenWidth, screenHeight);

  setUp(() async {
    debugEmulateFlutterTesterEnvironment = true;
    await webOnlyInitializePlatform();
    webOnlyFontCollection.debugRegisterTestFonts();
    await webOnlyFontCollection.ensureFontsLoaded();
  });

  test('Should draw linear gradient using rectangle.', () async {
    final RecordingCanvas rc =
        RecordingCanvas(const Rect.fromLTRB(0, 0, 500, 500));
    Rect shaderRect = const Rect.fromLTRB(50, 50, 300, 300);
    final Paint paint = Paint()..shader = Gradient.linear(
        Offset(shaderRect.left, shaderRect.top),
        Offset(shaderRect.right, shaderRect.bottom),
        [Color(0xFFcfdfd2), Color(0xFF042a85)]);
    rc.drawRect(shaderRect, paint);
    expect(rc.renderStrategy.hasArbitraryPaint, isTrue);
    await canvasScreenshot(rc, 'linear_gradient_rect',
        region: screenRect,
        maxDiffRatePercent: 0.01);
  });

  test('Should draw linear gradient with transform.', () async {
    final RecordingCanvas rc =
        RecordingCanvas(const Rect.fromLTRB(0, 0, 500, 500));
    List<double> angles = [0.0, 90.0, 180.0];
    double yOffset = 0;
    for (double angle in angles) {
      final Rect shaderRect = Rect.fromLTWH(50, 50 + yOffset, 100, 100);
      Matrix4 matrix = Matrix4.identity();
      matrix.translate(shaderRect.left, shaderRect.top);
      matrix.multiply(Matrix4
          .rotationZ((angle / 180) * math.pi));
      Matrix4 post = Matrix4.identity();
      post.translate(-shaderRect.left, -shaderRect.top);
      matrix.multiply(post);
      final Paint paint = Paint()
        ..shader = Gradient.linear(
            Offset(shaderRect.left, shaderRect.top),
            Offset(shaderRect.right, shaderRect.bottom),
            [Color(0xFFFF0000), Color(0xFF042a85)],
            null,
            TileMode.clamp,
            matrix.toFloat64());
      rc.drawRect(shaderRect, Paint()
        ..color = Color(0xFF000000));
      rc.drawOval(shaderRect, paint);
      yOffset += 120;
    }
    expect(rc.renderStrategy.hasArbitraryPaint, isTrue);
    await canvasScreenshot(rc, 'linear_gradient_oval_matrix',
        region: screenRect,
        maxDiffRatePercent: 0.2);
  });

  // Regression test for https://github.com/flutter/flutter/issues/50010
  test('Should draw linear gradient using rounded rect.', () async {
    final RecordingCanvas rc =
        RecordingCanvas(const Rect.fromLTRB(0, 0, 500, 500));
    Rect shaderRect = const Rect.fromLTRB(50, 50, 300, 300);
    final Paint paint = Paint()..shader = Gradient.linear(
        Offset(shaderRect.left, shaderRect.top),
        Offset(shaderRect.right, shaderRect.bottom),
        [Color(0xFFcfdfd2), Color(0xFF042a85)]);
    rc.drawRRect(RRect.fromRectAndRadius(shaderRect, Radius.circular(16)), paint);
    expect(rc.renderStrategy.hasArbitraryPaint, isTrue);
    await canvasScreenshot(rc, 'linear_gradient_rounded_rect',
        region: screenRect,
        maxDiffRatePercent: 0.1);
  });

  test('Should draw tiled repeated linear gradient with transform.', () async {
    final RecordingCanvas rc =
    RecordingCanvas(const Rect.fromLTRB(0, 0, 500, 500));
    List<double> angles = [0.0, 30.0, 210.0];
    double yOffset = 0;
    for (double angle in angles) {
      final Rect shaderRect = Rect.fromLTWH(50, 50 + yOffset, 100, 100);
      final Paint paint = Paint()
        ..shader = Gradient.linear(
            Offset(shaderRect.left, shaderRect.top),
            Offset(shaderRect.left + shaderRect.width / 2, shaderRect.top),
            [Color(0xFFFF0000), Color(0xFF042a85)],
            null,
            TileMode.repeated,
            Matrix4
                .rotationZ((angle / 180) * math.pi)
                .toFloat64());
      rc.drawRect(shaderRect, Paint()
        ..color = Color(0xFF000000));
      rc.drawOval(shaderRect, paint);
      yOffset += 120;
    }
    expect(rc.renderStrategy.hasArbitraryPaint, isTrue);
    await canvasScreenshot(rc, 'linear_gradient_tiled_repeated_rect',
        region: screenRect);
  });

  test('Should draw tiled mirrored linear gradient with transform.', () async {
    final RecordingCanvas rc =
    RecordingCanvas(const Rect.fromLTRB(0, 0, 500, 500));
    List<double> angles = [0.0, 30.0, 210.0];
    double yOffset = 0;
    for (double angle in angles) {
      final Rect shaderRect = Rect.fromLTWH(50, 50 + yOffset, 100, 100);
      final Paint paint = Paint()
        ..shader = Gradient.linear(
            Offset(shaderRect.left, shaderRect.top),
            Offset(shaderRect.left + shaderRect.width / 2, shaderRect.top),
            [Color(0xFFFF0000), Color(0xFF042a85)],
            null,
            TileMode.mirror,
            Matrix4
                .rotationZ((angle / 180) * math.pi)
                .toFloat64());
      rc.drawRect(shaderRect, Paint()
        ..color = Color(0xFF000000));
      rc.drawOval(shaderRect, paint);
      yOffset += 120;
    }
    expect(rc.renderStrategy.hasArbitraryPaint, isTrue);
    await canvasScreenshot(rc, 'linear_gradient_tiled_mirrored_rect',
        region: screenRect);
  });
}
