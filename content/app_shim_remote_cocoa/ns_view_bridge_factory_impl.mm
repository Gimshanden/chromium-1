// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/remote_cocoa.h"

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/no_destructor.h"
#include "content/app_shim_remote_cocoa/render_widget_host_ns_view_bridge.h"
#include "content/app_shim_remote_cocoa/render_widget_host_ns_view_host_helper.h"
#include "content/app_shim_remote_cocoa/web_contents_ns_view_bridge.h"
#include "content/browser/renderer_host/input/web_input_event_builders_mac.h"
#include "content/common/render_widget_host_ns_view.mojom.h"
#include "content/common/web_contents_ns_view_bridge.mojom.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "mojo/public/cpp/bindings/strong_associated_binding.h"
#include "ui/accelerated_widget_mac/window_resize_helper_mac.h"
#include "ui/base/cocoa/remote_accessibility_api.h"

namespace remote_cocoa {

namespace {

class RenderWidgetHostNSViewBridgeOwner
    : public RenderWidgetHostNSViewHostHelper {
 public:
  explicit RenderWidgetHostNSViewBridgeOwner(
      mojom::RenderWidgetHostNSViewHostAssociatedPtr client,
      mojom::RenderWidgetHostNSViewAssociatedRequest bridge_request)
      : host_(std::move(client)) {
    bridge_ = std::make_unique<remote_cocoa::RenderWidgetHostNSViewBridge>(
        host_.get(), this);
    bridge_->BindRequest(std::move(bridge_request));
    host_.set_connection_error_handler(
        base::BindOnce(&RenderWidgetHostNSViewBridgeOwner::OnConnectionError,
                       base::Unretained(this)));
  }

 private:
  void OnConnectionError() { delete this; }

  std::unique_ptr<content::InputEvent> TranslateEvent(
      const blink::WebInputEvent& web_event) {
    return std::make_unique<content::InputEvent>(web_event, ui::LatencyInfo());
  }

  // RenderWidgetHostNSViewHostHelper implementation.
  id GetRootBrowserAccessibilityElement() override {
    // The RenderWidgetHostViewCocoa in the app shim process does not
    // participate in the accessibility tree. Only the instance in the browser
    // process does.
    return nil;
  }
  id GetFocusedBrowserAccessibilityElement() override {
    // See above.
    return nil;
  }
  void SetAccessibilityWindow(NSWindow* window) override {
    host_->SetRemoteAccessibilityWindowToken(
        ui::RemoteAccessibility::GetTokenForLocalElement(window));
  }

  void ForwardKeyboardEvent(const content::NativeWebKeyboardEvent& key_event,
                            const ui::LatencyInfo& latency_info) override {
    const blink::WebKeyboardEvent* web_event =
        static_cast<const blink::WebKeyboardEvent*>(&key_event);
    std::unique_ptr<content::InputEvent> input_event =
        std::make_unique<content::InputEvent>(*web_event, latency_info);
    host_->ForwardKeyboardEvent(std::move(input_event),
                                key_event.skip_in_browser);
  }
  void ForwardKeyboardEventWithCommands(
      const content::NativeWebKeyboardEvent& key_event,
      const ui::LatencyInfo& latency_info,
      const std::vector<content::EditCommand>& commands) override {
    const blink::WebKeyboardEvent* web_event =
        static_cast<const blink::WebKeyboardEvent*>(&key_event);
    std::unique_ptr<content::InputEvent> input_event =
        std::make_unique<content::InputEvent>(*web_event, latency_info);
    host_->ForwardKeyboardEventWithCommands(
        std::move(input_event), key_event.skip_in_browser, commands);
  }
  void RouteOrProcessMouseEvent(
      const blink::WebMouseEvent& web_event) override {
    host_->RouteOrProcessMouseEvent(TranslateEvent(web_event));
  }
  void RouteOrProcessTouchEvent(
      const blink::WebTouchEvent& web_event) override {
    host_->RouteOrProcessTouchEvent(TranslateEvent(web_event));
  }
  void RouteOrProcessWheelEvent(
      const blink::WebMouseWheelEvent& web_event) override {
    host_->RouteOrProcessWheelEvent(TranslateEvent(web_event));
  }
  void ForwardMouseEvent(const blink::WebMouseEvent& web_event) override {
    host_->ForwardMouseEvent(TranslateEvent(web_event));
  }
  void ForwardWheelEvent(const blink::WebMouseWheelEvent& web_event) override {
    host_->ForwardWheelEvent(TranslateEvent(web_event));
  }
  void GestureBegin(blink::WebGestureEvent begin_event,
                    bool is_synthetically_injected) override {
    // The gesture type is not yet known, but assign a type to avoid
    // serialization asserts (the type will be stripped on the other side).
    begin_event.SetType(blink::WebInputEvent::kGestureScrollBegin);
    host_->GestureBegin(TranslateEvent(begin_event), is_synthetically_injected);
  }
  void GestureUpdate(blink::WebGestureEvent update_event) override {
    host_->GestureUpdate(TranslateEvent(update_event));
  }
  void GestureEnd(blink::WebGestureEvent end_event) override {
    host_->GestureEnd(TranslateEvent(end_event));
  }
  void SmartMagnify(const blink::WebGestureEvent& web_event) override {
    host_->SmartMagnify(TranslateEvent(web_event));
  }

  mojom::RenderWidgetHostNSViewHostAssociatedPtr host_;
  std::unique_ptr<RenderWidgetHostNSViewBridge> bridge_;
  base::scoped_nsobject<NSAccessibilityRemoteUIElement>
      remote_accessibility_element_;

  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostNSViewBridgeOwner);
};
}

void CreateRenderWidgetHostNSView(
    mojo::ScopedInterfaceEndpointHandle host_handle,
    mojo::ScopedInterfaceEndpointHandle view_request_handle) {
  // Cast from the stub interface to the mojom::RenderWidgetHostNSViewHost
  // and mojom::RenderWidgetHostNSView private interfaces.
  // TODO(ccameron): Remove the need for this cast.
  // https://crbug.com/888290
  mojom::RenderWidgetHostNSViewHostAssociatedPtr host(
      mojo::AssociatedInterfacePtrInfo<mojom::RenderWidgetHostNSViewHost>(
          std::move(host_handle), 0));
  mojom::RenderWidgetHostNSViewAssociatedRequest ns_view_request(
      std::move(view_request_handle));

  // Create a RenderWidgetHostNSViewBridgeOwner. The resulting object will be
  // destroyed when its underlying pipe is closed.
  ignore_result(new RenderWidgetHostNSViewBridgeOwner(
      std::move(host), std::move(ns_view_request)));
}

void CreateWebContentsNSView(
    uint64_t view_id,
    mojo::ScopedInterfaceEndpointHandle host_handle,
    mojo::ScopedInterfaceEndpointHandle view_request_handle) {
  mojom::WebContentsNSViewHostAssociatedPtr host(
      mojo::AssociatedInterfacePtrInfo<mojom::WebContentsNSViewHost>(
          std::move(host_handle), 0));
  mojom::WebContentsNSViewAssociatedRequest ns_view_request(
      std::move(view_request_handle));
  // Note that the resulting object will be destroyed when its underlying pipe
  // is closed.
  mojo::MakeStrongAssociatedBinding(
      std::make_unique<WebContentsNSViewBridge>(
          view_id, mojom::WebContentsNSViewHostAssociatedPtr(std::move(host))),
      std::move(ns_view_request),
      ui::WindowResizeHelperMac::Get()->task_runner());
}

}  // namespace remote_cocoa
