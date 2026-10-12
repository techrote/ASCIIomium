#pragma once

#include <cstdint>
#include <string_view>

#include "input/keyboard_event.h"

namespace asciiomium::input {

enum class PointerEventKind {
  Move,
  ButtonPress,
  ButtonRelease,
  Wheel,
};

enum class PointerButton {
  None,
  Left,
  Middle,
  Right,
};

using PointerButtons = std::uint8_t;
inline constexpr PointerButtons kPointerButtonNone = 0;
inline constexpr PointerButtons kPointerButtonLeft = 1u << 0;
inline constexpr PointerButtons kPointerButtonMiddle = 1u << 1;
inline constexpr PointerButtons kPointerButtonRight = 1u << 2;

[[nodiscard]] constexpr bool HasPointerButton(PointerButtons buttons,
                                              PointerButtons flag) noexcept {
  return (buttons & flag) != 0;
}

struct PointerEvent {
  PointerEventKind kind = PointerEventKind::Move;
  PointerButton button = PointerButton::None;
  int column = 0;
  int row = 0;
  int wheel_x = 0;
  int wheel_y = 0;
  KeyModifiers modifiers = kModifierNone;
  PointerButtons buttons = kPointerButtonNone;

  friend constexpr bool operator==(const PointerEvent&,
                                   const PointerEvent&) = default;
};

[[nodiscard]] inline std::string_view PointerEventKindName(
    PointerEventKind kind) noexcept {
  switch (kind) {
    case PointerEventKind::Move: return "move";
    case PointerEventKind::ButtonPress: return "button-press";
    case PointerEventKind::ButtonRelease: return "button-release";
    case PointerEventKind::Wheel: return "wheel";
  }
  return "unknown";
}

[[nodiscard]] inline std::string_view PointerButtonName(
    PointerButton button) noexcept {
  switch (button) {
    case PointerButton::None: return "none";
    case PointerButton::Left: return "left";
    case PointerButton::Middle: return "middle";
    case PointerButton::Right: return "right";
  }
  return "unknown";
}

[[nodiscard]] constexpr PointerButtons PointerButtonFlag(
    PointerButton button) noexcept {
  switch (button) {
    case PointerButton::Left: return kPointerButtonLeft;
    case PointerButton::Middle: return kPointerButtonMiddle;
    case PointerButton::Right: return kPointerButtonRight;
    case PointerButton::None: return kPointerButtonNone;
  }
  return kPointerButtonNone;
}

}  // namespace asciiomium::input
