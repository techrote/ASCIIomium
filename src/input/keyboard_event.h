#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asciiomium::input {

enum class KeyboardEventKind {
  Text,
  Key,
  Unsupported,
};

enum class KeyCode {
  None,
  Character,
  Enter,
  Tab,
  Backspace,
  Escape,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  Home,
  End,
  PageUp,
  PageDown,
  Insert,
  Delete,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
};

using KeyModifiers = std::uint8_t;
inline constexpr KeyModifiers kModifierNone = 0;
inline constexpr KeyModifiers kModifierShift = 1u << 0;
inline constexpr KeyModifiers kModifierAlt = 1u << 1;
inline constexpr KeyModifiers kModifierControl = 1u << 2;

[[nodiscard]] constexpr bool HasModifier(KeyModifiers modifiers,
                                         KeyModifiers flag) noexcept {
  return (modifiers & flag) != 0;
}

struct KeyboardEvent {
  KeyboardEventKind kind = KeyboardEventKind::Unsupported;
  KeyCode key = KeyCode::None;
  char32_t codepoint = 0;
  KeyModifiers modifiers = kModifierNone;
};

struct DecodedInputEvent {
  KeyboardEvent event;
  std::string raw_bytes;
  std::string diagnostic;
};

[[nodiscard]] inline std::string_view KeyCodeName(KeyCode key) noexcept {
  switch (key) {
    case KeyCode::None: return "none";
    case KeyCode::Character: return "character";
    case KeyCode::Enter: return "enter";
    case KeyCode::Tab: return "tab";
    case KeyCode::Backspace: return "backspace";
    case KeyCode::Escape: return "escape";
    case KeyCode::ArrowUp: return "arrow-up";
    case KeyCode::ArrowDown: return "arrow-down";
    case KeyCode::ArrowLeft: return "arrow-left";
    case KeyCode::ArrowRight: return "arrow-right";
    case KeyCode::Home: return "home";
    case KeyCode::End: return "end";
    case KeyCode::PageUp: return "page-up";
    case KeyCode::PageDown: return "page-down";
    case KeyCode::Insert: return "insert";
    case KeyCode::Delete: return "delete";
    case KeyCode::F1: return "f1";
    case KeyCode::F2: return "f2";
    case KeyCode::F3: return "f3";
    case KeyCode::F4: return "f4";
    case KeyCode::F5: return "f5";
    case KeyCode::F6: return "f6";
    case KeyCode::F7: return "f7";
    case KeyCode::F8: return "f8";
    case KeyCode::F9: return "f9";
    case KeyCode::F10: return "f10";
    case KeyCode::F11: return "f11";
    case KeyCode::F12: return "f12";
  }
  return "unknown";
}

[[nodiscard]] inline std::string ModifierNames(KeyModifiers modifiers) {
  if (modifiers == kModifierNone) {
    return "none";
  }
  std::string result;
  auto append = [&](std::string_view name) {
    if (!result.empty()) result += '+';
    result.append(name);
  };
  if (HasModifier(modifiers, kModifierShift)) append("shift");
  if (HasModifier(modifiers, kModifierAlt)) append("alt");
  if (HasModifier(modifiers, kModifierControl)) append("ctrl");
  return result;
}

}  // namespace asciiomium::input
