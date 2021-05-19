// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6
import 'dart:html' as html;

import 'package:test/bootstrap/browser.dart';
import 'package:test/test.dart';
import 'package:ui/src/engine.dart';

const PointerSupportDetector _defaultSupportDetector = PointerSupportDetector();

void main() {
  internalBootstrapBrowserTest(() => testMain);
}

void testMain() {
  group('$DesktopSemanticsEnabler', () {
    DesktopSemanticsEnabler desktopSemanticsEnabler;
    html.Element _placeholder;

    setUp(() {
      desktopSemanticsEnabler = DesktopSemanticsEnabler();
    });

    tearDown(() {
      if (_placeholder != null) {
        _placeholder.remove();
      }
    });

    test('prepare accesibility placeholder', () async {
      _placeholder = desktopSemanticsEnabler.prepareAccessibilityPlaceholder();

      expect(_placeholder.getAttribute('role'), 'button');
      expect(_placeholder.getAttribute('aria-live'), 'true');
      expect(_placeholder.getAttribute('tabindex'), '0');

      html.document.body.append(_placeholder);

      expect(html.document.getElementsByTagName('flt-semantics-placeholder'),
          isNotEmpty);

      expect(_placeholder.getBoundingClientRect().height, 1);
      expect(_placeholder.getBoundingClientRect().width, 1);
      expect(_placeholder.getBoundingClientRect().top, -1);
      expect(_placeholder.getBoundingClientRect().left, -1);
    },
        // TODO(nurhan): https://github.com/flutter/flutter/issues/50590
        skip: browserEngine == BrowserEngine.webkit);

    test('Not relevant events should be forwarded to the framework', () async {
      // Prework. Attach the placeholder to dom.
      _placeholder = desktopSemanticsEnabler.prepareAccessibilityPlaceholder();
      html.document.body.append(_placeholder);

      html.Event event = html.MouseEvent('mousemove');
      bool shouldForwardToFramework =
          desktopSemanticsEnabler.tryEnableSemantics(event);

      expect(shouldForwardToFramework, true);

      // Pointer events are not defined in webkit.
      if (browserEngine != BrowserEngine.webkit) {
        event = html.PointerEvent('pointermove');
        shouldForwardToFramework =
            desktopSemanticsEnabler.tryEnableSemantics(event);

        expect(shouldForwardToFramework, true);
      }
    });

    test(
        'Relevants events targeting placeholder should not be forwarded to the framework',
        () async {
      // Prework. Attach the placeholder to dom.
      _placeholder = desktopSemanticsEnabler.prepareAccessibilityPlaceholder();
      html.document.body.append(_placeholder);

      html.Event event = html.MouseEvent('mousedown');
      _placeholder.dispatchEvent(event);

      bool shouldForwardToFramework =
          desktopSemanticsEnabler.tryEnableSemantics(event);

      expect(shouldForwardToFramework, false);
    });

    test('disposes of the placeholder', () {
      _placeholder = desktopSemanticsEnabler.prepareAccessibilityPlaceholder();
      html.document.body.append(_placeholder);

      expect(_placeholder.isConnected, isTrue);
      desktopSemanticsEnabler.dispose();
      expect(_placeholder.isConnected, isFalse);
    });
  });

  group('$MobileSemanticsEnabler', () {
    MobileSemanticsEnabler mobileSemanticsEnabler;
    html.Element _placeholder;

    setUp(() {
      mobileSemanticsEnabler = MobileSemanticsEnabler();
    });

    tearDown(() {
      if (_placeholder != null) {
        _placeholder.remove();
      }
    });

    test('prepare accesibility placeholder', () async {
      _placeholder = mobileSemanticsEnabler.prepareAccessibilityPlaceholder();

      expect(_placeholder.getAttribute('role'), 'button');

      html.document.body.append(_placeholder);

      // Placeholder should cover all the screen on a mobile device.
      final num bodyHeight = html.window.innerHeight;
      final num bodyWidht = html.window.innerWidth;

      expect(_placeholder.getBoundingClientRect().height, bodyHeight);
      expect(_placeholder.getBoundingClientRect().width, bodyWidht);
    },
        // TODO(nurhan): https://github.com/flutter/flutter/issues/50590
        skip: browserEngine == BrowserEngine.webkit);

    test('Not relevant events should be forwarded to the framework', () async {
      html.Event event;
      if (_defaultSupportDetector.hasPointerEvents) {
        event = html.PointerEvent('pointermove');
      } else if (_defaultSupportDetector.hasTouchEvents) {
        event = html.TouchEvent('touchcancel');
      } else {
        event = html.MouseEvent('mousemove');
      }

      bool shouldForwardToFramework =
          mobileSemanticsEnabler.tryEnableSemantics(event);

      expect(shouldForwardToFramework, true);
    });
  },  // Run the `MobileSemanticsEnabler` only on mobile browsers.
      skip: isDesktop);
}
