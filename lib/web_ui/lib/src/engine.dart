// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

@JS()
library engine;

// This file is transformed during the build process in order to make it a
// single library. Some notable transformations:
//
// 1. Imports of engine/* files are stripped out.
// 2. Exports of engine/* files are replaced with a part directive.
//
// The code that performs the transformations lives in:
// - https://github.com/flutter/engine/blob/master/web_sdk/sdk_rewriter.dart

import 'dart:async';
// Some of these names are used in services/buffers.dart for example.
// ignore: unused_import
import 'dart:collection'
    show ListBase, IterableBase, DoubleLinkedQueue, DoubleLinkedQueueEntry;
import 'dart:convert' hide Codec;
import 'dart:developer' as developer;
import 'dart:html' as html;
import 'dart:js' as js;
import 'dart:js_util' as js_util;
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:js/js.dart';
import 'package:meta/meta.dart';

import '../ui.dart' as ui;

import 'engine/alarm_clock.dart';
export 'engine/alarm_clock.dart';

import 'engine/browser_detection.dart';
export 'engine/browser_detection.dart';

import 'engine/html_image_codec.dart';
export 'engine/html_image_codec.dart';

export 'engine/html/offscreen_canvas.dart';

import 'engine/html/painting.dart';
export 'engine/html/painting.dart';

import 'engine/html/path_to_svg_clip.dart';
export 'engine/html/path_to_svg_clip.dart';

import 'engine/html/path/conic.dart';
export 'engine/html/path/conic.dart';

import 'engine/html/path/cubic.dart';
export 'engine/html/path/cubic.dart';

import 'engine/html/path/path_iterator.dart';
export 'engine/html/path/path_iterator.dart';

import 'engine/html/path/path_metrics.dart';
export 'engine/html/path/path_metrics.dart';

import 'engine/html/path/path_ref.dart';
export 'engine/html/path/path_ref.dart';

import 'engine/html/path/path_to_svg.dart';
export 'engine/html/path/path_to_svg.dart';

import 'engine/html/path/path_utils.dart';
export 'engine/html/path/path_utils.dart';

import 'engine/html/path/path_windings.dart';
export 'engine/html/path/path_windings.dart';

import 'engine/html/path/tangent.dart';
export 'engine/html/path/tangent.dart';

import 'engine/html/render_vertices.dart';
export 'engine/html/render_vertices.dart';

import 'engine/html/shaders/image_shader.dart';
export 'engine/html/shaders/image_shader.dart';

export 'engine/html/shaders/normalized_gradient.dart';

import 'engine/html/shaders/shader.dart';
export 'engine/html/shaders/shader.dart';

export 'engine/html/shaders/shader_builder.dart';

export 'engine/html/shaders/vertex_shaders.dart';

export 'engine/html/shaders/webgl_context.dart';

import 'engine/mouse_cursor.dart';
export 'engine/mouse_cursor.dart';

import 'engine/navigation/history.dart';
export 'engine/navigation/history.dart';

import 'engine/navigation/js_url_strategy.dart';
export 'engine/navigation/js_url_strategy.dart';

import 'engine/navigation/url_strategy.dart';
export 'engine/navigation/url_strategy.dart';

import 'engine/plugins.dart';
export 'engine/plugins.dart';

import 'engine/pointer_binding.dart';
export 'engine/pointer_binding.dart';

// This import is intentionally commented out because the analyzer says it's unused.
// import 'engine/pointer_converter.dart';
export 'engine/pointer_converter.dart';

// This import is intentionally commented out because the analyzer says it's unused.
// import 'engine/services/buffers.dart';
export 'engine/services/buffers.dart';

import 'engine/services/message_codec.dart';
export 'engine/services/message_codec.dart';

import 'engine/services/message_codecs.dart';
export 'engine/services/message_codecs.dart';

// This import is intentionally commented out because the analyzer says it's unused.
// import 'engine/services/serialization.dart';
export 'engine/services/serialization.dart';

import 'engine/shadow.dart';
export 'engine/shadow.dart';

import 'engine/test_embedding.dart';
export 'engine/test_embedding.dart';

import 'engine/util.dart';
export 'engine/util.dart';

import 'engine/validators.dart';
export 'engine/validators.dart';

import 'engine/vector_math.dart';
export 'engine/vector_math.dart';

import 'engine/web_experiments.dart';
export 'engine/web_experiments.dart';

export 'engine/canvaskit/canvas.dart';

import 'engine/canvaskit/canvaskit_api.dart';
export 'engine/canvaskit/canvaskit_api.dart';

export 'engine/canvaskit/canvaskit_canvas.dart';

import 'engine/canvaskit/color_filter.dart';
export 'engine/canvaskit/color_filter.dart';

import 'engine/canvaskit/embedded_views.dart';
export 'engine/canvaskit/embedded_views.dart';

export 'engine/canvaskit/fonts.dart';

export 'engine/canvaskit/font_fallbacks.dart';

export 'engine/canvaskit/image.dart';

export 'engine/canvaskit/image_filter.dart';

import 'engine/canvaskit/initialization.dart';
export 'engine/canvaskit/initialization.dart';

export 'engine/canvaskit/interval_tree.dart';

import 'engine/canvaskit/layer.dart';
export 'engine/canvaskit/layer.dart';

import 'engine/canvaskit/layer_scene_builder.dart';
export 'engine/canvaskit/layer_scene_builder.dart';

export 'engine/canvaskit/layer_tree.dart';

export 'engine/canvaskit/mask_filter.dart';

export 'engine/canvaskit/n_way_canvas.dart';

export 'engine/canvaskit/painting.dart';

export 'engine/canvaskit/path.dart';

export 'engine/canvaskit/path_metrics.dart';

export 'engine/canvaskit/picture.dart';

export 'engine/canvaskit/picture_recorder.dart';

import 'engine/canvaskit/rasterizer.dart';
export 'engine/canvaskit/rasterizer.dart';

export 'engine/canvaskit/raster_cache.dart';

export 'engine/canvaskit/shader.dart';

export 'engine/canvaskit/skia_object_cache.dart';

import 'engine/canvaskit/surface.dart';
export 'engine/canvaskit/surface.dart';

export 'engine/canvaskit/text.dart';

export 'engine/canvaskit/util.dart';

export 'engine/canvaskit/vertices.dart';

part 'engine/assets.dart';
part 'engine/html/bitmap_canvas.dart';
part 'engine/canvas_pool.dart';
part 'engine/clipboard.dart';
part 'engine/color_filter.dart';
part 'engine/html/dom_canvas.dart';
part 'engine/dom_renderer.dart';
part 'engine/engine_canvas.dart';
part 'engine/font_change_util.dart';
part 'engine/frame_reference.dart';
part 'engine/html/backdrop_filter.dart';
part 'engine/html/canvas.dart';
part 'engine/html/clip.dart';
part 'engine/html/color_filter.dart';
part 'engine/html/debug_canvas_reuse_overlay.dart';
part 'engine/html/image_filter.dart';
part 'engine/html/offset.dart';
part 'engine/html/opacity.dart';
part 'engine/html/path/path.dart';
part 'engine/html/picture.dart';
part 'engine/html/platform_view.dart';
part 'engine/html/recording_canvas.dart';
part 'engine/html/scene.dart';
part 'engine/html/scene_builder.dart';
part 'engine/html/shader_mask.dart';
part 'engine/html/surface.dart';
part 'engine/html/surface_stats.dart';
part 'engine/html/transform.dart';
part 'engine/keyboard_binding.dart';
part 'engine/keyboard.dart';
part 'engine/key_map.dart';
part 'engine/onscreen_logging.dart';
part 'engine/picture.dart';
part 'engine/platform_dispatcher.dart';
part 'engine/platform_views.dart';
part 'engine/profiler.dart';
part 'engine/rrect_renderer.dart';
part 'engine/semantics/accessibility.dart';
part 'engine/semantics/checkable.dart';
part 'engine/semantics/image.dart';
part 'engine/semantics/incrementable.dart';
part 'engine/semantics/label_and_value.dart';
part 'engine/semantics/live_region.dart';
part 'engine/semantics/scrollable.dart';
part 'engine/semantics/semantics.dart';
part 'engine/semantics/semantics_helper.dart';
part 'engine/semantics/tappable.dart';
part 'engine/semantics/text_field.dart';
part 'engine/text/font_collection.dart';
part 'engine/text/layout_service.dart';
part 'engine/text/line_break_properties.dart';
part 'engine/text/line_breaker.dart';
part 'engine/text/measurement.dart';
part 'engine/text/paint_service.dart';
part 'engine/text/paragraph.dart';
part 'engine/text/canvas_paragraph.dart';
part 'engine/text/ruler.dart';
part 'engine/text/unicode_range.dart';
part 'engine/text/word_break_properties.dart';
part 'engine/text/word_breaker.dart';
part 'engine/text_editing/autofill_hint.dart';
part 'engine/text_editing/input_type.dart';
part 'engine/text_editing/text_capitalization.dart';
part 'engine/text_editing/text_editing.dart';
part 'engine/window.dart';

// The mode the app is running in.
// Keep these in sync with the same constants on the framework-side under foundation/constants.dart.
const bool kReleaseMode =
    bool.fromEnvironment('dart.vm.product', defaultValue: false);
const bool kProfileMode =
    bool.fromEnvironment('dart.vm.profile', defaultValue: false);
const bool kDebugMode = !kReleaseMode && !kProfileMode;
String get buildMode => kReleaseMode
    ? 'release'
    : kProfileMode
        ? 'profile'
        : 'debug';

/// A benchmark metric that includes frame-related computations prior to
/// submitting layer and picture operations to the underlying renderer, such as
/// HTML and CanvasKit. During this phase we compute transforms, clips, and
/// other information needed for rendering.
const String kProfilePrerollFrame = 'preroll_frame';

/// A benchmark metric that includes submitting layer and picture information
/// to the renderer.
const String kProfileApplyFrame = 'apply_frame';

bool _engineInitialized = false;

final List<ui.VoidCallback> _hotRestartListeners = <ui.VoidCallback>[];

/// Requests that [listener] is called just before hot restarting the app.
void registerHotRestartListener(ui.VoidCallback listener) {
  _hotRestartListeners.add(listener);
}

/// This method performs one-time initialization of the Web environment that
/// supports the Flutter framework.
///
/// This is only available on the Web, as native Flutter configures the
/// environment in the native embedder.
void initializeEngine() {
  if (_engineInitialized) {
    return;
  }

  // Setup the hook that allows users to customize URL strategy before running
  // the app.
  _addUrlStrategyListener();

  // Called by the Web runtime just before hot restarting the app.
  //
  // This extension cleans up resources that are registered with browser's
  // global singletons that Dart compiler is unable to clean-up automatically.
  //
  // This extension does not need to clean-up Dart statics. Those are cleaned
  // up by the compiler.
  developer.registerExtension('ext.flutter.disassemble', (_, __) {
    for (ui.VoidCallback listener in _hotRestartListeners) {
      listener();
    }
    return Future<developer.ServiceExtensionResponse>.value(
        developer.ServiceExtensionResponse.result('OK'));
  });

  _engineInitialized = true;

  // Calling this getter to force the DOM renderer to initialize before we
  // initialize framework bindings.
  domRenderer;

  WebExperiments.ensureInitialized();

  if (Profiler.isBenchmarkMode) {
    Profiler.ensureInitialized();
  }

  bool waitingForAnimation = false;
  scheduleFrameCallback = () {
    // We're asked to schedule a frame and call `frameHandler` when the frame
    // fires.
    if (!waitingForAnimation) {
      waitingForAnimation = true;
      html.window.requestAnimationFrame((num highResTime) {
        _frameTimingsOnVsync();

        // Reset immediately, because `frameHandler` can schedule more frames.
        waitingForAnimation = false;

        // We have to convert high-resolution time to `int` so we can construct
        // a `Duration` out of it. However, high-res time is supplied in
        // milliseconds as a double value, with sub-millisecond information
        // hidden in the fraction. So we first multiply it by 1000 to uncover
        // microsecond precision, and only then convert to `int`.
        final int highResTimeMicroseconds = (1000 * highResTime).toInt();

        // In Flutter terminology "building a frame" consists of "beginning
        // frame" and "drawing frame".
        //
        // We do not call `_frameTimingsOnBuildFinish` from here because
        // part of the rasterization process, particularly in the HTML
        // renderer, takes place in the `SceneBuilder.build()`.
        _frameTimingsOnBuildStart();
        if (EnginePlatformDispatcher.instance._onBeginFrame != null) {
          EnginePlatformDispatcher.instance.invokeOnBeginFrame(
              Duration(microseconds: highResTimeMicroseconds));
        }

        if (EnginePlatformDispatcher.instance._onDrawFrame != null) {
          // TODO(yjbanov): technically Flutter flushes microtasks between
          //                onBeginFrame and onDrawFrame. We don't, which hasn't
          //                been an issue yet, but eventually we'll have to
          //                implement it properly.
          EnginePlatformDispatcher.instance.invokeOnDrawFrame();
        }
      });
    }
  };

  Keyboard.initialize();
  MouseCursor.initialize();
}

void _addUrlStrategyListener() {
  _jsSetUrlStrategy = allowInterop((JsUrlStrategy? jsStrategy) {
    customUrlStrategy =
        jsStrategy == null ? null : CustomUrlStrategy.fromJs(jsStrategy);
  });
  registerHotRestartListener(() {
    _jsSetUrlStrategy = null;
  });
}

class NullTreeSanitizer implements html.NodeTreeSanitizer {
  @override
  void sanitizeTree(html.Node node) {}
}

/// Converts a matrix represented using [Float64List] to one represented using
/// [Float32List].
///
/// 32-bit precision is sufficient because Flutter Engine itself (as well as
/// Skia) use 32-bit precision under the hood anyway.
///
/// 32-bit matrices require 2x less memory and in V8 they are allocated on the
/// JavaScript heap, thus avoiding a malloc.
///
/// See also:
/// * https://bugs.chromium.org/p/v8/issues/detail?id=9199
/// * https://bugs.chromium.org/p/v8/issues/detail?id=2022
Float32List toMatrix32(Float64List matrix64) {
  final Float32List matrix32 = Float32List(16);
  matrix32[15] = matrix64[15];
  matrix32[14] = matrix64[14];
  matrix32[13] = matrix64[13];
  matrix32[12] = matrix64[12];
  matrix32[11] = matrix64[11];
  matrix32[10] = matrix64[10];
  matrix32[9] = matrix64[9];
  matrix32[8] = matrix64[8];
  matrix32[7] = matrix64[7];
  matrix32[6] = matrix64[6];
  matrix32[5] = matrix64[5];
  matrix32[4] = matrix64[4];
  matrix32[3] = matrix64[3];
  matrix32[2] = matrix64[2];
  matrix32[1] = matrix64[1];
  matrix32[0] = matrix64[0];
  return matrix32;
}
