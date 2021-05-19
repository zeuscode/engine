// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flow/embedded_views.h"
#define RAPIDJSON_HAS_STDSTRING 1

#include "platform_view.h"

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include <cstring>
#include <sstream>

#include "flutter/fml/logging.h"
#include "flutter/fml/make_copyable.h"
#include "flutter/lib/ui/window/pointer_data.h"
#include "flutter/lib/ui/window/window.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/encodable_value.h"
#include "flutter/shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"
#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/include/rapidjson/writer.h"

#include "logging.h"
#include "runtime/dart/utils/inlines.h"
#include "vsync_waiter.h"

#if defined(LEGACY_FUCHSIA_EMBEDDER)
#include "flutter/lib/ui/compositing/scene_host.h"
#endif

namespace flutter_runner {

static constexpr char kFlutterPlatformChannel[] = "flutter/platform";
static constexpr char kTextInputChannel[] = "flutter/textinput";
static constexpr char kKeyEventChannel[] = "flutter/keyevent";
static constexpr char kAccessibilityChannel[] = "flutter/accessibility";
static constexpr char kFlutterPlatformViewsChannel[] = "flutter/platform_views";

// FL(77): Terminate engine if Fuchsia system FIDL connections have error.
template <class T>
void SetInterfaceErrorHandler(fidl::InterfacePtr<T>& interface,
                              std::string name) {
  interface.set_error_handler([name](zx_status_t status) {
    FML_LOG(ERROR) << "Interface error on: " << name << "status: " << status;
  });
}
template <class T>
void SetInterfaceErrorHandler(fidl::Binding<T>& binding, std::string name) {
  binding.set_error_handler([name](zx_status_t status) {
    FML_LOG(ERROR) << "Interface error on: " << name << ", status: " << status;
  });
}

PlatformView::PlatformView(
    flutter::PlatformView::Delegate& delegate,
    std::string debug_label,
    fuchsia::ui::views::ViewRef view_ref,
    flutter::TaskRunners task_runners,
    std::shared_ptr<sys::ServiceDirectory> runner_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
        parent_environment_service_provider_handle,
    fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener>
        session_listener_request,
    fidl::InterfaceHandle<fuchsia::ui::views::Focuser> focuser,
    fidl::InterfaceRequest<fuchsia::ui::input3::KeyboardListener>
        keyboard_listener_request,
    fit::closure session_listener_error_callback,
    OnEnableWireframe wireframe_enabled_callback,
    OnCreateView on_create_view_callback,
    OnUpdateView on_update_view_callback,
    OnDestroyView on_destroy_view_callback,
    OnCreateSurface on_create_surface_callback,
    std::shared_ptr<flutter::ExternalViewEmbedder> external_view_embedder,
    fml::TimeDelta vsync_offset,
    zx_handle_t vsync_event_handle)
    : flutter::PlatformView(delegate, std::move(task_runners)),
      debug_label_(std::move(debug_label)),
      view_ref_(std::move(view_ref)),
      focuser_(focuser.Bind()),
      session_listener_binding_(this, std::move(session_listener_request)),
      session_listener_error_callback_(
          std::move(session_listener_error_callback)),
      wireframe_enabled_callback_(std::move(wireframe_enabled_callback)),
      on_create_view_callback_(std::move(on_create_view_callback)),
      on_update_view_callback_(std::move(on_update_view_callback)),
      on_destroy_view_callback_(std::move(on_destroy_view_callback)),
      on_create_surface_callback_(std::move(on_create_surface_callback)),
      external_view_embedder_(external_view_embedder),
      ime_client_(this),
      vsync_offset_(std::move(vsync_offset)),
      vsync_event_handle_(vsync_event_handle),
      keyboard_listener_binding_(this, std::move(keyboard_listener_request)),
      weak_factory_(this) {
  // Register all error handlers.
  SetInterfaceErrorHandler(session_listener_binding_, "SessionListener");
  SetInterfaceErrorHandler(ime_, "Input Method Editor");
  SetInterfaceErrorHandler(text_sync_service_, "Text Sync Service");
  SetInterfaceErrorHandler(parent_environment_service_provider_,
                           "Parent Environment Service Provider");
  SetInterfaceErrorHandler(keyboard_listener_binding_,
                           "KeyboardListener Service");
  // Access the IME service.
  parent_environment_service_provider_ =
      parent_environment_service_provider_handle.Bind();

  parent_environment_service_provider_.get()->ConnectToService(
      fuchsia::ui::input::ImeService::Name_,
      text_sync_service_.NewRequest().TakeChannel());

  // Finally! Register the native platform message handlers.
  RegisterPlatformMessageHandlers();

  fuchsia::ui::views::ViewRef accessibility_view_ref;
  view_ref_.Clone(&accessibility_view_ref);
  accessibility_bridge_ = std::make_unique<AccessibilityBridge>(
      *this, runner_services, std::move(accessibility_view_ref));
}

PlatformView::~PlatformView() = default;

void PlatformView::RegisterPlatformMessageHandlers() {
  platform_message_handlers_[kFlutterPlatformChannel] =
      std::bind(&PlatformView::HandleFlutterPlatformChannelPlatformMessage,
                this, std::placeholders::_1);
  platform_message_handlers_[kTextInputChannel] =
      std::bind(&PlatformView::HandleFlutterTextInputChannelPlatformMessage,
                this, std::placeholders::_1);
  platform_message_handlers_[kAccessibilityChannel] =
      std::bind(&PlatformView::HandleAccessibilityChannelPlatformMessage, this,
                std::placeholders::_1);
  platform_message_handlers_[kFlutterPlatformViewsChannel] =
      std::bind(&PlatformView::HandleFlutterPlatformViewsChannelPlatformMessage,
                this, std::placeholders::_1);
}

// |fuchsia::ui::input::InputMethodEditorClient|
void PlatformView::DidUpdateState(
    fuchsia::ui::input::TextInputState state,
    std::unique_ptr<fuchsia::ui::input::InputEvent> input_event) {
  rapidjson::Document document;
  auto& allocator = document.GetAllocator();
  rapidjson::Value encoded_state(rapidjson::kObjectType);
  encoded_state.AddMember("text", state.text, allocator);
  encoded_state.AddMember("selectionBase", state.selection.base, allocator);
  encoded_state.AddMember("selectionExtent", state.selection.extent, allocator);
  switch (state.selection.affinity) {
    case fuchsia::ui::input::TextAffinity::UPSTREAM:
      encoded_state.AddMember("selectionAffinity",
                              rapidjson::Value("TextAffinity.upstream"),
                              allocator);
      break;
    case fuchsia::ui::input::TextAffinity::DOWNSTREAM:
      encoded_state.AddMember("selectionAffinity",
                              rapidjson::Value("TextAffinity.downstream"),
                              allocator);
      break;
  }
  encoded_state.AddMember("selectionIsDirectional", true, allocator);
  encoded_state.AddMember("composingBase", state.composing.start, allocator);
  encoded_state.AddMember("composingExtent", state.composing.end, allocator);

  rapidjson::Value args(rapidjson::kArrayType);
  args.PushBack(current_text_input_client_, allocator);
  args.PushBack(encoded_state, allocator);

  document.SetObject();
  document.AddMember("method",
                     rapidjson::Value("TextInputClient.updateEditingState"),
                     allocator);
  document.AddMember("args", args, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.GetString());
  DispatchPlatformMessage(std::make_unique<flutter::PlatformMessage>(
      kTextInputChannel,                                 // channel
      fml::MallocMapping::Copy(data, buffer.GetSize()),  // message
      nullptr)                                           // response
  );
  last_text_state_ =
      std::make_unique<fuchsia::ui::input::TextInputState>(state);
}

// |fuchsia::ui::input::InputMethodEditorClient|
void PlatformView::OnAction(fuchsia::ui::input::InputMethodAction action) {
  rapidjson::Document document;
  auto& allocator = document.GetAllocator();

  rapidjson::Value args(rapidjson::kArrayType);
  args.PushBack(current_text_input_client_, allocator);

  // Done is currently the only text input action defined by Flutter.
  args.PushBack("TextInputAction.done", allocator);

  document.SetObject();
  document.AddMember(
      "method", rapidjson::Value("TextInputClient.performAction"), allocator);
  document.AddMember("args", args, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.GetString());
  DispatchPlatformMessage(std::make_unique<flutter::PlatformMessage>(
      kTextInputChannel,                                 // channel
      fml::MallocMapping::Copy(data, buffer.GetSize()),  // message
      nullptr)                                           // response
  );
}

void PlatformView::OnScenicError(std::string error) {
  FML_LOG(ERROR) << "Session error: " << error;
  session_listener_error_callback_();
}

void PlatformView::OnScenicEvent(
    std::vector<fuchsia::ui::scenic::Event> events) {
  TRACE_EVENT0("flutter", "PlatformView::OnScenicEvent");

  std::vector<fuchsia::ui::gfx::Event> deferred_view_events;
  bool metrics_changed = false;
  for (auto& event : events) {
    switch (event.Which()) {
      case fuchsia::ui::scenic::Event::Tag::kGfx:
        switch (event.gfx().Which()) {
          case fuchsia::ui::gfx::Event::Tag::kMetrics: {
            const fuchsia::ui::gfx::Metrics& metrics =
                event.gfx().metrics().metrics;
            const float new_view_pixel_ratio = metrics.scale_x;
            if (new_view_pixel_ratio <= 0.f) {
              FML_DLOG(ERROR)
                  << "Got an invalid pixel ratio from Scenic; ignoring: "
                  << new_view_pixel_ratio;
              break;
            }

            // Avoid metrics update when possible -- it is computationally
            // expensive.
            if (view_pixel_ratio_.has_value() &&
                *view_pixel_ratio_ == new_view_pixel_ratio) {
              FML_DLOG(ERROR)
                  << "Got an identical pixel ratio from Scenic; ignoring: "
                  << new_view_pixel_ratio;
              break;
            }

            view_pixel_ratio_ = new_view_pixel_ratio;
            metrics_changed = true;
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged: {
            const fuchsia::ui::gfx::BoundingBox& bounding_box =
                event.gfx().view_properties_changed().properties.bounding_box;
            const std::pair<float, float> new_view_size = {
                std::max(bounding_box.max.x - bounding_box.min.x, 0.0f),
                std::max(bounding_box.max.y - bounding_box.min.y, 0.0f)};
            if (new_view_size.first <= 0.f || new_view_size.second <= 0.f) {
              FML_DLOG(ERROR)
                  << "Got an invalid view size from Scenic; ignoring: "
                  << new_view_size.first << " " << new_view_size.second;
              break;
            }

            // Avoid metrics update when possible -- it is computationally
            // expensive.
            if (view_logical_size_.has_value() &&
                *view_logical_size_ == new_view_size) {
              FML_DLOG(ERROR)
                  << "Got an identical view size from Scenic; ignoring: "
                  << new_view_size.first << " " << new_view_size.second;
              break;
            }

            view_logical_size_ = new_view_size;
            metrics_changed = true;
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kViewConnected:
#if defined(LEGACY_FUCHSIA_EMBEDDER)
            task_runners_.GetUITaskRunner()->PostTask(
                [view_holder_id =
                     event.gfx().view_connected().view_holder_id]() {
                  flutter::SceneHost::OnViewConnected(view_holder_id);
                });
#endif  // LEGACY_FUCHSIA_EMBEDDER
            if (!OnChildViewConnected(
                    event.gfx().view_connected().view_holder_id)) {
              deferred_view_events.push_back(std::move(event.gfx()));
            }
            break;
          case fuchsia::ui::gfx::Event::Tag::kViewDisconnected:
#if defined(LEGACY_FUCHSIA_EMBEDDER)
            task_runners_.GetUITaskRunner()->PostTask(
                [view_holder_id =
                     event.gfx().view_disconnected().view_holder_id]() {
                  flutter::SceneHost::OnViewDisconnected(view_holder_id);
                });
#endif  // LEGACY_FUCHSIA_EMBEDDER
            if (!OnChildViewDisconnected(
                    event.gfx().view_disconnected().view_holder_id)) {
              deferred_view_events.push_back(std::move(event.gfx()));
            }
            break;
          case fuchsia::ui::gfx::Event::Tag::kViewStateChanged:
#if defined(LEGACY_FUCHSIA_EMBEDDER)
            task_runners_.GetUITaskRunner()->PostTask(
                [view_holder_id =
                     event.gfx().view_state_changed().view_holder_id,
                 state =
                     event.gfx().view_state_changed().state.is_rendering]() {
                  flutter::SceneHost::OnViewStateChanged(view_holder_id, state);
                });
#endif  // LEGACY_FUCHSIA_EMBEDDER
            if (!OnChildViewStateChanged(
                    event.gfx().view_state_changed().view_holder_id,
                    event.gfx().view_state_changed().state.is_rendering)) {
              deferred_view_events.push_back(std::move(event.gfx()));
            }
            break;
          case fuchsia::ui::gfx::Event::Tag::Invalid:
            FML_DCHECK(false) << "Flutter PlatformView::OnScenicEvent: Got "
                                 "an invalid GFX event.";
            break;
          default:
            // We don't care about some event types, so not handling them is OK.
            break;
        }
        break;
      case fuchsia::ui::scenic::Event::Tag::kInput:
        switch (event.input().Which()) {
          case fuchsia::ui::input::InputEvent::Tag::kFocus: {
            OnHandleFocusEvent(event.input().focus());
            break;
          }
          case fuchsia::ui::input::InputEvent::Tag::kPointer: {
            OnHandlePointerEvent(event.input().pointer());
            break;
          }
          case fuchsia::ui::input::InputEvent::Tag::kKeyboard: {
            // All devices should receive key events via input3.KeyboardListener
            // instead.
            FML_LOG(WARNING) << "Keyboard event from Scenic: ignored";
            break;
          }
          case fuchsia::ui::input::InputEvent::Tag::Invalid: {
            FML_DCHECK(false)
                << "Flutter PlatformView::OnScenicEvent: Got an invalid INPUT "
                   "event.";
          }
        }
        break;
      default: {
        break;
      }
    }
  }

  // If some View events went unmatched, try processing them again one more time
  // in case they arrived out-of-order with the View creation callback.
  if (!deferred_view_events.empty()) {
    task_runners_.GetPlatformTaskRunner()->PostTask(fml::MakeCopyable(
        [weak = weak_factory_.GetWeakPtr(),
         deferred_view_events = std::move(deferred_view_events)]() {
          if (!weak) {
            FML_LOG(WARNING)
                << "PlatformView already destroyed when "
                   "processing deferred view events; dropping events.";
            return;
          }

          for (const auto& event : deferred_view_events) {
            switch (event.Which()) {
              case fuchsia::ui::gfx::Event::Tag::kViewConnected: {
                bool view_found = weak->OnChildViewConnected(
                    event.view_connected().view_holder_id);
                FML_DCHECK(view_found);
                break;
              }
              case fuchsia::ui::gfx::Event::Tag::kViewDisconnected: {
                bool view_found = weak->OnChildViewDisconnected(
                    event.view_disconnected().view_holder_id);
                FML_DCHECK(view_found);
                break;
              }
              case fuchsia::ui::gfx::Event::Tag::kViewStateChanged: {
                bool view_found = weak->OnChildViewStateChanged(
                    event.view_state_changed().view_holder_id,
                    event.view_state_changed().state.is_rendering);
                FML_DCHECK(view_found);
                break;
              }
              default:
                FML_DCHECK(false) << "Flutter PlatformView::OnScenicEvent: Got "
                                     "an invalid deferred GFX event.";
                break;
            }
          }
        }));
  }

  // If any of the viewport metrics changed, inform the engine now.
  if (view_pixel_ratio_.has_value() && view_logical_size_.has_value() &&
      metrics_changed) {
    const float pixel_ratio = *view_pixel_ratio_;
    const std::pair<float, float> logical_size = *view_logical_size_;
    SetViewportMetrics({
        pixel_ratio,                        // device_pixel_ratio
        logical_size.first * pixel_ratio,   // physical_width
        logical_size.second * pixel_ratio,  // physical_height
        0.0f,                               // physical_padding_top
        0.0f,                               // physical_padding_right
        0.0f,                               // physical_padding_bottom
        0.0f,                               // physical_padding_left
        0.0f,                               // physical_view_inset_top
        0.0f,                               // physical_view_inset_right
        0.0f,                               // physical_view_inset_bottom
        0.0f,                               // physical_view_inset_left
        0.0f,  // p_physical_system_gesture_inset_top
        0.0f,  // p_physical_system_gesture_inset_right
        0.0f,  // p_physical_system_gesture_inset_bottom
        0.0f,  // p_physical_system_gesture_inset_left
    });
  }
}

bool PlatformView::OnChildViewConnected(scenic::ResourceId view_holder_id) {
  auto view_id_mapping = child_view_ids_.find(view_holder_id);
  if (view_id_mapping == child_view_ids_.end()) {
    return false;
  }

  std::ostringstream out;
  out << "{"
      << "\"method\":\"View.viewConnected\","
      << "\"args\":{"
      << "  \"viewId\":" << view_id_mapping->second  // ViewHolderToken handle
      << "  }"
      << "}";
  auto call = out.str();

  std::unique_ptr<flutter::PlatformMessage> message =
      std::make_unique<flutter::PlatformMessage>(
          "flutter/platform_views",
          fml::MallocMapping::Copy(call.c_str(), call.size()), nullptr);
  DispatchPlatformMessage(std::move(message));

  return true;
}

bool PlatformView::OnChildViewDisconnected(scenic::ResourceId view_holder_id) {
  auto view_id_mapping = child_view_ids_.find(view_holder_id);
  if (view_id_mapping == child_view_ids_.end()) {
    return false;
  }

  std::ostringstream out;
  out << "{"
      << "\"method\":\"View.viewDisconnected\","
      << "\"args\":{"
      << "  \"viewId\":" << view_id_mapping->second  // ViewHolderToken handle
      << "  }"
      << "}";
  auto call = out.str();

  std::unique_ptr<flutter::PlatformMessage> message =
      std::make_unique<flutter::PlatformMessage>(
          "flutter/platform_views",
          fml::MallocMapping::Copy(call.c_str(), call.size()), nullptr);
  DispatchPlatformMessage(std::move(message));

  return true;
}

bool PlatformView::OnChildViewStateChanged(scenic::ResourceId view_holder_id,
                                           bool is_rendering) {
  auto view_id_mapping = child_view_ids_.find(view_holder_id);
  if (view_id_mapping == child_view_ids_.end()) {
    return false;
  }

  const std::string is_rendering_str = is_rendering ? "true" : "false";
  std::ostringstream out;
  out << "{"
      << "\"method\":\"View.viewStateChanged\","
      << "\"args\":{"
      << "  \"viewId\":" << view_id_mapping->second << ","  // ViewHolderToken
      << "  \"is_rendering\":" << is_rendering_str << ","   // IsViewRendering
      << "  \"state\":" << is_rendering_str                 // IsViewRendering
      << "  }"
      << "}";
  auto call = out.str();

  std::unique_ptr<flutter::PlatformMessage> message =
      std::make_unique<flutter::PlatformMessage>(
          "flutter/platform_views",
          fml::MallocMapping::Copy(call.c_str(), call.size()), nullptr);
  DispatchPlatformMessage(std::move(message));

  return true;
}

static flutter::PointerData::Change GetChangeFromPointerEventPhase(
    fuchsia::ui::input::PointerEventPhase phase) {
  switch (phase) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      return flutter::PointerData::Change::kAdd;
    case fuchsia::ui::input::PointerEventPhase::HOVER:
      return flutter::PointerData::Change::kHover;
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      return flutter::PointerData::Change::kDown;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      return flutter::PointerData::Change::kMove;
    case fuchsia::ui::input::PointerEventPhase::UP:
      return flutter::PointerData::Change::kUp;
    case fuchsia::ui::input::PointerEventPhase::REMOVE:
      return flutter::PointerData::Change::kRemove;
    case fuchsia::ui::input::PointerEventPhase::CANCEL:
      return flutter::PointerData::Change::kCancel;
    default:
      return flutter::PointerData::Change::kCancel;
  }
}

static flutter::PointerData::DeviceKind GetKindFromPointerType(
    fuchsia::ui::input::PointerEventType type) {
  switch (type) {
    case fuchsia::ui::input::PointerEventType::TOUCH:
      return flutter::PointerData::DeviceKind::kTouch;
    case fuchsia::ui::input::PointerEventType::MOUSE:
      return flutter::PointerData::DeviceKind::kMouse;
    default:
      return flutter::PointerData::DeviceKind::kTouch;
  }
}

// TODO(SCN-1278): Remove this.
// Turns two floats (high bits, low bits) into a 64-bit uint.
static trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

bool PlatformView::OnHandlePointerEvent(
    const fuchsia::ui::input::PointerEvent& pointer) {
  TRACE_EVENT0("flutter", "PlatformView::OnHandlePointerEvent");

  // TODO(SCN-1278): Use proper trace_id for tracing flow.
  trace_flow_id_t trace_id =
      PointerTraceHACK(pointer.radius_major, pointer.radius_minor);
  TRACE_FLOW_END("input", "dispatch_event_to_client", trace_id);

  const float pixel_ratio =
      view_pixel_ratio_.has_value() ? *view_pixel_ratio_ : 0.f;

  flutter::PointerData pointer_data;
  pointer_data.Clear();
  pointer_data.time_stamp = pointer.event_time / 1000;
  pointer_data.change = GetChangeFromPointerEventPhase(pointer.phase);
  pointer_data.kind = GetKindFromPointerType(pointer.type);
  pointer_data.device = pointer.pointer_id;
  // Pointer events are in logical pixels, so scale to physical.
  pointer_data.physical_x = pointer.x * pixel_ratio;
  pointer_data.physical_y = pointer.y * pixel_ratio;
  // Buttons are single bit values starting with kMousePrimaryButton = 1.
  pointer_data.buttons = static_cast<uint64_t>(pointer.buttons);

  switch (pointer_data.change) {
    case flutter::PointerData::Change::kDown:
      down_pointers_.insert(pointer_data.device);
      break;
    case flutter::PointerData::Change::kCancel:
    case flutter::PointerData::Change::kUp:
      down_pointers_.erase(pointer_data.device);
      break;
    case flutter::PointerData::Change::kMove:
      if (down_pointers_.count(pointer_data.device) == 0) {
        pointer_data.change = flutter::PointerData::Change::kHover;
      }
      break;
    case flutter::PointerData::Change::kAdd:
      if (down_pointers_.count(pointer_data.device) != 0) {
        FML_DLOG(ERROR) << "Received add event for down pointer.";
      }
      break;
    case flutter::PointerData::Change::kRemove:
      if (down_pointers_.count(pointer_data.device) != 0) {
        FML_DLOG(ERROR) << "Received remove event for down pointer.";
      }
      break;
    case flutter::PointerData::Change::kHover:
      if (down_pointers_.count(pointer_data.device) != 0) {
        FML_DLOG(ERROR) << "Received hover event for down pointer.";
      }
      break;
  }

  auto packet = std::make_unique<flutter::PointerDataPacket>(1);
  packet->SetPointerData(0, pointer_data);
  DispatchPointerDataPacket(std::move(packet));
  return true;
}

// |fuchsia::ui:input3::KeyboardListener|
void PlatformView::OnKeyEvent(
    fuchsia::ui::input3::KeyEvent key_event,
    fuchsia::ui::input3::KeyboardListener::OnKeyEventCallback callback) {
  const char* type = nullptr;
  switch (key_event.type()) {
    case fuchsia::ui::input3::KeyEventType::PRESSED:
      type = "keydown";
      break;
    case fuchsia::ui::input3::KeyEventType::RELEASED:
      type = "keyup";
      break;
    case fuchsia::ui::input3::KeyEventType::SYNC:
      // What, if anything, should happen here?
    case fuchsia::ui::input3::KeyEventType::CANCEL:
      // What, if anything, should happen here?
    default:
      break;
  }
  if (type == nullptr) {
    FML_DLOG(ERROR) << "Unknown key event phase.";
    callback(fuchsia::ui::input3::KeyEventStatus::NOT_HANDLED);
    return;
  }
  keyboard_.ConsumeEvent(std::move(key_event));

  rapidjson::Document document;
  auto& allocator = document.GetAllocator();
  document.SetObject();
  document.AddMember("type", rapidjson::Value(type, strlen(type)), allocator);
  document.AddMember("keymap", rapidjson::Value("fuchsia"), allocator);
  document.AddMember("hidUsage", keyboard_.LastHIDUsage(), allocator);
  document.AddMember("codePoint", keyboard_.LastCodePoint(), allocator);
  document.AddMember("modifiers", keyboard_.Modifiers(), allocator);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);

  const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer.GetString());
  DispatchPlatformMessage(std::make_unique<flutter::PlatformMessage>(
      kKeyEventChannel,                                  // channel
      fml::MallocMapping::Copy(data, buffer.GetSize()),  // data
      nullptr)                                           // response
  );
  callback(fuchsia::ui::input3::KeyEventStatus::HANDLED);
}

bool PlatformView::OnHandleFocusEvent(
    const fuchsia::ui::input::FocusEvent& focus) {
  // Ensure last_text_state_ is set to make sure Flutter actually wants an IME.
  if (focus.focused && last_text_state_ != nullptr) {
    ActivateIme();
    return true;
  } else if (!focus.focused) {
    DeactivateIme();
    return true;
  }
  return false;
}

void PlatformView::ActivateIme() {
  DEBUG_CHECK(last_text_state_ != nullptr, LOG_TAG, "");

  text_sync_service_->GetInputMethodEditor(
      fuchsia::ui::input::KeyboardType::TEXT,       // keyboard type
      fuchsia::ui::input::InputMethodAction::DONE,  // input method action
      *last_text_state_,                            // initial state
      ime_client_.NewBinding(),                     // client
      ime_.NewRequest()                             // editor
  );
}

void PlatformView::DeactivateIme() {
  if (ime_) {
    text_sync_service_->HideKeyboard();
    ime_ = nullptr;
  }
  if (ime_client_.is_bound()) {
    ime_client_.Unbind();
  }
}

// |flutter::PlatformView|
std::unique_ptr<flutter::VsyncWaiter> PlatformView::CreateVSyncWaiter() {
  return std::make_unique<flutter_runner::VsyncWaiter>(
      debug_label_, vsync_event_handle_, task_runners_, vsync_offset_);
}

// |flutter::PlatformView|
std::unique_ptr<flutter::Surface> PlatformView::CreateRenderingSurface() {
  return on_create_surface_callback_ ? on_create_surface_callback_() : nullptr;
}

// |flutter::PlatformView|
std::shared_ptr<flutter::ExternalViewEmbedder>
PlatformView::CreateExternalViewEmbedder() {
  return external_view_embedder_;
}

// |flutter::PlatformView|
void PlatformView::HandlePlatformMessage(
    std::unique_ptr<flutter::PlatformMessage> message) {
  if (!message) {
    return;
  }
  const std::string channel = message->channel();
  auto found = platform_message_handlers_.find(channel);
  if (found == platform_message_handlers_.end()) {
    const bool already_errored = unregistered_channels_.count(channel);
    if (!already_errored) {
      FML_LOG(INFO)
          << "Platform view received message on channel '" << message->channel()
          << "' with no registered handler. And empty response will be "
             "generated. Please implement the native message handler. This "
             "message will appear only once per channel.";
      unregistered_channels_.insert(channel);
    }
    flutter::PlatformView::HandlePlatformMessage(std::move(message));
    return;
  }
  found->second(std::move(message));
}

// |flutter::PlatformView|
// |flutter_runner::AccessibilityBridge::Delegate|
void PlatformView::SetSemanticsEnabled(bool enabled) {
  flutter::PlatformView::SetSemanticsEnabled(enabled);
  if (enabled) {
    SetAccessibilityFeatures(static_cast<int32_t>(
        flutter::AccessibilityFeatureFlag::kAccessibleNavigation));
  } else {
    SetAccessibilityFeatures(0);
  }
}

// |flutter::PlatformView|
// |flutter_runner::AccessibilityBridge::Delegate|
void PlatformView::DispatchSemanticsAction(int32_t node_id,
                                           flutter::SemanticsAction action) {
  flutter::PlatformView::DispatchSemanticsAction(node_id, action, {});
}

// |flutter::PlatformView|
void PlatformView::UpdateSemantics(
    flutter::SemanticsNodeUpdates update,
    flutter::CustomAccessibilityActionUpdates actions) {
  const float pixel_ratio =
      view_pixel_ratio_.has_value() ? *view_pixel_ratio_ : 0.f;

  accessibility_bridge_->AddSemanticsNodeUpdate(update, pixel_ratio);
}

// Channel handler for kAccessibilityChannel
void PlatformView::HandleAccessibilityChannelPlatformMessage(
    std::unique_ptr<flutter::PlatformMessage> message) {
  FML_DCHECK(message->channel() == kAccessibilityChannel);

  const flutter::StandardMessageCodec& standard_message_codec =
      flutter::StandardMessageCodec::GetInstance(nullptr);
  std::unique_ptr<flutter::EncodableValue> decoded =
      standard_message_codec.DecodeMessage(message->data().GetMapping(),
                                           message->data().GetSize());

  flutter::EncodableMap map = std::get<flutter::EncodableMap>(*decoded);
  std::string type =
      std::get<std::string>(map.at(flutter::EncodableValue("type")));
  if (type == "announce") {
    flutter::EncodableMap data_map = std::get<flutter::EncodableMap>(
        map.at(flutter::EncodableValue("data")));
    std::string text =
        std::get<std::string>(data_map.at(flutter::EncodableValue("message")));

    accessibility_bridge_->RequestAnnounce(text);
  }

  message->response()->CompleteEmpty();
}

// Channel handler for kFlutterPlatformChannel
void PlatformView::HandleFlutterPlatformChannelPlatformMessage(
    std::unique_ptr<flutter::PlatformMessage> message) {
  FML_DCHECK(message->channel() == kFlutterPlatformChannel);
  const auto& data = message->data();
  rapidjson::Document document;
  document.Parse(reinterpret_cast<const char*>(data.GetMapping()),
                 data.GetSize());
  if (document.HasParseError() || !document.IsObject()) {
    return;
  }

  auto root = document.GetObject();
  auto method = root.FindMember("method");
  if (method == root.MemberEnd() || !method->value.IsString()) {
    return;
  }

  // Fuchsia does not handle any platform messages at this time.
  message->response()->CompleteEmpty();
}

// Channel handler for kTextInputChannel
void PlatformView::HandleFlutterTextInputChannelPlatformMessage(
    std::unique_ptr<flutter::PlatformMessage> message) {
  FML_DCHECK(message->channel() == kTextInputChannel);
  const auto& data = message->data();
  rapidjson::Document document;
  document.Parse(reinterpret_cast<const char*>(data.GetMapping()),
                 data.GetSize());
  if (document.HasParseError() || !document.IsObject()) {
    return;
  }
  auto root = document.GetObject();
  auto method = root.FindMember("method");
  if (method == root.MemberEnd() || !method->value.IsString()) {
    return;
  }

  if (method->value == "TextInput.show") {
    if (ime_) {
      text_sync_service_->ShowKeyboard();
    }
  } else if (method->value == "TextInput.hide") {
    if (ime_) {
      text_sync_service_->HideKeyboard();
    }
  } else if (method->value == "TextInput.setClient") {
    current_text_input_client_ = 0;
    DeactivateIme();
    auto args = root.FindMember("args");
    if (args == root.MemberEnd() || !args->value.IsArray() ||
        args->value.Size() != 2)
      return;
    const auto& configuration = args->value[1];
    if (!configuration.IsObject()) {
      return;
    }
    // TODO(abarth): Read the keyboard type from the configuration.
    current_text_input_client_ = args->value[0].GetInt();

    auto initial_text_input_state = fuchsia::ui::input::TextInputState{};
    initial_text_input_state.text = "";
    last_text_state_ = std::make_unique<fuchsia::ui::input::TextInputState>(
        initial_text_input_state);
    ActivateIme();
  } else if (method->value == "TextInput.setEditingState") {
    if (ime_) {
      auto args_it = root.FindMember("args");
      if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
        return;
      }
      const auto& args = args_it->value;
      fuchsia::ui::input::TextInputState state;
      state.text = "";
      // TODO(abarth): Deserialize state.
      auto text = args.FindMember("text");
      if (text != args.MemberEnd() && text->value.IsString())
        state.text = text->value.GetString();
      auto selection_base = args.FindMember("selectionBase");
      if (selection_base != args.MemberEnd() && selection_base->value.IsInt())
        state.selection.base = selection_base->value.GetInt();
      auto selection_extent = args.FindMember("selectionExtent");
      if (selection_extent != args.MemberEnd() &&
          selection_extent->value.IsInt())
        state.selection.extent = selection_extent->value.GetInt();
      auto selection_affinity = args.FindMember("selectionAffinity");
      if (selection_affinity != args.MemberEnd() &&
          selection_affinity->value.IsString() &&
          selection_affinity->value == "TextAffinity.upstream")
        state.selection.affinity = fuchsia::ui::input::TextAffinity::UPSTREAM;
      else
        state.selection.affinity = fuchsia::ui::input::TextAffinity::DOWNSTREAM;
      // We ignore selectionIsDirectional because that concept doesn't exist on
      // Fuchsia.
      auto composing_base = args.FindMember("composingBase");
      if (composing_base != args.MemberEnd() && composing_base->value.IsInt())
        state.composing.start = composing_base->value.GetInt();
      auto composing_extent = args.FindMember("composingExtent");
      if (composing_extent != args.MemberEnd() &&
          composing_extent->value.IsInt())
        state.composing.end = composing_extent->value.GetInt();
      ime_->SetState(std::move(state));
    }
  } else if (method->value == "TextInput.clearClient") {
    current_text_input_client_ = 0;
    last_text_state_ = nullptr;
    DeactivateIme();
  } else {
    FML_DLOG(ERROR) << "Unknown " << message->channel() << " method "
                    << method->value.GetString();
  }
}

void PlatformView::HandleFlutterPlatformViewsChannelPlatformMessage(
    std::unique_ptr<flutter::PlatformMessage> message) {
  FML_DCHECK(message->channel() == kFlutterPlatformViewsChannel);
  const auto& data = message->data();
  rapidjson::Document document;
  document.Parse(reinterpret_cast<const char*>(data.GetMapping()),
                 data.GetSize());
  if (document.HasParseError() || !document.IsObject()) {
    FML_LOG(ERROR) << "Could not parse document";
    return;
  }
  auto root = document.GetObject();
  auto method = root.FindMember("method");
  if (method == root.MemberEnd() || !method->value.IsString()) {
    return;
  }

  if (method->value == "View.enableWireframe") {
    auto args_it = root.FindMember("args");
    if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
      FML_LOG(ERROR) << "No arguments found.";
      return;
    }
    const auto& args = args_it->value;

    auto enable = args.FindMember("enable");
    if (!enable->value.IsBool()) {
      FML_LOG(ERROR) << "Argument 'enable' is not a bool";
      return;
    }

    wireframe_enabled_callback_(enable->value.GetBool());
  } else if (method->value == "View.create") {
    auto args_it = root.FindMember("args");
    if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
      FML_LOG(ERROR) << "No arguments found.";
      return;
    }
    const auto& args = args_it->value;

    auto view_id = args.FindMember("viewId");
    if (!view_id->value.IsUint64()) {
      FML_LOG(ERROR) << "Argument 'viewId' is not a int64";
      return;
    }

    auto hit_testable = args.FindMember("hitTestable");
    if (!hit_testable->value.IsBool()) {
      FML_LOG(ERROR) << "Argument 'hitTestable' is not a bool";
      return;
    }

    auto focusable = args.FindMember("focusable");
    if (!focusable->value.IsBool()) {
      FML_LOG(ERROR) << "Argument 'focusable' is not a bool";
      return;
    }

    const int64_t view_id_raw = view_id->value.GetUint64();
    auto on_view_bound =
        [weak = weak_factory_.GetWeakPtr(),
         platform_task_runner = task_runners_.GetPlatformTaskRunner(),
         view_id = view_id_raw](scenic::ResourceId resource_id) {
          platform_task_runner->PostTask([weak, view_id, resource_id]() {
            if (!weak) {
              FML_LOG(WARNING)
                  << "ViewHolder bound to PlatformView after PlatformView was "
                     "destroyed; ignoring.";
              return;
            }

            FML_DCHECK(weak->child_view_ids_.count(resource_id) == 0);
            weak->child_view_ids_[resource_id] = view_id;
          });
        };
    on_create_view_callback_(view_id_raw, std::move(on_view_bound),
                             hit_testable->value.GetBool(),
                             focusable->value.GetBool());

    // The client is waiting for view creation. Send an empty response back
    // to signal the view was created.
    if (message->response().get()) {
      message->response()->Complete(
          std::make_unique<fml::NonOwnedMapping>((const uint8_t*)"[0]", 3u));
    }
  } else if (method->value == "View.update") {
    auto args_it = root.FindMember("args");
    if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
      FML_LOG(ERROR) << "No arguments found.";
      return;
    }
    const auto& args = args_it->value;

    auto view_id = args.FindMember("viewId");
    if (!view_id->value.IsUint64()) {
      FML_LOG(ERROR) << "Argument 'viewId' is not a int64";
      return;
    }

    auto hit_testable = args.FindMember("hitTestable");
    if (!hit_testable->value.IsBool()) {
      FML_LOG(ERROR) << "Argument 'hitTestable' is not a bool";
      return;
    }

    auto focusable = args.FindMember("focusable");
    if (!focusable->value.IsBool()) {
      FML_LOG(ERROR) << "Argument 'focusable' is not a bool";
      return;
    }

    SkRect view_occlusion_hint_raw = SkRect::MakeEmpty();
    auto view_occlusion_hint = args.FindMember("viewOcclusionHintLTRB");
    if (view_occlusion_hint != args.MemberEnd()) {
      if (view_occlusion_hint->value.IsArray()) {
        const auto& view_occlusion_hint_array =
            view_occlusion_hint->value.GetArray();
        if (view_occlusion_hint_array.Size() == 4) {
          bool parse_error = false;
          for (int i = 0; i < 4; i++) {
            auto& array_val = view_occlusion_hint_array[i];
            if (!array_val.IsDouble()) {
              FML_LOG(ERROR) << "Argument 'viewOcclusionHintLTRB' element " << i
                             << " is not a double";
              parse_error = true;
              break;
            }
          }

          if (!parse_error) {
            view_occlusion_hint_raw =
                SkRect::MakeLTRB(view_occlusion_hint_array[0].GetDouble(),
                                 view_occlusion_hint_array[1].GetDouble(),
                                 view_occlusion_hint_array[2].GetDouble(),
                                 view_occlusion_hint_array[3].GetDouble());
          }
        } else {
          FML_LOG(ERROR)
              << "Argument 'viewOcclusionHintLTRB' expected size 4; got "
              << view_occlusion_hint_array.Size();
        }
      } else {
        FML_LOG(ERROR)
            << "Argument 'viewOcclusionHintLTRB' is not a double array";
      }
    } else {
      FML_LOG(WARNING) << "Argument 'viewOcclusionHintLTRB' is missing";
    }

    on_update_view_callback_(
        view_id->value.GetUint64(), view_occlusion_hint_raw,
        hit_testable->value.GetBool(), focusable->value.GetBool());
  } else if (method->value == "View.dispose") {
    auto args_it = root.FindMember("args");
    if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
      FML_LOG(ERROR) << "No arguments found.";
      return;
    }
    const auto& args = args_it->value;

    auto view_id = args.FindMember("viewId");
    if (!view_id->value.IsUint64()) {
      FML_LOG(ERROR) << "Argument 'viewId' is not a int64";
      return;
    }

    const int64_t view_id_raw = view_id->value.GetUint64();
    auto on_view_unbound =
        [weak = weak_factory_.GetWeakPtr(),
         platform_task_runner = task_runners_.GetPlatformTaskRunner()](
            scenic::ResourceId resource_id) {
          platform_task_runner->PostTask([weak, resource_id]() {
            if (!weak) {
              FML_LOG(WARNING)
                  << "ViewHolder unbound from PlatformView after PlatformView"
                     "was destroyed; ignoring.";
              return;
            }

            FML_DCHECK(weak->child_view_ids_.count(resource_id) == 1);
            weak->child_view_ids_.erase(resource_id);
          });
        };
    on_destroy_view_callback_(view_id_raw, std::move(on_view_unbound));
  } else if (method->value == "View.requestFocus") {
    auto args_it = root.FindMember("args");
    if (args_it == root.MemberEnd() || !args_it->value.IsObject()) {
      FML_LOG(ERROR) << "No arguments found.";
      return;
    }
    const auto& args = args_it->value;

    auto view_ref = args.FindMember("viewRef");
    if (!view_ref->value.IsUint64()) {
      FML_LOG(ERROR) << "Argument 'viewRef' is not a int64";
      return;
    }

    zx_handle_t handle = view_ref->value.GetUint64();
    zx_handle_t out_handle;
    zx_status_t status =
        zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &out_handle);
    if (status != ZX_OK) {
      FML_LOG(ERROR) << "Argument 'viewRef' is not valid";
      return;
    }
    auto ref = fuchsia::ui::views::ViewRef({
        .reference = zx::eventpair(out_handle),
    });
    focuser_->RequestFocus(
        std::move(ref),
        [view_ref = view_ref->value.GetUint64(), message = std::move(message)](
            fuchsia::ui::views::Focuser_RequestFocus_Result result) {
          if (message->response().get()) {
            int result_code =
                result.is_err()
                    ? static_cast<
                          std::underlying_type_t<fuchsia::ui::views::Error>>(
                          result.err())
                    : 0;

            std::ostringstream out;
            out << "[" << result_code << "]";
            message->response()->Complete(
                std::make_unique<fml::NonOwnedMapping>(
                    (const uint8_t*)out.str().c_str(), out.str().length()));
          }
        });
  } else {
    FML_DLOG(ERROR) << "Unknown " << message->channel() << " method "
                    << method->value.GetString();
  }
}

}  // namespace flutter_runner
