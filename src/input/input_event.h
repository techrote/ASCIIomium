#pragma once

#include <string>

#include "input/keyboard_event.h"
#include "input/pointer_event.h"

namespace asciiomium::input {

enum class DecodedInputKind {
  Keyboard,
  Pointer,
};

struct DecodedInputEvent {
  DecodedInputKind kind = DecodedInputKind::Keyboard;
  KeyboardEvent event;
  PointerEvent pointer;
  std::string raw_bytes;
  std::string diagnostic;

  [[nodiscard]] bool is_keyboard() const noexcept {
    return kind == DecodedInputKind::Keyboard;
  }
  [[nodiscard]] bool is_pointer() const noexcept {
    return kind == DecodedInputKind::Pointer;
  }
};

}  // namespace asciiomium::input
