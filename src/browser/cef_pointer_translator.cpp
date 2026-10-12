#include "browser/cef_pointer_translator.h"

#include <limits>
#include <sstream>

#include "include/internal/cef_types.h"

namespace asciiomium::browser {
namespace {

int CefModifiers(asciiomium::input::KeyModifiers modifiers,
                 asciiomium::input::PointerButtons buttons) {
  using namespace asciiomium::input;
  int flags = 0;
  if (HasModifier(modifiers, kModifierShift)) {
    flags |= EVENTFLAG_SHIFT_DOWN;
  }
  if (HasModifier(modifiers, kModifierControl)) {
    flags |= EVENTFLAG_CONTROL_DOWN;
  }
  if (HasModifier(modifiers, kModifierAlt)) {
    flags |= EVENTFLAG_ALT_DOWN;
  }
  if (HasPointerButton(buttons, kPointerButtonLeft)) {
    flags |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  }
  if (HasPointerButton(buttons, kPointerButtonMiddle)) {
    flags |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
  }
  if (HasPointerButton(buttons, kPointerButtonRight)) {
    flags |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  }
  return flags;
}

bool SafeWheelScale(int logical, int scale, int* output) {
  if (scale <= 0 || output == nullptr) {
    return false;
  }
  const long long value =
      static_cast<long long>(logical) * static_cast<long long>(scale);
  if (value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    return false;
  }
  *output = static_cast<int>(value);
  return true;
}

}  // namespace

std::optional<CefPointerDispatch> TranslatePointerEvent(
    const asciiomium::input::PointerEvent& input,
    const asciiomium::geometry::ViewportTransform& transform,
    int wheel_delta_per_notch) {
  using namespace asciiomium::input;

  const auto mapped =
      transform.TerminalCellToBrowser(input.column, input.row);
  if (!mapped.has_value()) {
    return std::nullopt;
  }

  CefPointerDispatch dispatch;
  dispatch.event.x = mapped->x;
  dispatch.event.y = mapped->y;
  dispatch.event.modifiers = CefModifiers(input.modifiers, input.buttons);

  switch (input.kind) {
    case PointerEventKind::Move:
      dispatch.kind = CefPointerDispatchKind::Move;
      dispatch.click_count = 0;
      return dispatch;

    case PointerEventKind::ButtonPress:
    case PointerEventKind::ButtonRelease:
      if (input.button == PointerButton::None) {
        return std::nullopt;
      }
      dispatch.kind = CefPointerDispatchKind::Click;
      dispatch.button = input.button;
      dispatch.mouse_up =
          input.kind == PointerEventKind::ButtonRelease;
      dispatch.click_count = 1;
      return dispatch;

    case PointerEventKind::Wheel:
      if (!SafeWheelScale(input.wheel_x, wheel_delta_per_notch,
                          &dispatch.delta_x) ||
          !SafeWheelScale(input.wheel_y, wheel_delta_per_notch,
                          &dispatch.delta_y)) {
        return std::nullopt;
      }
      dispatch.kind = CefPointerDispatchKind::Wheel;
      dispatch.click_count = 0;
      return dispatch;
  }

  return std::nullopt;
}

std::string DescribeCefPointerDispatch(
    const CefPointerDispatch& dispatch) {
  std::ostringstream out;
  switch (dispatch.kind) {
    case CefPointerDispatchKind::Move:
      out << "mouse-move";
      break;
    case CefPointerDispatchKind::Click:
      out << "mouse-click "
          << asciiomium::input::PointerButtonName(dispatch.button)
          << (dispatch.mouse_up ? " up" : " down");
      break;
    case CefPointerDispatchKind::Wheel:
      out << "mouse-wheel delta=" << dispatch.delta_x << ','
          << dispatch.delta_y;
      break;
  }
  out << " xy=" << dispatch.event.x << ',' << dispatch.event.y
      << " modifiers=" << dispatch.event.modifiers;
  return out.str();
}

}  // namespace asciiomium::browser
