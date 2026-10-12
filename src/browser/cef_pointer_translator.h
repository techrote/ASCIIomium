#pragma once

#include <optional>
#include <string>

#include "geometry/viewport_transform.h"
#include "include/cef_browser.h"
#include "input/pointer_event.h"

namespace asciiomium::browser {

enum class CefPointerDispatchKind {
  Move,
  Click,
  Wheel,
};

struct CefPointerDispatch {
  CefPointerDispatchKind kind = CefPointerDispatchKind::Move;
  CefMouseEvent event{};
  asciiomium::input::PointerButton button =
      asciiomium::input::PointerButton::None;
  bool mouse_up = false;
  int click_count = 1;
  int delta_x = 0;
  int delta_y = 0;
};

// Converts one canonical cell-granular pointer event through the authoritative
// #10 viewport transform. nullopt means the terminal cell is outside browser
// content (for example contain-mode letterbox space) or the event is invalid.
[[nodiscard]] std::optional<CefPointerDispatch> TranslatePointerEvent(
    const asciiomium::input::PointerEvent& input,
    const asciiomium::geometry::ViewportTransform& transform,
    int wheel_delta_per_notch);

[[nodiscard]] std::string DescribeCefPointerDispatch(
    const CefPointerDispatch& dispatch);

}  // namespace asciiomium::browser
