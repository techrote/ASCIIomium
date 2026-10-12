#include "input/vt_input_parser.h"

#include <array>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace asciiomium::input {
namespace {

enum class StepStatus {
  Event,
  NeedMore,
};

struct ParseStep {
  StepStatus status = StepStatus::NeedMore;
  std::size_t consumed = 0;
  DecodedInputKind kind = DecodedInputKind::Keyboard;
  KeyboardEvent keyboard;
  PointerEvent pointer;
  std::string diagnostic;
};

ParseStep NeedMore() {
  return {};
}

ParseStep KeyboardStep(std::size_t consumed,
                       KeyboardEvent event,
                       std::string diagnostic = {}) {
  ParseStep step;
  step.status = StepStatus::Event;
  step.consumed = consumed;
  step.kind = DecodedInputKind::Keyboard;
  step.keyboard = event;
  step.diagnostic = std::move(diagnostic);
  return step;
}

ParseStep PointerStep(std::size_t consumed,
                      PointerEvent event,
                      std::string diagnostic = {}) {
  ParseStep step;
  step.status = StepStatus::Event;
  step.consumed = consumed;
  step.kind = DecodedInputKind::Pointer;
  step.pointer = event;
  step.diagnostic = std::move(diagnostic);
  return step;
}

ParseStep Unsupported(std::size_t consumed, std::string diagnostic) {
  return KeyboardStep(
      consumed,
      KeyboardEvent{KeyboardEventKind::Unsupported, KeyCode::None, 0,
                    kModifierNone},
      std::move(diagnostic));
}

KeyboardEvent Key(KeyCode key,
                  KeyModifiers modifiers = kModifierNone,
                  char32_t codepoint = 0) {
  return KeyboardEvent{KeyboardEventKind::Key, key, codepoint, modifiers};
}

KeyboardEvent Text(char32_t codepoint,
                   KeyModifiers modifiers = kModifierNone) {
  return KeyboardEvent{KeyboardEventKind::Text, KeyCode::None, codepoint,
                       modifiers};
}

struct Utf8Decode {
  enum class Status {
    Complete,
    Incomplete,
    Invalid,
  };
  Status status = Status::Invalid;
  char32_t codepoint = 0;
  std::size_t length = 0;
};

bool IsContinuation(unsigned char byte) {
  return (byte & 0xC0u) == 0x80u;
}

Utf8Decode DecodeUtf8(std::string_view bytes) {
  if (bytes.empty()) {
    return {Utf8Decode::Status::Incomplete, 0, 0};
  }

  const auto b0 = static_cast<unsigned char>(bytes[0]);
  if (b0 < 0x80u) {
    return {Utf8Decode::Status::Complete, static_cast<char32_t>(b0), 1};
  }

  std::size_t length = 0;
  char32_t value = 0;
  if (b0 >= 0xC2u && b0 <= 0xDFu) {
    length = 2;
    value = b0 & 0x1Fu;
  } else if (b0 >= 0xE0u && b0 <= 0xEFu) {
    length = 3;
    value = b0 & 0x0Fu;
  } else if (b0 >= 0xF0u && b0 <= 0xF4u) {
    length = 4;
    value = b0 & 0x07u;
  } else {
    return {Utf8Decode::Status::Invalid, 0, 1};
  }

  if (bytes.size() < length) {
    return {Utf8Decode::Status::Incomplete, 0, 0};
  }

  for (std::size_t i = 1; i < length; ++i) {
    const auto byte = static_cast<unsigned char>(bytes[i]);
    if (!IsContinuation(byte)) {
      return {Utf8Decode::Status::Invalid, 0, 1};
    }
    value = (value << 6) | (byte & 0x3Fu);
  }

  const bool overlong =
      (length == 2 && value < 0x80) ||
      (length == 3 && value < 0x800) ||
      (length == 4 && value < 0x10000);
  const bool surrogate = value >= 0xD800 && value <= 0xDFFF;
  if (overlong || surrogate || value > 0x10FFFF) {
    return {Utf8Decode::Status::Invalid, 0, 1};
  }

  return {Utf8Decode::Status::Complete, value, length};
}

std::optional<KeyModifiers> DecodeModifierParameter(int parameter) {
  if (parameter <= 1) {
    return kModifierNone;
  }
  if (parameter > 8) {
    return std::nullopt;
  }

  const int bits = parameter - 1;
  KeyModifiers modifiers = kModifierNone;
  if ((bits & 1) != 0) modifiers |= kModifierShift;
  if ((bits & 2) != 0) modifiers |= kModifierAlt;
  if ((bits & 4) != 0) modifiers |= kModifierControl;
  return modifiers;
}

std::optional<std::vector<int>> ParseParameters(std::string_view text) {
  std::vector<int> values;
  if (text.empty()) {
    return values;
  }

  int value = 0;
  bool have_digit = false;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ';') {
      values.push_back(have_digit ? value : 1);
      value = 0;
      have_digit = false;
      continue;
    }

    const char ch = text[i];
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    have_digit = true;
    value = value * 10 + (ch - '0');
    if (value > 1'000'000) {
      return std::nullopt;
    }
  }
  return values;
}

std::optional<std::array<int, 3>> ParseMouseParameters(
    std::string_view text) {
  std::array<int, 3> values{};
  std::size_t field = 0;
  int value = 0;
  bool have_digit = false;

  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == ';') {
      if (!have_digit || field >= values.size()) {
        return std::nullopt;
      }
      values[field++] = value;
      value = 0;
      have_digit = false;
      continue;
    }

    const char ch = text[i];
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    have_digit = true;
    value = value * 10 + (ch - '0');
    if (value > 1'000'000) {
      return std::nullopt;
    }
  }

  if (field != values.size()) {
    return std::nullopt;
  }
  return values;
}

PointerButton DecodePointerButton(int code) {
  switch (code) {
    case 0: return PointerButton::Left;
    case 1: return PointerButton::Middle;
    case 2: return PointerButton::Right;
    default: return PointerButton::None;
  }
}

KeyModifiers DecodeMouseModifiers(int cb) {
  KeyModifiers modifiers = kModifierNone;
  if ((cb & 4) != 0) modifiers |= kModifierShift;
  if ((cb & 8) != 0) modifiers |= kModifierAlt;
  if ((cb & 16) != 0) modifiers |= kModifierControl;
  return modifiers;
}

ParseStep ParseSgrMouse(std::string_view bytes, bool flush) {
  std::size_t final_index = std::string_view::npos;
  for (std::size_t i = 3; i < bytes.size(); ++i) {
    if (bytes[i] == 'M' || bytes[i] == 'm') {
      final_index = i;
      break;
    }
    const auto ch = static_cast<unsigned char>(bytes[i]);
    if ((ch >= 0x40u && ch <= 0x7Eu) && ch != ';') {
      return Unsupported(i + 1, "unsupported-sgr-mouse-final");
    }
  }

  if (final_index == std::string_view::npos) {
    if (!flush) {
      return NeedMore();
    }
    return Unsupported(bytes.size(), "incomplete-sgr-mouse");
  }

  const std::size_t consumed = final_index + 1;
  const auto params =
      ParseMouseParameters(bytes.substr(3, final_index - 3));
  if (!params.has_value()) {
    return Unsupported(consumed, "invalid-sgr-mouse-parameters");
  }

  const int cb = (*params)[0];
  const int protocol_x = (*params)[1];
  const int protocol_y = (*params)[2];
  if (cb < 0 || cb > 127 || protocol_x <= 0 || protocol_y <= 0) {
    return Unsupported(consumed, "invalid-sgr-mouse-range");
  }

  PointerEvent event;
  event.column = protocol_x - 1;
  event.row = protocol_y - 1;
  event.modifiers = DecodeMouseModifiers(cb);

  const bool motion = (cb & 32) != 0;
  const bool wheel = (cb & 64) != 0;
  const int button_code = cb & 3;
  const char final = bytes[final_index];

  if (wheel) {
    if (motion || final != 'M') {
      return Unsupported(consumed, "invalid-sgr-mouse-wheel");
    }
    event.kind = PointerEventKind::Wheel;
    event.button = PointerButton::None;
    switch (button_code) {
      case 0: event.wheel_y = 1; break;
      case 1: event.wheel_y = -1; break;
      case 2: event.wheel_x = -1; break;
      case 3: event.wheel_x = 1; break;
    }
    return PointerStep(consumed, event);
  }

  if (motion) {
    if (final != 'M') {
      return Unsupported(consumed, "invalid-sgr-mouse-motion");
    }
    event.kind = PointerEventKind::Move;
    event.button = DecodePointerButton(button_code);
    event.buttons = PointerButtonFlag(event.button);
    return PointerStep(consumed, event);
  }

  event.button = DecodePointerButton(button_code);
  if (event.button == PointerButton::None) {
    return Unsupported(consumed, "unsupported-sgr-mouse-button");
  }

  if (final == 'M') {
    event.kind = PointerEventKind::ButtonPress;
    event.buttons = PointerButtonFlag(event.button);
  } else {
    event.kind = PointerEventKind::ButtonRelease;
    event.buttons = kPointerButtonNone;
  }
  return PointerStep(consumed, event);
}

KeyCode TildeKey(int code) {
  switch (code) {
    case 1:
    case 7: return KeyCode::Home;
    case 2: return KeyCode::Insert;
    case 3: return KeyCode::Delete;
    case 4:
    case 8: return KeyCode::End;
    case 5: return KeyCode::PageUp;
    case 6: return KeyCode::PageDown;
    case 11: return KeyCode::F1;
    case 12: return KeyCode::F2;
    case 13: return KeyCode::F3;
    case 14: return KeyCode::F4;
    case 15: return KeyCode::F5;
    case 17: return KeyCode::F6;
    case 18: return KeyCode::F7;
    case 19: return KeyCode::F8;
    case 20: return KeyCode::F9;
    case 21: return KeyCode::F10;
    case 23: return KeyCode::F11;
    case 24: return KeyCode::F12;
    default: return KeyCode::None;
  }
}

ParseStep ParseAtom(std::string_view bytes,
                    KeyModifiers extra_modifiers,
                    bool flush) {
  if (bytes.empty()) {
    return NeedMore();
  }

  const auto byte = static_cast<unsigned char>(bytes[0]);

  if (byte == '\r' || byte == '\n') {
    return KeyboardStep(1, Key(KeyCode::Enter, extra_modifiers));
  }
  if (byte == '\t') {
    return KeyboardStep(1, Key(KeyCode::Tab, extra_modifiers));
  }
  if (byte == '\b' || byte == 0x7Fu) {
    return KeyboardStep(1, Key(KeyCode::Backspace, extra_modifiers));
  }

  if (byte == 0x00u) {
    return KeyboardStep(1, Key(KeyCode::Character,
                               extra_modifiers | kModifierControl, U' '));
  }
  if (byte >= 0x01u && byte <= 0x1Au) {
    return KeyboardStep(1, Key(KeyCode::Character,
                               extra_modifiers | kModifierControl,
                               static_cast<char32_t>(U'A' + byte - 1)));
  }
  if (byte >= 0x1Cu && byte <= 0x1Fu) {
    static constexpr std::array<char32_t, 4> kControlChars{
        U'\\', U']', U'^', U'_'};
    return KeyboardStep(1, Key(KeyCode::Character,
                               extra_modifiers | kModifierControl,
                               kControlChars[byte - 0x1Cu]));
  }
  if (byte < 0x20u) {
    return Unsupported(1, "unsupported-control-byte");
  }

  if (byte < 0x80u) {
    return KeyboardStep(
        1, Text(static_cast<char32_t>(byte), extra_modifiers));
  }

  const auto decoded = DecodeUtf8(bytes);
  if (decoded.status == Utf8Decode::Status::Incomplete) {
    if (!flush) {
      return NeedMore();
    }
    return Unsupported(bytes.size(), "incomplete-utf8");
  }
  if (decoded.status == Utf8Decode::Status::Invalid) {
    return Unsupported(decoded.length == 0 ? 1 : decoded.length,
                       "malformed-utf8");
  }
  return KeyboardStep(
      decoded.length, Text(decoded.codepoint, extra_modifiers));
}

ParseStep ParseCsi(std::string_view bytes, bool flush) {
  if (bytes.size() >= 3 && bytes[2] == '<') {
    return ParseSgrMouse(bytes, flush);
  }

  std::size_t final_index = std::string_view::npos;
  for (std::size_t i = 2; i < bytes.size(); ++i) {
    const auto ch = static_cast<unsigned char>(bytes[i]);
    if (ch >= 0x40u && ch <= 0x7Eu) {
      final_index = i;
      break;
    }
  }

  if (final_index == std::string_view::npos) {
    if (!flush) {
      return NeedMore();
    }
    return Unsupported(bytes.size(), "incomplete-csi");
  }

  const std::size_t consumed = final_index + 1;
  const char final = bytes[final_index];
  const auto params = ParseParameters(bytes.substr(2, final_index - 2));
  if (!params.has_value()) {
    return Unsupported(consumed, "unsupported-csi-parameters");
  }

  KeyModifiers modifiers = kModifierNone;
  if (params->size() >= 2) {
    const auto parsed = DecodeModifierParameter((*params)[1]);
    if (!parsed.has_value()) {
      return Unsupported(consumed, "unsupported-csi-modifier");
    }
    modifiers = *parsed;
  }
  if (params->size() > 2) {
    return Unsupported(consumed, "unsupported-csi-arity");
  }

  auto directional = [&](KeyCode key) -> ParseStep {
    if (!params->empty() && (*params)[0] != 1) {
      return Unsupported(consumed, "unsupported-csi-prefix");
    }
    return KeyboardStep(consumed, Key(key, modifiers));
  };

  switch (final) {
    case 'A': return directional(KeyCode::ArrowUp);
    case 'B': return directional(KeyCode::ArrowDown);
    case 'C': return directional(KeyCode::ArrowRight);
    case 'D': return directional(KeyCode::ArrowLeft);
    case 'H': return directional(KeyCode::Home);
    case 'F': return directional(KeyCode::End);
    case 'Z':
      if (!params->empty()) {
        return Unsupported(consumed, "unsupported-shift-tab-parameters");
      }
      return KeyboardStep(
          consumed, Key(KeyCode::Tab, kModifierShift));
    case '~': {
      if (params->empty()) {
        return Unsupported(consumed, "missing-csi-tilde-code");
      }
      const KeyCode key = TildeKey((*params)[0]);
      if (key == KeyCode::None) {
        return Unsupported(consumed, "unsupported-csi-tilde-code");
      }
      return KeyboardStep(consumed, Key(key, modifiers));
    }
    default:
      return Unsupported(consumed, "unsupported-csi-final");
  }
}

ParseStep ParseSs3(std::string_view bytes, bool flush) {
  if (bytes.size() < 3) {
    if (!flush) {
      return NeedMore();
    }
    return Unsupported(bytes.size(), "incomplete-ss3");
  }

  KeyCode key = KeyCode::None;
  switch (bytes[2]) {
    case 'A': key = KeyCode::ArrowUp; break;
    case 'B': key = KeyCode::ArrowDown; break;
    case 'C': key = KeyCode::ArrowRight; break;
    case 'D': key = KeyCode::ArrowLeft; break;
    case 'H': key = KeyCode::Home; break;
    case 'F': key = KeyCode::End; break;
    case 'P': key = KeyCode::F1; break;
    case 'Q': key = KeyCode::F2; break;
    case 'R': key = KeyCode::F3; break;
    case 'S': key = KeyCode::F4; break;
    default: break;
  }
  if (key == KeyCode::None) {
    return Unsupported(3, "unsupported-ss3");
  }
  return KeyboardStep(3, Key(key));
}

ParseStep ParseOne(std::string_view bytes, bool flush) {
  if (bytes.empty()) {
    return NeedMore();
  }

  if (static_cast<unsigned char>(bytes[0]) != 0x1Bu) {
    return ParseAtom(bytes, kModifierNone, flush);
  }

  if (bytes.size() == 1) {
    if (!flush) {
      return NeedMore();
    }
    return KeyboardStep(1, Key(KeyCode::Escape));
  }

  if (static_cast<unsigned char>(bytes[1]) == 0x1Bu) {
    return KeyboardStep(1, Key(KeyCode::Escape));
  }
  if (bytes[1] == '[') {
    return ParseCsi(bytes, flush);
  }
  if (bytes[1] == 'O') {
    return ParseSs3(bytes, flush);
  }

  ParseStep alt = ParseAtom(bytes.substr(1), kModifierAlt, flush);
  if (alt.status == StepStatus::NeedMore) {
    return alt;
  }
  ++alt.consumed;
  return alt;
}

}  // namespace

std::vector<DecodedInputEvent> VtInputParser::Feed(std::string_view bytes) {
  pending_.append(bytes.data(), bytes.size());
  return ParseAvailable(false);
}

std::vector<DecodedInputEvent> VtInputParser::FlushPending() {
  return ParseAvailable(true);
}

std::vector<DecodedInputEvent> VtInputParser::ParseAvailable(bool flush) {
  std::vector<DecodedInputEvent> events;

  while (!pending_.empty()) {
    ParseStep step = ParseOne(pending_, flush);
    if (step.status == StepStatus::NeedMore) {
      if (pending_.size() <= kMaxPendingBytes) {
        break;
      }

      DecodedInputEvent overflow;
      overflow.event =
          KeyboardEvent{KeyboardEventKind::Unsupported, KeyCode::None, 0,
                        kModifierNone};
      overflow.raw_bytes = pending_.substr(0, kMaxPendingBytes);
      overflow.diagnostic = "input-buffer-limit";
      events.push_back(std::move(overflow));
      pending_.erase(0, kMaxPendingBytes);
      continue;
    }

    if (step.consumed == 0 || step.consumed > pending_.size()) {
      DecodedInputEvent invalid;
      invalid.event =
          KeyboardEvent{KeyboardEventKind::Unsupported, KeyCode::None, 0,
                        kModifierNone};
      invalid.raw_bytes = pending_.substr(0, 1);
      invalid.diagnostic = "parser-invariant";
      events.push_back(std::move(invalid));
      pending_.erase(0, 1);
      continue;
    }

    DecodedInputEvent decoded;
    decoded.kind = step.kind;
    decoded.event = step.keyboard;
    decoded.pointer = step.pointer;
    decoded.raw_bytes = pending_.substr(0, step.consumed);
    decoded.diagnostic = std::move(step.diagnostic);
    events.push_back(std::move(decoded));
    pending_.erase(0, step.consumed);
  }

  return events;
}

std::string EscapeBytes(std::string_view bytes) {
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setfill('0');
  for (const unsigned char byte : bytes) {
    if (byte >= 0x20u && byte <= 0x7Eu && byte != '\\') {
      out << static_cast<char>(byte);
    } else {
      out << "\\x" << std::setw(2) << static_cast<unsigned>(byte);
    }
  }
  return out.str();
}

std::string DescribeKeyboardEvent(const KeyboardEvent& event) {
  std::ostringstream out;
  switch (event.kind) {
    case KeyboardEventKind::Text:
      out << "text";
      break;
    case KeyboardEventKind::Key:
      out << "key:" << KeyCodeName(event.key);
      break;
    case KeyboardEventKind::Unsupported:
      out << "unsupported";
      break;
  }

  if (event.codepoint != 0) {
    out << " U+" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(event.codepoint <= 0xFFFF ? 4 : 6)
        << static_cast<std::uint32_t>(event.codepoint) << std::dec;
  }
  out << " mods=" << ModifierNames(event.modifiers);
  return out.str();
}

std::string DescribePointerEvent(const PointerEvent& event) {
  std::ostringstream out;
  out << "pointer:" << PointerEventKindName(event.kind)
      << " button=" << PointerButtonName(event.button)
      << " cell=" << event.column << ',' << event.row
      << " mods=" << ModifierNames(event.modifiers);
  if (event.kind == PointerEventKind::Wheel) {
    out << " wheel=" << event.wheel_x << ',' << event.wheel_y;
  }
  if (event.buttons != kPointerButtonNone) {
    out << " buttons=";
    bool first = true;
    auto append = [&](std::string_view name) {
      if (!first) out << '+';
      out << name;
      first = false;
    };
    if (HasPointerButton(event.buttons, kPointerButtonLeft)) append("left");
    if (HasPointerButton(event.buttons, kPointerButtonMiddle)) append("middle");
    if (HasPointerButton(event.buttons, kPointerButtonRight)) append("right");
  }
  return out.str();
}

}  // namespace asciiomium::input
