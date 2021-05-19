// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia_external_view_embedder.h"

#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/types.h>

#include <algorithm>  // For std::clamp

#include "flutter/fml/trace_event.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace flutter_runner {
namespace {

// Layer separation is as infinitesimal as possible without introducing
// Z-fighting.
constexpr float kScenicZElevationBetweenLayers = 0.0001f;
constexpr float kScenicZElevationForPlatformView = 100.f;
constexpr float kScenicElevationForInputInterceptor = 500.f;

SkScalar OpacityFromMutatorStack(const flutter::MutatorsStack& mutatorsStack) {
  SkScalar mutatorsOpacity = 1.f;
  for (auto i = mutatorsStack.Bottom(); i != mutatorsStack.Top(); ++i) {
    const auto& mutator = *i;
    switch (mutator->GetType()) {
      case flutter::MutatorType::opacity: {
        mutatorsOpacity *= std::clamp(mutator->GetAlphaFloat(), 0.f, 1.f);
      } break;
      default: {
        break;
      }
    }
  }

  return mutatorsOpacity;
}

SkMatrix TransformFromMutatorStack(
    const flutter::MutatorsStack& mutatorsStack) {
  SkMatrix mutatorsTransform;
  for (auto i = mutatorsStack.Bottom(); i != mutatorsStack.Top(); ++i) {
    const auto& mutator = *i;
    switch (mutator->GetType()) {
      case flutter::MutatorType::transform: {
        mutatorsTransform.preConcat(mutator->GetMatrix());
      } break;
      default: {
        break;
      }
    }
  }

  return mutatorsTransform;
}

}  // namespace

FuchsiaExternalViewEmbedder::FuchsiaExternalViewEmbedder(
    std::string debug_label,
    fuchsia::ui::views::ViewToken view_token,
    scenic::ViewRefPair view_ref_pair,
    DefaultSessionConnection& session,
    VulkanSurfaceProducer& surface_producer,
    bool intercept_all_input)
    : session_(session),
      surface_producer_(surface_producer),
      root_view_(session_.get(),
                 std::move(view_token),
                 std::move(view_ref_pair.control_ref),
                 std::move(view_ref_pair.view_ref),
                 debug_label),
      metrics_node_(session_.get()),
      layer_tree_node_(session_.get()) {
  layer_tree_node_.SetLabel("Flutter::LayerTree");
  metrics_node_.SetLabel("Flutter::MetricsWatcher");
  metrics_node_.SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);
  metrics_node_.AddChild(layer_tree_node_);
  root_view_.AddChild(metrics_node_);

  // Set up the input interceptor at the top of the scene, if applicable.  It
  // will capture all input, and any unwanted input will be reinjected into
  // embedded views.
  if (intercept_all_input) {
    input_interceptor_node_.emplace(session_.get());
    input_interceptor_node_->SetLabel("Flutter::InputInterceptor");
    input_interceptor_node_->SetHitTestBehavior(
        fuchsia::ui::gfx::HitTestBehavior::kDefault);
    input_interceptor_node_->SetSemanticVisibility(false);

    metrics_node_.AddChild(input_interceptor_node_.value());
  }

  session_.Present();
}

FuchsiaExternalViewEmbedder::~FuchsiaExternalViewEmbedder() = default;

SkCanvas* FuchsiaExternalViewEmbedder::GetRootCanvas() {
  auto found = frame_layers_.find(kRootLayerId);
  if (found == frame_layers_.end()) {
    FML_DLOG(WARNING)
        << "No root canvas could be found. This is extremely unlikely and "
           "indicates that the external view embedder did not receive the "
           "notification to begin the frame.";
    return nullptr;
  }

  return found->second.canvas_spy->GetSpyingCanvas();
}

std::vector<SkCanvas*> FuchsiaExternalViewEmbedder::GetCurrentCanvases() {
  std::vector<SkCanvas*> canvases;
  for (const auto& layer : frame_layers_) {
    // This method (for legacy reasons) expects non-root current canvases.
    if (layer.first.has_value()) {
      canvases.push_back(layer.second.canvas_spy->GetSpyingCanvas());
    }
  }
  return canvases;
}

void FuchsiaExternalViewEmbedder::PrerollCompositeEmbeddedView(
    int view_id,
    std::unique_ptr<flutter::EmbeddedViewParams> params) {
  zx_handle_t handle = static_cast<zx_handle_t>(view_id);
  FML_CHECK(frame_layers_.count(handle) == 0);

  frame_layers_.emplace(std::make_pair(EmbedderLayerId{handle},
                                       EmbedderLayer(frame_size_, *params)));
  frame_composition_order_.push_back(handle);
}

SkCanvas* FuchsiaExternalViewEmbedder::CompositeEmbeddedView(int view_id) {
  zx_handle_t handle = static_cast<zx_handle_t>(view_id);
  auto found = frame_layers_.find(handle);
  FML_CHECK(found != frame_layers_.end());

  return found->second.canvas_spy->GetSpyingCanvas();
}

flutter::PostPrerollResult FuchsiaExternalViewEmbedder::PostPrerollAction(
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
  return flutter::PostPrerollResult::kSuccess;
}

void FuchsiaExternalViewEmbedder::BeginFrame(
    SkISize frame_size,
    GrDirectContext* context,
    double device_pixel_ratio,
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
  TRACE_EVENT0("flutter", "FuchsiaExternalViewEmbedder::BeginFrame");

  // Reset for new frame.
  Reset();
  frame_size_ = frame_size;
  frame_dpr_ = device_pixel_ratio;

  // Create the root layer.
  frame_layers_.emplace(
      std::make_pair(kRootLayerId, EmbedderLayer(frame_size, std::nullopt)));
  frame_composition_order_.push_back(kRootLayerId);

  // Set up the input interceptor at the top of the scene, if applicable.
  if (input_interceptor_node_.has_value()) {
    const uint64_t rect_hash =
        (static_cast<uint64_t>(frame_size_.width()) << 32) +
        frame_size_.height();

    // Create a new rect if needed for the interceptor.
    auto found_rect = scenic_interceptor_rects_.find(rect_hash);
    if (found_rect == scenic_interceptor_rects_.end()) {
      auto [emplaced_rect, success] =
          scenic_interceptor_rects_.emplace(std::make_pair(
              rect_hash, scenic::Rectangle(session_.get(), frame_size_.width(),
                                           frame_size_.height())));
      FML_CHECK(success);

      found_rect = std::move(emplaced_rect);
    }

    // TODO(fxb/): Don't hardcode elevation.
    input_interceptor_node_->SetTranslation(
        frame_size.width() * 0.5f, frame_size.height() * 0.5f,
        -kScenicElevationForInputInterceptor);
    input_interceptor_node_->SetShape(found_rect->second);
  }
}

void FuchsiaExternalViewEmbedder::EndFrame(
    bool should_resubmit_frame,
    fml::RefPtr<fml::RasterThreadMerger> raster_thread_merger) {
  TRACE_EVENT0("flutter", "FuchsiaExternalViewEmbedder::EndFrame");
}

void FuchsiaExternalViewEmbedder::SubmitFrame(
    GrDirectContext* context,
    std::unique_ptr<flutter::SurfaceFrame> frame,
    const std::shared_ptr<const fml::SyncSwitch>& gpu_disable_sync_switch) {
  TRACE_EVENT0("flutter", "FuchsiaExternalViewEmbedder::SubmitFrame");
  std::vector<std::unique_ptr<SurfaceProducerSurface>> frame_surfaces;
  std::unordered_map<EmbedderLayerId, size_t> frame_surface_indices;

  // Create surfaces for the frame and associate them with layer IDs.
  {
    TRACE_EVENT0("flutter", "CreateSurfaces");

    for (const auto& layer : frame_layers_) {
      if (!layer.second.canvas_spy->DidDrawIntoCanvas()) {
        continue;
      }

      auto surface =
          surface_producer_.ProduceSurface(layer.second.surface_size);
      if (!surface) {
        const std::string layer_id_str =
            layer.first.has_value() ? std::to_string(layer.first.value())
                                    : "Background";
        FML_LOG(ERROR) << "Failed to create surface for layer " << layer_id_str
                       << "; size (" << layer.second.surface_size.width()
                       << ", " << layer.second.surface_size.height() << ")";
        FML_DCHECK(false);
        continue;
      }

      frame_surface_indices.emplace(
          std::make_pair(layer.first, frame_surfaces.size()));
      frame_surfaces.emplace_back(std::move(surface));
    }
  }

  // Submit layers and platform views to Scenic in composition order.
  {
    TRACE_EVENT0("flutter", "SubmitLayers");

    std::unordered_map<uint64_t, size_t> scenic_rect_indices;
    size_t scenic_layer_index = 0;
    float embedded_views_height = 0.0f;

    // First re-scale everything according to the DPR.
    const float inv_dpr = 1.0f / frame_dpr_;
    layer_tree_node_.SetScale(inv_dpr, inv_dpr, 1.0f);

    bool first_layer = true;
    for (const auto& layer_id : frame_composition_order_) {
      const auto& layer = frame_layers_.find(layer_id);
      FML_CHECK(layer != frame_layers_.end());

      // Draw the PlatformView associated with each layer first.
      if (layer_id.has_value()) {
        FML_CHECK(layer->second.embedded_view_params.has_value());
        auto& view_params = layer->second.embedded_view_params.value();

        // Validate the MutatorsStack encodes the same transform as the
        // transform matrix.
        FML_DCHECK(TransformFromMutatorStack(view_params.mutatorsStack()) ==
                   view_params.transformMatrix());

        // Get the ScenicView structure corresponding to the platform view.
        auto found = scenic_views_.find(layer_id.value());
        FML_CHECK(found != scenic_views_.end());
        auto& view_holder = found->second;

        // Compute offset and size for the platform view.
        const SkMatrix& view_transform = view_params.transformMatrix();
        const SkPoint view_offset = SkPoint::Make(
            view_transform.getTranslateX(), view_transform.getTranslateY());
        const SkSize view_size = view_params.sizePoints();
        const SkSize view_scale = SkSize::Make(view_transform.getScaleX(),
                                               view_transform.getScaleY());
        FML_DCHECK(!view_size.isEmpty() && !view_scale.isEmpty());

        // Compute opacity for the platform view.
        const float view_opacity =
            OpacityFromMutatorStack(view_params.mutatorsStack());

        // Set opacity.
        if (view_opacity != view_holder.opacity) {
          view_holder.opacity_node.SetOpacity(view_opacity);
          view_holder.opacity = view_opacity;
        }

        // Set transform and elevation.
        const float view_elevation =
            kScenicZElevationBetweenLayers * scenic_layer_index +
            embedded_views_height;
        if (view_offset != view_holder.offset ||
            view_scale != view_holder.scale ||
            view_elevation != view_holder.elevation) {
          view_holder.entity_node.SetTranslation(view_offset.fX, view_offset.fY,
                                                 -view_elevation);
          view_holder.entity_node.SetScale(view_scale.fWidth,
                                           view_scale.fHeight, 1.f);
          view_holder.offset = view_offset;
          view_holder.scale = view_scale;
          view_holder.elevation = view_elevation;
        }

        // Set HitTestBehavior.
        if (view_holder.pending_hit_testable != view_holder.hit_testable) {
          view_holder.entity_node.SetHitTestBehavior(
              view_holder.pending_hit_testable
                  ? fuchsia::ui::gfx::HitTestBehavior::kDefault
                  : fuchsia::ui::gfx::HitTestBehavior::kSuppress);
          view_holder.hit_testable = view_holder.pending_hit_testable;
        }

        // Set size, occlusion hint, and focusable.
        //
        // Scenic rejects `SetViewProperties` calls with a zero size.
        if (!view_size.isEmpty() &&
            (view_size != view_holder.size ||
             view_holder.pending_occlusion_hint != view_holder.occlusion_hint ||
             view_holder.pending_focusable != view_holder.focusable)) {
          view_holder.size = view_size;
          view_holder.occlusion_hint = view_holder.pending_occlusion_hint;
          view_holder.focusable = view_holder.pending_focusable;
          view_holder.view_holder.SetViewProperties({
              .bounding_box =
                  {
                      .min = {.x = 0.f, .y = 0.f, .z = -1000.f},
                      .max = {.x = view_holder.size.fWidth,
                              .y = view_holder.size.fHeight,
                              .z = 0.f},
                  },
              .inset_from_min = {.x = view_holder.occlusion_hint.fLeft,
                                 .y = view_holder.occlusion_hint.fTop,
                                 .z = 0.f},
              .inset_from_max = {.x = view_holder.occlusion_hint.fRight,
                                 .y = view_holder.occlusion_hint.fBottom,
                                 .z = 0.f},
              .focus_change = view_holder.focusable,
          });
        }

        // Attach the ScenicView to the main scene graph.
        layer_tree_node_.AddChild(view_holder.opacity_node);

        // Account for the ScenicView's height when positioning the next layer.
        embedded_views_height += kScenicZElevationForPlatformView;
      }

      // Acquire the surface associated with the layer.
      SurfaceProducerSurface* surface_for_layer = nullptr;
      if (layer->second.canvas_spy->DidDrawIntoCanvas()) {
        const auto& surface_index = frame_surface_indices.find(layer_id);
        if (surface_index != frame_surface_indices.end()) {
          FML_CHECK(surface_index->second < frame_surfaces.size());
          surface_for_layer = frame_surfaces[surface_index->second].get();
          FML_CHECK(surface_for_layer != nullptr);
        } else {
          const std::string layer_id_str =
              layer_id.has_value() ? std::to_string(layer_id.value())
                                   : "Background";
          FML_LOG(ERROR) << "Missing surface for layer " << layer_id_str
                         << "; skipping scene graph add of layer.";
          FML_DCHECK(false);
        }
      }

      // Draw the layer if we acquired a surface for it successfully.
      if (surface_for_layer != nullptr) {
        // Create a new layer if needed for the surface.
        FML_CHECK(scenic_layer_index <= scenic_layers_.size());
        if (scenic_layer_index == scenic_layers_.size()) {
          ScenicLayer new_layer{
              .shape_node = scenic::ShapeNode(session_.get()),
              .material = scenic::Material(session_.get()),
          };
          new_layer.shape_node.SetMaterial(new_layer.material);
          scenic_layers_.emplace_back(std::move(new_layer));
        }

        // Compute a hash and index for the rect.
        const uint64_t rect_hash =
            (static_cast<uint64_t>(layer->second.surface_size.width()) << 32) +
            layer->second.surface_size.height();
        size_t rect_index = 0;
        auto found_index = scenic_rect_indices.find(rect_hash);
        if (found_index == scenic_rect_indices.end()) {
          scenic_rect_indices.emplace(std::make_pair(rect_hash, 0));
        } else {
          rect_index = found_index->second + 1;
          scenic_rect_indices[rect_hash] = rect_index;
        }

        // Create a new rect if needed for the surface.
        auto found_rects = scenic_rects_.find(rect_hash);
        if (found_rects == scenic_rects_.end()) {
          auto [emplaced_rects, success] = scenic_rects_.emplace(
              std::make_pair(rect_hash, std::vector<scenic::Rectangle>()));
          FML_CHECK(success);

          found_rects = std::move(emplaced_rects);
        }
        FML_CHECK(rect_index <= found_rects->second.size());
        if (rect_index == found_rects->second.size()) {
          found_rects->second.emplace_back(scenic::Rectangle(
              session_.get(), layer->second.surface_size.width(),
              layer->second.surface_size.height()));
        }

        // Set layer shape and texture.
        // Scenic currently lacks an API to enable rendering of alpha channel;
        // Flutter Embedder also lacks an API to detect if a layer has alpha or
        // not. Alpha channels are only rendered if there is a OpacityNode
        // higher in the tree with opacity != 1. For now, assume any layer
        // beyond the first has alpha and clamp to a infinitesimally smaller
        // value than 1.  The first layer retains an opacity of 1 to avoid
        // blending with anything underneath.
        //
        // This does not cause visual problems in practice, but probably has
        // performance implications.
        const SkAlpha layer_opacity =
            first_layer ? SK_AlphaOPAQUE : SK_AlphaOPAQUE - 1;
        const float layer_elevation =
            kScenicZElevationBetweenLayers * scenic_layer_index +
            embedded_views_height;
        auto& scenic_layer = scenic_layers_[scenic_layer_index];
        auto& scenic_rect = found_rects->second[rect_index];
        scenic_layer.shape_node.SetLabel("Flutter::Layer");
        scenic_layer.shape_node.SetShape(scenic_rect);
        scenic_layer.shape_node.SetTranslation(
            layer->second.surface_size.width() * 0.5f,
            layer->second.surface_size.height() * 0.5f, -layer_elevation);
        scenic_layer.material.SetColor(SK_AlphaOPAQUE, SK_AlphaOPAQUE,
                                       SK_AlphaOPAQUE, layer_opacity);
        scenic_layer.material.SetTexture(surface_for_layer->GetImageId());

        // Only the first (i.e. the bottom-most) layer should receive input.
        // TODO: Workaround for invisible overlays stealing input. Remove when
        // the underlying bug is fixed.
        const fuchsia::ui::gfx::HitTestBehavior layer_hit_test_behavior =
            first_layer ? fuchsia::ui::gfx::HitTestBehavior::kDefault
                        : fuchsia::ui::gfx::HitTestBehavior::kSuppress;
        scenic_layer.shape_node.SetHitTestBehavior(layer_hit_test_behavior);

        // Attach the ScenicLayer to the main scene graph.
        layer_tree_node_.AddChild(scenic_layer.shape_node);
      }

      // Reset for the next pass:
      //  +The next layer will not be the first layer.
      //  +Account for the current layer's height when positioning the next.
      first_layer = false;
      scenic_layer_index++;
    }
  }

  // Present the session to Scenic, along with surface acquire/release fencess.
  {
    TRACE_EVENT0("flutter", "SessionPresent");

    session_.Present();
  }

  // Render the recorded SkPictures into the surfaces.
  {
    TRACE_EVENT0("flutter", "RasterizeSurfaces");

    for (const auto& surface_index : frame_surface_indices) {
      TRACE_EVENT0("flutter", "RasterizeSurface");

      FML_CHECK(surface_index.second < frame_surfaces.size());
      SurfaceProducerSurface* surface =
          frame_surfaces[surface_index.second].get();
      FML_CHECK(surface != nullptr);

      sk_sp<SkSurface> sk_surface = surface->GetSkiaSurface();
      FML_CHECK(sk_surface != nullptr);
      FML_CHECK(SkISize::Make(sk_surface->width(), sk_surface->height()) ==
                frame_size_);
      SkCanvas* canvas = sk_surface->getCanvas();
      FML_CHECK(canvas != nullptr);

      const auto& layer = frame_layers_.find(surface_index.first);
      FML_CHECK(layer != frame_layers_.end());
      sk_sp<SkPicture> picture =
          layer->second.recorder->finishRecordingAsPicture();
      FML_CHECK(picture != nullptr);

      canvas->setMatrix(SkMatrix::I());
      canvas->clear(SK_ColorTRANSPARENT);
      canvas->drawPicture(picture);
      canvas->flush();
    }
  }

  // Flush deferred Skia work and inform Scenic that render targets are ready.
  {
    TRACE_EVENT0("flutter", "PresentSurfaces");

    surface_producer_.OnSurfacesPresented(std::move(frame_surfaces));
  }

  // Submit the underlying render-backend-specific frame for processing.
  frame->Submit();
}

void FuchsiaExternalViewEmbedder::EnableWireframe(bool enable) {
  session_.get()->Enqueue(
      scenic::NewSetEnableDebugViewBoundsCmd(root_view_.id(), enable));
  session_.Present();
}

void FuchsiaExternalViewEmbedder::CreateView(int64_t view_id,
                                             ViewIdCallback on_view_bound) {
  FML_CHECK(scenic_views_.find(view_id) == scenic_views_.end());

  ScenicView new_view = {
      .opacity_node = scenic::OpacityNodeHACK(session_.get()),
      .entity_node = scenic::EntityNode(session_.get()),
      .view_holder = scenic::ViewHolder(
          session_.get(),
          scenic::ToViewHolderToken(zx::eventpair((zx_handle_t)view_id)),
          "Flutter::PlatformView"),
  };
  on_view_bound(new_view.view_holder.id());

  new_view.opacity_node.SetLabel("flutter::PlatformView::OpacityMutator");
  new_view.entity_node.SetLabel("flutter::PlatformView::TransformMutator");
  new_view.opacity_node.AddChild(new_view.entity_node);
  new_view.entity_node.Attach(new_view.view_holder);
  new_view.entity_node.SetTranslation(0.f, 0.f,
                                      -kScenicZElevationBetweenLayers);

  scenic_views_.emplace(std::make_pair(view_id, std::move(new_view)));
}

void FuchsiaExternalViewEmbedder::DestroyView(int64_t view_id,
                                              ViewIdCallback on_view_unbound) {
  auto scenic_view = scenic_views_.find(view_id);
  FML_CHECK(scenic_view != scenic_views_.end());
  scenic::ResourceId resource_id = scenic_view->second.view_holder.id();

  scenic_views_.erase(scenic_view);
  on_view_unbound(resource_id);
}

void FuchsiaExternalViewEmbedder::SetViewProperties(
    int64_t view_id,
    const SkRect& occlusion_hint,
    bool hit_testable,
    bool focusable) {
  auto found = scenic_views_.find(view_id);
  FML_CHECK(found != scenic_views_.end());
  auto& view_holder = found->second;

  view_holder.pending_occlusion_hint = occlusion_hint;
  view_holder.pending_hit_testable = hit_testable;
  view_holder.pending_focusable = focusable;
}

void FuchsiaExternalViewEmbedder::Reset() {
  frame_layers_.clear();
  frame_composition_order_.clear();
  frame_size_ = SkISize::Make(0, 0);
  frame_dpr_ = 1.f;

  // Detach the root node to prepare for the next frame.
  layer_tree_node_.DetachChildren();

  // Clear images on all layers so they aren't cached unnecessarily.
  for (auto& layer : scenic_layers_) {
    layer.material.SetTexture(0);
  }
}

}  // namespace flutter_runner
