// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:html' as html;
import 'dart:typed_data';

import 'package:ui/src/engine.dart' show window, NullTreeSanitizer;
import 'package:ui/ui.dart' as ui;

import '../html/path_to_svg_clip.dart';
import '../services.dart';
import '../util.dart';
import '../vector_math.dart';
import 'canvas.dart';
import 'initialization.dart';
import 'path.dart';
import 'picture_recorder.dart';
import 'surface.dart';

/// This composites HTML views into the [ui.Scene].
class HtmlViewEmbedder {
  /// A picture recorder associated with a view id.
  ///
  /// When we composite in the platform view, we need to create a new canvas
  /// for further paint commands to paint to, since the composited view will
  /// be on top of the current canvas, and we want further paint commands to
  /// be on top of the platform view.
  final Map<int, CkPictureRecorder> _pictureRecorders =
      <int, CkPictureRecorder>{};

  /// The most recent composition parameters for a given view id.
  ///
  /// If we receive a request to composite a view, but the composition
  /// parameters haven't changed, we can avoid having to recompute the
  /// element stack that correctly composites the view into the scene.
  final Map<int, EmbeddedViewParams> _currentCompositionParams =
      <int, EmbeddedViewParams>{};

  /// The HTML element associated with the given view id.
  final Map<int?, html.Element> _views = <int?, html.Element>{};

  /// The root view in the stack of mutator elements for the view id.
  final Map<int?, html.Element?> _rootViews = <int?, html.Element?>{};

  /// Surfaces used to draw on top of platform views, keyed by platform view ID.
  ///
  /// These surfaces are cached in the [OverlayCache] and reused.
  final Map<int, Surface> _overlays = <int, Surface>{};

  /// The views that need to be recomposited into the scene on the next frame.
  final Set<int> _viewsToRecomposite = <int>{};

  /// The views that need to be disposed of on the next frame.
  final Set<int> _viewsToDispose = <int>{};

  /// The list of view ids that should be composited, in order.
  List<int> _compositionOrder = <int>[];

  /// The most recent composition order.
  List<int> _activeCompositionOrder = <int>[];

  /// The number of clipping elements used last time the view was composited.
  Map<int, int> _clipCount = <int, int>{};

  /// The size of the frame, in physical pixels.
  ui.Size _frameSize = ui.window.physicalSize;

  void set frameSize(ui.Size size) {
    if (_frameSize == size) {
      return;
    }
    _activeCompositionOrder.clear();
    _frameSize = size;
  }

  void handlePlatformViewCall(
    ByteData? data,
    ui.PlatformMessageResponseCallback? callback,
  ) {
    const MethodCodec codec = StandardMethodCodec();
    final MethodCall decoded = codec.decodeMethodCall(data);

    switch (decoded.method) {
      case 'create':
        _create(decoded, callback);
        return;
      case 'dispose':
        _dispose(decoded, callback!);
        return;
    }
    callback!(null);
  }

  void _create(
      MethodCall methodCall, ui.PlatformMessageResponseCallback? callback) {
    final Map<dynamic, dynamic> args = methodCall.arguments;
    final int? viewId = args['id'];
    final String? viewType = args['viewType'];
    const MethodCodec codec = StandardMethodCodec();

    if (_views[viewId] != null) {
      callback!(codec.encodeErrorEnvelope(
        code: 'recreating_view',
        message: 'trying to create an already created view',
        details: 'view id: $viewId',
      ));
      return;
    }

    final ui.PlatformViewFactory? factory =
        ui.platformViewRegistry.registeredFactories[viewType];
    if (factory == null) {
      callback!(codec.encodeErrorEnvelope(
        code: 'unregistered_view_type',
        message: 'trying to create a view with an unregistered type',
        details: 'unregistered view type: $viewType',
      ));
      return;
    }

    // TODO(het): Support creation parameters.
    html.Element embeddedView = factory(viewId!);
    _views[viewId] = embeddedView;

    _rootViews[viewId] = embeddedView;

    callback!(codec.encodeSuccessEnvelope(null));
  }

  void _dispose(
      MethodCall methodCall, ui.PlatformMessageResponseCallback callback) {
    final int? viewId = methodCall.arguments;
    const MethodCodec codec = StandardMethodCodec();
    if (viewId == null || !_views.containsKey(viewId)) {
      callback(codec.encodeErrorEnvelope(
        code: 'unknown_view',
        message: 'trying to dispose an unknown view',
        details: 'view id: $viewId',
      ));
      return;
    }
    _viewsToDispose.add(viewId);
    callback(codec.encodeSuccessEnvelope(null));
  }

  List<CkCanvas> getCurrentCanvases() {
    final List<CkCanvas> canvases = <CkCanvas>[];
    for (int i = 0; i < _compositionOrder.length; i++) {
      final int viewId = _compositionOrder[i];
      canvases.add(_pictureRecorders[viewId]!.recordingCanvas!);
    }
    return canvases;
  }

  void prerollCompositeEmbeddedView(int viewId, EmbeddedViewParams params) {
    final pictureRecorder = CkPictureRecorder();
    pictureRecorder.beginRecording(ui.Offset.zero & _frameSize);
    pictureRecorder.recordingCanvas!.clear(ui.Color(0x00000000));
    _pictureRecorders[viewId] = pictureRecorder;
    _compositionOrder.add(viewId);

    // Do nothing if the params didn't change.
    if (_currentCompositionParams[viewId] == params) {
      return;
    }
    _currentCompositionParams[viewId] = params;
    _viewsToRecomposite.add(viewId);
  }

  CkCanvas? compositeEmbeddedView(int viewId) {
    // Do nothing if this view doesn't need to be composited.
    if (!_viewsToRecomposite.contains(viewId)) {
      return _pictureRecorders[viewId]!.recordingCanvas;
    }
    _compositeWithParams(viewId, _currentCompositionParams[viewId]!);
    _viewsToRecomposite.remove(viewId);
    return _pictureRecorders[viewId]!.recordingCanvas;
  }

  void _compositeWithParams(int viewId, EmbeddedViewParams params) {
    final html.Element platformView = _views[viewId]!;
    platformView.style.width = '${params.size.width}px';
    platformView.style.height = '${params.size.height}px';
    platformView.style.position = 'absolute';

    // <flt-scene-host> disables pointer events. Reenable them here because the
    // underlying platform view would want to handle the pointer events.
    platformView.style.pointerEvents = 'auto';

    final int currentClippingCount = _countClips(params.mutators);
    final int? previousClippingCount = _clipCount[viewId];
    if (currentClippingCount != previousClippingCount) {
      _clipCount[viewId] = currentClippingCount;
      html.Element oldPlatformViewRoot = _rootViews[viewId]!;
      html.Element? newPlatformViewRoot = _reconstructClipViewsChain(
        currentClippingCount,
        platformView,
        oldPlatformViewRoot,
      );
      _rootViews[viewId] = newPlatformViewRoot;
    }
    _applyMutators(params.mutators, platformView);
  }

  int _countClips(MutatorsStack mutators) {
    int clipCount = 0;
    for (final Mutator mutator in mutators) {
      if (mutator.isClipType) {
        clipCount++;
      }
    }
    return clipCount;
  }

  html.Element? _reconstructClipViewsChain(
    int numClips,
    html.Element platformView,
    html.Element headClipView,
  ) {
    int indexInFlutterView = -1;
    if (headClipView.parent != null) {
      indexInFlutterView = skiaSceneHost!.children.indexOf(headClipView);
      headClipView.remove();
    }
    html.Element? head = platformView;
    int clipIndex = 0;
    // Re-use as much existing clip views as needed.
    while (head != headClipView && clipIndex < numClips) {
      head = head!.parent;
      clipIndex++;
    }
    // If there weren't enough existing clip views, add more.
    while (clipIndex < numClips) {
      html.Element clippingView = html.Element.tag('flt-clip');
      clippingView.append(head!);
      head = clippingView;
      clipIndex++;
    }
    head!.remove();

    // If the chain was previously attached, attach it to the same position.
    if (indexInFlutterView > -1) {
      skiaSceneHost!.children.insert(indexInFlutterView, head);
    }
    return head;
  }

  void _applyMutators(MutatorsStack mutators, html.Element embeddedView) {
    html.Element head = embeddedView;
    Matrix4 headTransform = Matrix4.identity();
    double embeddedOpacity = 1.0;
    _resetAnchor(head);

    for (final Mutator mutator in mutators) {
      switch (mutator.type) {
        case MutatorType.transform:
          headTransform = mutator.matrix!.multiplied(headTransform);
          head.style.transform =
              float64ListToCssTransform(headTransform.storage);
          break;
        case MutatorType.clipRect:
        case MutatorType.clipRRect:
        case MutatorType.clipPath:
          html.Element clipView = head.parent!;
          clipView.style.clip = '';
          clipView.style.clipPath = '';
          headTransform = Matrix4.identity();
          clipView.style.transform = '';
          if (mutator.rect != null) {
            final ui.Rect rect = mutator.rect!;
            clipView.style.clip = 'rect(${rect.top}px, ${rect.right}px, '
                '${rect.bottom}px, ${rect.left}px)';
          } else if (mutator.rrect != null) {
            final CkPath path = CkPath();
            path.addRRect(mutator.rrect!);
            _ensureSvgPathDefs();
            html.Element pathDefs =
                _svgPathDefs!.querySelector('#sk_path_defs')!;
            _clipPathCount += 1;
            html.Node newClipPath = html.DocumentFragment.svg(
              '<clipPath id="svgClip$_clipPathCount">'
              '<path d="${path.toSvgString()}">'
              '</path></clipPath>',
              treeSanitizer: NullTreeSanitizer(),
            );
            pathDefs.append(newClipPath);
            clipView.style.clipPath = 'url(#svgClip$_clipPathCount)';
          } else if (mutator.path != null) {
            final CkPath path = mutator.path as CkPath;
            _ensureSvgPathDefs();
            html.Element pathDefs =
                _svgPathDefs!.querySelector('#sk_path_defs')!;
            _clipPathCount += 1;
            html.Node newClipPath = html.DocumentFragment.svg(
              '<clipPath id="svgClip$_clipPathCount">'
              '<path d="${path.toSvgString()}">'
              '</path></clipPath>',
              treeSanitizer: NullTreeSanitizer(),
            );
            pathDefs.append(newClipPath);
            clipView.style.clipPath = 'url(#svgClip$_clipPathCount)';
          }
          _resetAnchor(clipView);
          head = clipView;
          break;
        case MutatorType.opacity:
          embeddedOpacity *= mutator.alphaFloat;
          break;
      }
    }

    embeddedView.style.opacity = embeddedOpacity.toString();

    // Reverse scale based on screen scale.
    //
    // HTML elements use logical (CSS) pixels, but we have been using physical
    // pixels, so scale down the head element to match the logical resolution.
    final double scale = window.devicePixelRatio;
    final double inverseScale = 1 / scale;
    final Matrix4 scaleMatrix =
        Matrix4.diagonal3Values(inverseScale, inverseScale, 1);
    headTransform = scaleMatrix.multiplied(headTransform);
    head.style.transform = float64ListToCssTransform(headTransform.storage);
  }

  /// Sets the transform origin to the top-left corner of the element.
  ///
  /// By default, the transform origin is the center of the element, but
  /// Flutter assumes the transform origin is the top-left point.
  void _resetAnchor(html.Element element) {
    element.style.transformOrigin = '0 0 0';
    element.style.position = 'absolute';
  }

  int _clipPathCount = 0;

  html.Element? _svgPathDefs;

  /// Ensures we add a container of SVG path defs to the DOM so they can
  /// be referred to in clip-path: url(#blah).
  void _ensureSvgPathDefs() {
    if (_svgPathDefs != null) {
      return;
    }
    _svgPathDefs = html.Element.html(
      '$kSvgResourceHeader<defs id="sk_path_defs"></defs></svg>',
      treeSanitizer: NullTreeSanitizer(),
    );
    skiaSceneHost!.append(_svgPathDefs!);
  }

  void submitFrame() {
    disposeViews();

    for (int i = 0; i < _compositionOrder.length; i++) {
      int viewId = _compositionOrder[i];
      _ensureOverlayInitialized(viewId);
      final SurfaceFrame frame = _overlays[viewId]!.acquireFrame(_frameSize);
      final CkCanvas canvas = frame.skiaCanvas;
      canvas.drawPicture(
        _pictureRecorders[viewId]!.endRecording(),
      );
      frame.submit();
    }
    _pictureRecorders.clear();
    if (listEquals(_compositionOrder, _activeCompositionOrder)) {
      _compositionOrder.clear();
      return;
    }

    final Set<int> unusedViews = Set<int>.from(_activeCompositionOrder);
    _activeCompositionOrder.clear();

    List<int>? debugInvalidViewIds;
    for (int i = 0; i < _compositionOrder.length; i++) {
      int viewId = _compositionOrder[i];

      if (assertionsEnabled) {
        if (!_views.containsKey(viewId)) {
          debugInvalidViewIds ??= <int>[];
          debugInvalidViewIds.add(viewId);
          continue;
        }
      }

      unusedViews.remove(viewId);
      html.Element platformViewRoot = _rootViews[viewId]!;
      html.Element overlay = _overlays[viewId]!.htmlElement;
      platformViewRoot.remove();
      skiaSceneHost!.append(platformViewRoot);
      overlay.remove();
      skiaSceneHost!.append(overlay);
      _activeCompositionOrder.add(viewId);
    }
    _compositionOrder.clear();

    for (final int unusedViewId in unusedViews) {
      _releaseOverlay(unusedViewId);
      _rootViews[unusedViewId]?.remove();
    }

    if (assertionsEnabled) {
      if (debugInvalidViewIds != null && debugInvalidViewIds.isNotEmpty) {
        throw AssertionError(
          'Cannot render platform views: ${debugInvalidViewIds.join(', ')}. '
          'These views have not been created, or they have been deleted.',
        );
      }
    }
  }

  void disposeViews() {
    if (_viewsToDispose.isEmpty) {
      return;
    }

    for (final int viewId in _viewsToDispose) {
      final html.Element rootView = _rootViews[viewId]!;
      rootView.remove();
      _views.remove(viewId);
      _rootViews.remove(viewId);
      _releaseOverlay(viewId);
      _currentCompositionParams.remove(viewId);
      _clipCount.remove(viewId);
      _viewsToRecomposite.remove(viewId);
    }
    _viewsToDispose.clear();
  }

  void _releaseOverlay(int viewId) {
    if (_overlays[viewId] != null) {
      OverlayCache.instance.releaseOverlay(_overlays[viewId]!);
      _overlays.remove(viewId);
    }
  }

  void _ensureOverlayInitialized(int viewId) {
    // If there's an active overlay for the view ID, continue using it.
    Surface? overlay = _overlays[viewId];
    if (overlay != null) {
      return;
    }

    // Try reusing a cached overlay created for another platform view.
    overlay = OverlayCache.instance.reserveOverlay();

    // If nothing to reuse, create a new overlay.
    if (overlay == null) {
      overlay = Surface(this);
    }

    _overlays[viewId] = overlay;
  }
}

/// Caches surfaces used to overlay platform views.
class OverlayCache {
  static const int kDefaultCacheSize = 5;

  /// The cache singleton.
  static final OverlayCache instance = OverlayCache(kDefaultCacheSize);

  OverlayCache(this.maximumSize);

  /// The cache will not grow beyond this size.
  final int maximumSize;

  /// Cached surfaces, available for reuse.
  final List<Surface> _cache = <Surface>[];

  /// Returns the list of cached surfaces.
  ///
  /// Useful in tests.
  List<Surface> get debugCachedSurfaces => _cache;

  /// Reserves an overlay from the cache, if available.
  ///
  /// Returns null if the cache is empty.
  Surface? reserveOverlay() {
    if (_cache.isEmpty) {
      return null;
    }
    return _cache.removeLast();
  }

  /// Returns an overlay back to the cache.
  ///
  /// If the cache is full, the overlay is deleted.
  void releaseOverlay(Surface overlay) {
    overlay.htmlElement.remove();
    if (_cache.length < maximumSize) {
      _cache.add(overlay);
    } else {
      overlay.dispose();
    }
  }

  int get debugLength => _cache.length;

  void debugClear() {
    for (final Surface overlay in _cache) {
      overlay.dispose();
    }
    _cache.clear();
  }
}

/// The parameters passed to the view embedder.
class EmbeddedViewParams {
  EmbeddedViewParams(this.offset, this.size, MutatorsStack mutators)
      : mutators = MutatorsStack._copy(mutators);

  final ui.Offset offset;
  final ui.Size size;
  final MutatorsStack mutators;

  bool operator ==(Object other) {
    if (identical(this, other)) {
      return true;
    }
    return other is EmbeddedViewParams &&
        other.offset == offset &&
        other.size == size &&
        other.mutators == mutators;
  }

  int get hashCode => ui.hashValues(offset, size, mutators);
}

enum MutatorType {
  clipRect,
  clipRRect,
  clipPath,
  transform,
  opacity,
}

/// Stores mutation information like clipping or transform.
class Mutator {
  const Mutator._(
    this.type,
    this.rect,
    this.rrect,
    this.path,
    this.matrix,
    this.alpha,
  );

  final MutatorType type;
  final ui.Rect? rect;
  final ui.RRect? rrect;
  final ui.Path? path;
  final Matrix4? matrix;
  final int? alpha;

  const Mutator.clipRect(ui.Rect rect)
      : this._(MutatorType.clipRect, rect, null, null, null, null);
  const Mutator.clipRRect(ui.RRect rrect)
      : this._(MutatorType.clipRRect, null, rrect, null, null, null);
  const Mutator.clipPath(ui.Path path)
      : this._(MutatorType.clipPath, null, null, path, null, null);
  const Mutator.transform(Matrix4 matrix)
      : this._(MutatorType.transform, null, null, null, matrix, null);
  const Mutator.opacity(int alpha)
      : this._(MutatorType.opacity, null, null, null, null, alpha);

  bool get isClipType =>
      type == MutatorType.clipRect ||
      type == MutatorType.clipRRect ||
      type == MutatorType.clipPath;

  double get alphaFloat => alpha! / 255.0;

  bool operator ==(Object other) {
    if (identical(this, other)) {
      return true;
    }
    if (other is! Mutator) {
      return false;
    }

    final Mutator typedOther = other;
    if (type != typedOther.type) {
      return false;
    }

    switch (type) {
      case MutatorType.clipRect:
        return rect == typedOther.rect;
      case MutatorType.clipRRect:
        return rrect == typedOther.rrect;
      case MutatorType.clipPath:
        return path == typedOther.path;
      case MutatorType.transform:
        return matrix == typedOther.matrix;
      case MutatorType.opacity:
        return alpha == typedOther.alpha;
      default:
        return false;
    }
  }

  int get hashCode => ui.hashValues(type, rect, rrect, path, matrix, alpha);
}

/// A stack of mutators that can be applied to an embedded view.
class MutatorsStack extends Iterable<Mutator> {
  MutatorsStack() : _mutators = <Mutator>[];

  MutatorsStack._copy(MutatorsStack original)
      : _mutators = List<Mutator>.from(original._mutators);

  final List<Mutator> _mutators;

  void pushClipRect(ui.Rect rect) {
    _mutators.add(Mutator.clipRect(rect));
  }

  void pushClipRRect(ui.RRect rrect) {
    _mutators.add(Mutator.clipRRect(rrect));
  }

  void pushClipPath(ui.Path path) {
    _mutators.add(Mutator.clipPath(path));
  }

  void pushTransform(Matrix4 matrix) {
    _mutators.add(Mutator.transform(matrix));
  }

  void pushOpacity(int alpha) {
    _mutators.add(Mutator.opacity(alpha));
  }

  void pop() {
    _mutators.removeLast();
  }

  bool operator ==(Object other) {
    if (identical(other, this)) {
      return true;
    }
    return other is MutatorsStack &&
        listEquals<Mutator>(other._mutators, _mutators);
  }

  int get hashCode => ui.hashList(_mutators);

  @override
  Iterator<Mutator> get iterator => _mutators.reversed.iterator;
}
