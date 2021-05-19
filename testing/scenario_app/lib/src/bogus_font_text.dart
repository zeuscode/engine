// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6
import 'dart:ui';

import 'channel_util.dart';
import 'scenario.dart';

/// Tries to draw some text in a bogus font. Should end up drawing in the
/// system default font.
class BogusFontText extends Scenario {
  /// Creates the BogusFontText scenario.
  ///
  /// The [dispatcher] parameter must not be null.
  BogusFontText(PlatformDispatcher dispatcher)
      : assert(dispatcher != null),
        super(dispatcher);

  // Semi-arbitrary.
  double _screenWidth = 700;

  @override
  void onBeginFrame(Duration duration) {
    final SceneBuilder builder = SceneBuilder();
    final PictureRecorder recorder = PictureRecorder();
    final Canvas canvas = Canvas(recorder);

    final ParagraphBuilder paragraphBuilder =
        ParagraphBuilder(ParagraphStyle(fontFamily: "some font that doesn't exist"))
          ..pushStyle(TextStyle(fontSize: 80))
          ..addText('One more thing...')
          ..pop();
    final Paragraph paragraph = paragraphBuilder.build();

    paragraph.layout(ParagraphConstraints(width: _screenWidth));

    canvas.drawParagraph(paragraph, const Offset(50, 80));
    final Picture picture = recorder.endRecording();

    builder.addPicture(
      Offset.zero,
      picture,
      willChangeHint: true,
    );
    final Scene scene = builder.build();
    window.render(scene);
    scene.dispose();

    sendJsonMessage(
      dispatcher: dispatcher,
      channel: 'display_data',
      json: <String, dynamic>{
        'data': 'ready',
      },
    );
  }
}
