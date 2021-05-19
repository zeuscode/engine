// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:ui/ui.dart' as ui;
import 'package:ui/src/engine.dart';

import 'package:test/test.dart';
import 'package:test/bootstrap/browser.dart';
import 'package:web_engine_tester/golden_tester.dart';

import 'common.dart';

void main() {
  internalBootstrapBrowserTest(() => testMain);
}

const ui.Rect kDefaultRegion = const ui.Rect.fromLTRB(0, 0, 100, 50);

Future<void> matchPictureGolden(String goldenFile, CkPicture picture,
    {ui.Rect region = kDefaultRegion, bool write = false}) async {
  final EnginePlatformDispatcher dispatcher =
      ui.window.platformDispatcher as EnginePlatformDispatcher;
  final LayerSceneBuilder sb = LayerSceneBuilder();
  sb.pushOffset(0, 0);
  sb.addPicture(ui.Offset.zero, picture);
  dispatcher.rasterizer!.draw(sb.build().layerTree);
  await matchGoldenFile(goldenFile,
      region: region, maxDiffRatePercent: 0.0, write: write);
}

void testMain() {
  group('Font fallbacks', () {
    setUpCanvasKitTest();

    /// Used to save and restore [ui.window.onPlatformMessage] after each test.
    ui.PlatformMessageCallback? savedCallback;

    setUp(() {
      notoDownloadQueue.downloader = TestDownloader();
      TestDownloader.mockDownloads.clear();
      savedCallback = ui.window.onPlatformMessage;
      FontFallbackData.debugReset();
    });

    tearDown(() {
      ui.window.onPlatformMessage = savedCallback;
    });

    test('Roboto is always a fallback font', () {
      expect(FontFallbackData.instance.globalFontFallbacks, contains('Roboto'));
    });

    test('will download Noto Naskh Arabic if Arabic text is added', () async {
      final Completer<void> fontChangeCompleter = Completer<void>();
      // Intercept the system font change message.
      ui.window.onPlatformMessage = (String name, ByteData? data,
          ui.PlatformMessageResponseCallback? callback) {
        if (name == 'flutter/system') {
          const JSONMessageCodec codec = JSONMessageCodec();
          final dynamic message = codec.decodeMessage(data);
          if (message is Map) {
            if (message['type'] == 'fontsChange') {
              fontChangeCompleter.complete();
            }
          }
        }
        if (savedCallback != null) {
          savedCallback!(name, data, callback);
        }
      };

      TestDownloader.mockDownloads[
              'https://fonts.googleapis.com/css2?family=Noto+Naskh+Arabic+UI'] =
          '''
/* arabic */
@font-face {
  font-family: 'Noto Naskh Arabic UI';
  font-style: normal;
  font-weight: 400;
  src: url(packages/ui/assets/NotoNaskhArabic-Regular.ttf) format('ttf');
  unicode-range: U+0600-06FF, U+200C-200E, U+2010-2011, U+204F, U+2E41, U+FB50-FDFF, U+FE80-FEFC;
}
''';

      expect(FontFallbackData.instance.globalFontFallbacks, ['Roboto']);

      // Creating this paragraph should cause us to start to download the
      // fallback font.
      CkParagraphBuilder pb = CkParagraphBuilder(
        CkParagraphStyle(),
      );
      pb.addText('مرحبا');

      await fontChangeCompleter.future;

      expect(FontFallbackData.instance.globalFontFallbacks,
          contains('Noto Naskh Arabic UI 0'));

      final CkPictureRecorder recorder = CkPictureRecorder();
      final CkCanvas canvas = recorder.beginRecording(kDefaultRegion);

      pb = CkParagraphBuilder(
        CkParagraphStyle(),
      );
      pb.pushStyle(ui.TextStyle(fontSize: 32));
      pb.addText('مرحبا');
      pb.pop();
      final CkParagraph paragraph = pb.build();
      paragraph.layout(ui.ParagraphConstraints(width: 1000));

      canvas.drawParagraph(paragraph, ui.Offset(0, 0));

      await matchPictureGolden(
          'canvaskit_font_fallback_arabic.png', recorder.endRecording());
      // TODO: https://github.com/flutter/flutter/issues/60040
      // TODO: https://github.com/flutter/flutter/issues/71520
    }, skip: isIosSafari || isFirefox);

    test('will download Noto Emojis and Noto Symbols if no matching Noto Font',
        () async {
      final Completer<void> fontChangeCompleter = Completer<void>();
      // Intercept the system font change message.
      ui.window.onPlatformMessage = (String name, ByteData? data,
          ui.PlatformMessageResponseCallback? callback) {
        if (name == 'flutter/system') {
          const JSONMessageCodec codec = JSONMessageCodec();
          final dynamic message = codec.decodeMessage(data);
          if (message is Map) {
            if (message['type'] == 'fontsChange') {
              fontChangeCompleter.complete();
            }
          }
        }
        if (savedCallback != null) {
          savedCallback!(name, data, callback);
        }
      };

      TestDownloader.mockDownloads[
              'https://fonts.googleapis.com/css2?family=Noto+Color+Emoji+Compat'] =
          '''
/* arabic */
@font-face {
  font-family: 'Noto Color Emoji';
  src: url(packages/ui/assets/NotoColorEmoji.ttf) format('ttf');
}
''';

      expect(FontFallbackData.instance.globalFontFallbacks, ['Roboto']);

      // Creating this paragraph should cause us to start to download the
      // fallback font.
      CkParagraphBuilder pb = CkParagraphBuilder(
        CkParagraphStyle(),
      );
      pb.addText('Hello 😊');

      await fontChangeCompleter.future;

      expect(FontFallbackData.instance.globalFontFallbacks,
          contains('Noto Color Emoji Compat 0'));

      final CkPictureRecorder recorder = CkPictureRecorder();
      final CkCanvas canvas = recorder.beginRecording(kDefaultRegion);

      pb = CkParagraphBuilder(
        CkParagraphStyle(),
      );
      pb.pushStyle(ui.TextStyle(fontSize: 26));
      pb.addText('Hello 😊');
      pb.pop();
      final CkParagraph paragraph = pb.build();
      paragraph.layout(ui.ParagraphConstraints(width: 1000));

      canvas.drawParagraph(paragraph, ui.Offset(0, 0));

      await matchPictureGolden(
          'canvaskit_font_fallback_emoji.png', recorder.endRecording());
      // TODO: https://github.com/flutter/flutter/issues/60040
      // TODO: https://github.com/flutter/flutter/issues/71520
    }, skip: isIosSafari || isFirefox);

    test('will gracefully fail if we cannot parse the Google Fonts CSS',
        () async {
      TestDownloader.mockDownloads[
              'https://fonts.googleapis.com/css2?family=Noto+Naskh+Arabic+UI'] =
          'invalid CSS... this should cause our parser to fail';

      expect(FontFallbackData.instance.globalFontFallbacks, ['Roboto']);

      // Creating this paragraph should cause us to start to download the
      // fallback font.
      CkParagraphBuilder pb = CkParagraphBuilder(
        CkParagraphStyle(),
      );
      pb.addText('مرحبا');

      // Flush microtasks and test that we didn't start any downloads.
      await Future<void>.delayed(Duration.zero);

      expect(notoDownloadQueue.isPending, isFalse);
      expect(FontFallbackData.instance.globalFontFallbacks, ['Roboto']);
    });

    // Regression test for https://github.com/flutter/flutter/issues/75836
    // When we had this bug our font fallback resolution logic would end up in an
    // infinite loop and this test would freeze and time out.
    test('Can find fonts for two adjacent unmatched code units from different fonts', () async {
      final LoggingDownloader loggingDownloader = LoggingDownloader(NotoDownloader());
      notoDownloadQueue.downloader = loggingDownloader;
      // Try rendering text that requires fallback fonts, initially before the fonts are loaded.

      CkParagraphBuilder(CkParagraphStyle()).addText('ヽಠ');
      await notoDownloadQueue.downloader.debugWhenIdle();
      expect(
        loggingDownloader.log,
        <String>[
          'https://fonts.googleapis.com/css2?family=Noto+Sans+SC',
          'https://fonts.googleapis.com/css2?family=Noto+Sans+JP',
          'https://fonts.googleapis.com/css2?family=Noto+Sans+Kannada+UI',
          'Noto Sans SC',
          'Noto Sans JP',
          'Noto Sans Kannada UI',
        ],
      );

      // Do the same thing but this time with loaded fonts.
      loggingDownloader.log.clear();
      CkParagraphBuilder(CkParagraphStyle()).addText('ヽಠ');
      await notoDownloadQueue.downloader.debugWhenIdle();
      expect(loggingDownloader.log, isEmpty);
    });

    test('findMinimumFontsForCodeunits for all supported code units', () async {
      final LoggingDownloader loggingDownloader = LoggingDownloader(NotoDownloader());
      notoDownloadQueue.downloader = loggingDownloader;

      // Collect all supported code units from all fallback fonts in the Noto
      // font tree.
      final Set<String> testedFonts = <String>{};
      final Set<int> supportedUniqueCodeUnits = <int>{};
      final IntervalTree<NotoFont> notoTree = FontFallbackData.instance.notoTree;
      for (NotoFont font in notoTree.root.enumerateAllElements()) {
        testedFonts.add(font.name);
        for (CodeunitRange range in font.approximateUnicodeRanges) {
          for (int codeUnit = range.start; codeUnit < range.end; codeUnit += 1) {
            supportedUniqueCodeUnits.add(codeUnit);
          }
        }
      }

      expect(supportedUniqueCodeUnits.length, greaterThan(10000)); // sanity check
      expect(testedFonts, unorderedEquals(<String>{
        'Noto Sans',
        'Noto Sans Malayalam UI',
        'Noto Sans Armenian',
        'Noto Sans Georgian',
        'Noto Sans Hebrew',
        'Noto Naskh Arabic UI',
        'Noto Sans Devanagari UI',
        'Noto Sans Telugu UI',
        'Noto Sans Tamil UI',
        'Noto Sans Kannada UI',
        'Noto Sans Sinhala',
        'Noto Sans Gurmukhi UI',
        'Noto Sans Gujarati UI',
        'Noto Sans Bengali UI',
        'Noto Sans Thai UI',
        'Noto Sans Lao UI',
        'Noto Sans Myanmar UI',
        'Noto Sans Ethiopic',
        'Noto Sans Khmer UI',
        'Noto Sans SC',
        'Noto Sans JP',
        'Noto Sans TC',
        'Noto Sans HK',
        'Noto Sans KR',
        'Noto Sans Egyptian Hieroglyphs',
      }));

      // Construct random paragraphs out of supported code units.
      final math.Random random = math.Random(0);
      final List<int> supportedCodeUnits = supportedUniqueCodeUnits.toList()..shuffle(random);
      const int paragraphLength = 3;

      for (int batchStart = 0; batchStart < supportedCodeUnits.length; batchStart += paragraphLength) {
        final int batchEnd = math.min(batchStart + paragraphLength, supportedCodeUnits.length);
        final Set<int> codeUnits = <int>{};
        for (int i = batchStart; i < batchEnd; i += 1) {
          codeUnits.add(supportedCodeUnits[i]);
        }
        final Set<NotoFont> fonts = <NotoFont>{};
        for (int codeUnit in codeUnits) {
          List<NotoFont> fontsForUnit = notoTree.intersections(codeUnit);

          // All code units are extracted from the same tree, so there must
          // be at least one font supporting each code unit
          expect(fontsForUnit, isNotEmpty);
          fonts.addAll(fontsForUnit);
        }

        try {
          findMinimumFontsForCodeUnits(codeUnits, fonts);
        } catch (e) {
          print(
            'findMinimumFontsForCodeunits failed:\n'
            '  Code units: ${codeUnits.join(', ')}\n'
            '  Fonts: ${fonts.map((f) => f.name).join(', ')}',
          );
          rethrow;
        }
      }
    });
    // TODO: https://github.com/flutter/flutter/issues/60040
  }, skip: isIosSafari);
}

class TestDownloader extends NotoDownloader {
  static final Map<String, String> mockDownloads = <String, String>{};
  @override
  Future<String> downloadAsString(String url, {String? debugDescription}) async {
    if (mockDownloads.containsKey(url)) {
      return mockDownloads[url]!;
    } else {
      return '';
    }
  }
}

class LoggingDownloader implements NotoDownloader {
  final List<String> log = <String>[];

  LoggingDownloader(this.delegate);

  final NotoDownloader delegate;

  @override
  Future<void> debugWhenIdle() {
    return delegate.debugWhenIdle();
  }

  @override
  Future<ByteBuffer> downloadAsBytes(String url, {String? debugDescription}) {
    log.add(debugDescription ?? url);
    return delegate.downloadAsBytes(url);
  }

  @override
  Future<String> downloadAsString(String url, {String? debugDescription}) {
    log.add(debugDescription ?? url);
    return delegate.downloadAsString(url);
  }

  @override
  int get debugActiveDownloadCount => delegate.debugActiveDownloadCount;
}
