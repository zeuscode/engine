// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_METAL_COMPOSITOR_H_
#define FLUTTER_METAL_COMPOSITOR_H_

#include "flutter/fml/macros.h"
#include "flutter/shell/platform/darwin/macos/framework/Source/FlutterCompositor.h"

namespace flutter {

class FlutterMetalCompositor : public FlutterCompositor {
 public:
  explicit FlutterMetalCompositor(FlutterViewController* view_controller);

  virtual ~FlutterMetalCompositor() = default;

  // Creates a BackingStore and sets backing_store_out to a
  // FlutterBackingStore struct containing details of the new
  // backing store.
  //
  // If the backing store is being requested for the first time
  // for a given frame, we do not create a new backing store but
  // rather return the backing store associated with the
  // FlutterView's FlutterSurfaceManager.
  //
  // Any additional state allocated for the backing store and
  // saved as user_data in the backing store must be collected
  // in backing_store_out's destruction_callback field which will
  // be called when the embedder collects the backing store.
  bool CreateBackingStore(const FlutterBackingStoreConfig* config,
                          FlutterBackingStore* backing_store_out) override;

  // Releases and deallocates any and all resources that were allocated
  // for this FlutterBackingStore object in CreateBackingStore.
  bool CollectBackingStore(const FlutterBackingStore* backing_store) override;

  // Composites the provided FlutterLayer objects and presents the composited
  // frame to the FlutterView(s).
  bool Present(const FlutterLayer** layers, size_t layers_count) override;

  FML_DISALLOW_COPY_AND_ASSIGN(FlutterMetalCompositor);
};

}  // namespace flutter

#endif  // FLUTTER_METAL_COMPOSITOR_H_
