#include <iostream>
#include <string>
#include <vector>

#include "input/vt_input_parser.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void RequireEvent(const asciiomium::input::DecodedInputEvent& decoded,
                  asciiomium::input::KeyboardEventKind kind,
                  asciiomium::input::KeyCode key,
                  char32_t codepoint,
                  asciiomium::input::KeyModifiers modifiers,
                  const char* message) {
  const auto& event = decoded.event;
  if (event.kind != kind || event.key != key ||
      event.codepoint != codepoint || event.modifiers != modifiers) {
    std::cerr << "FAIL: " << message << " got "
              << asciiomium::input::DescribeKeyboardEvent(event)
              << " raw=" << asciiomium::input::EscapeBytes(decoded.raw_bytes)
              << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace asciiomium::input;

  {
    VtInputParser parser;
    const auto events = parser.Feed("abc");
    Require(events.size() == 3, "ASCII text event count");
    if (events.size() == 3) {
      RequireEvent(events[0], KeyboardEventKind::Text, KeyCode::None, U'a', 0,
                   "ASCII a");
      RequireEvent(events[1], KeyboardEventKind::Text, KeyCode::None, U'b', 0,
                   "ASCII b");
      RequireEvent(events[2], KeyboardEventKind::Text, KeyCode::None, U'c', 0,
                   "ASCII c");
    }
  }

  {
    VtInputParser parser;
    const std::string utf8 = "\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80";
    std::vector<DecodedInputEvent> events;
    for (const char byte : utf8) {
      auto part = parser.Feed(std::string_view(&byte, 1));
      events.insert(events.end(), part.begin(), part.end());
    }
    Require(events.size() == 3, "fragmented UTF-8 event count");
    if (events.size() == 3) {
      RequireEvent(events[0], KeyboardEventKind::Text, KeyCode::None, U'\u00A3',
                   0, "two-byte UTF-8");
      RequireEvent(events[1], KeyboardEventKind::Text, KeyCode::None, U'\u20AC',
                   0, "three-byte UTF-8");
      RequireEvent(events[2], KeyboardEventKind::Text, KeyCode::None,
                   U'\U0001F600', 0, "four-byte UTF-8");
    }
    Require(!parser.has_pending(), "UTF-8 parser drained");
  }

  {
    VtInputParser parser;
    Require(parser.Feed("\x1b[").empty(), "fragmented CSI waits");
    const auto events = parser.Feed("A");
    Require(events.size() == 1, "fragmented CSI completes");
    if (events.size() == 1) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::ArrowUp, 0, 0,
                   "arrow up");
    }
  }

  {
    VtInputParser parser;
    const auto events = parser.Feed(
        "\x1b[1;5A\x1b[1;2D\x1b[H\x1b[F"
        "\x1b[5~\x1b[6~\x1b[2~\x1b[3~");
    Require(events.size() == 8, "navigation sequence count");
    if (events.size() == 8) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::ArrowUp, 0,
                   kModifierControl, "Ctrl+Up");
      RequireEvent(events[1], KeyboardEventKind::Key, KeyCode::ArrowLeft, 0,
                   kModifierShift, "Shift+Left");
      RequireEvent(events[2], KeyboardEventKind::Key, KeyCode::Home, 0, 0,
                   "Home");
      RequireEvent(events[3], KeyboardEventKind::Key, KeyCode::End, 0, 0,
                   "End");
      RequireEvent(events[4], KeyboardEventKind::Key, KeyCode::PageUp, 0, 0,
                   "PageUp");
      RequireEvent(events[5], KeyboardEventKind::Key, KeyCode::PageDown, 0, 0,
                   "PageDown");
      RequireEvent(events[6], KeyboardEventKind::Key, KeyCode::Insert, 0, 0,
                   "Insert");
      RequireEvent(events[7], KeyboardEventKind::Key, KeyCode::Delete, 0, 0,
                   "Delete");
    }
  }

  {
    VtInputParser parser;
    const auto events = parser.Feed(
        "\x1bOP\x1bOQ\x1bOR\x1bOS\x1b[15~\x1b[24~");
    Require(events.size() == 6, "function-key sequence count");
    if (events.size() == 6) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::F1, 0, 0, "F1");
      RequireEvent(events[3], KeyboardEventKind::Key, KeyCode::F4, 0, 0, "F4");
      RequireEvent(events[4], KeyboardEventKind::Key, KeyCode::F5, 0, 0, "F5");
      RequireEvent(events[5], KeyboardEventKind::Key, KeyCode::F12, 0, 0,
                   "F12");
    }
  }

  {
    VtInputParser parser;
    const std::string controls{"\x01\x03\x16", 3};
    const auto events = parser.Feed(controls);
    Require(events.size() == 3, "Ctrl shortcut event count");
    if (events.size() == 3) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::Character, U'A',
                   kModifierControl, "Ctrl+A");
      RequireEvent(events[1], KeyboardEventKind::Key, KeyCode::Character, U'C',
                   kModifierControl, "Ctrl+C");
      RequireEvent(events[2], KeyboardEventKind::Key, KeyCode::Character, U'V',
                   kModifierControl, "Ctrl+V");
    }
  }

  {
    VtInputParser parser;
    const std::string ctrl_alt_q{"\x1b\x11", 2};
    const auto events = parser.Feed(ctrl_alt_q);
    Require(events.size() == 1, "Ctrl+Alt+Q event count");
    if (events.size() == 1) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::Character, U'Q',
                   kModifierControl | kModifierAlt, "Ctrl+Alt+Q");
    }
  }

  {
    VtInputParser parser;
    const auto alt = parser.Feed("\x1bx");
    Require(alt.size() == 1, "Alt+x count");
    if (alt.size() == 1) {
      RequireEvent(alt[0], KeyboardEventKind::Text, KeyCode::None, U'x',
                   kModifierAlt, "Alt+x");
    }

    Require(parser.Feed("\x1b").empty(), "lone Escape waits");
    const auto escape = parser.FlushPending();
    Require(escape.size() == 1, "lone Escape flush count");
    if (escape.size() == 1) {
      RequireEvent(escape[0], KeyboardEventKind::Key, KeyCode::Escape, 0, 0,
                   "lone Escape");
    }
  }

  {
    VtInputParser parser;
    const std::string editing{"\r\t\x7f", 3};
    const auto events = parser.Feed(editing);
    Require(events.size() == 3, "editing-key event count");
    if (events.size() == 3) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::Enter, 0, 0,
                   "Enter");
      RequireEvent(events[1], KeyboardEventKind::Key, KeyCode::Tab, 0, 0,
                   "Tab");
      RequireEvent(events[2], KeyboardEventKind::Key, KeyCode::Backspace, 0, 0,
                   "Backspace");
    }
  }

  {
    VtInputParser parser;
    const auto events = parser.Feed("\x1b[Z");
    Require(events.size() == 1, "Shift+Tab event count");
    if (events.size() == 1) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::Tab, 0,
                   kModifierShift, "Shift+Tab");
    }
  }

  {
    VtInputParser parser;
    Require(parser.Feed("\x1b[").empty(), "incomplete CSI waits");
    const auto events = parser.FlushPending();
    Require(events.size() == 1, "incomplete CSI flush count");
    if (events.size() == 1) {
      Require(events[0].event.kind == KeyboardEventKind::Unsupported,
              "incomplete CSI is unsupported");
      Require(events[0].diagnostic == "incomplete-csi",
              "incomplete CSI diagnostic");
    }
  }

  {
    VtInputParser parser;
    const std::string incomplete{"\xE2\x82", 2};
    Require(parser.Feed(incomplete).empty(), "incomplete UTF-8 waits");
    const auto events = parser.FlushPending();
    Require(events.size() == 1, "incomplete UTF-8 flush count");
    if (events.size() == 1) {
      Require(events[0].event.kind == KeyboardEventKind::Unsupported,
              "incomplete UTF-8 is unsupported");
      Require(events[0].diagnostic == "incomplete-utf8",
              "incomplete UTF-8 diagnostic");
    }
  }

  {
    VtInputParser parser;
    const std::string malformed{"\xE2\x28\xA1", 3};
    const auto events = parser.Feed(malformed);
    Require(!events.empty(), "malformed UTF-8 reported");
    Require(events.front().event.kind == KeyboardEventKind::Unsupported,
            "malformed UTF-8 is unsupported");
    Require(events.front().diagnostic == "malformed-utf8",
            "malformed UTF-8 diagnostic");
  }

  {
    VtInputParser parser;
    std::string long_prefix = "\x1b[";
    long_prefix.append(VtInputParser::kMaxPendingBytes + 20, '1');
    const auto events = parser.Feed(long_prefix);
    Require(!events.empty(), "overlong pending sequence reported");
    Require(events.front().diagnostic == "input-buffer-limit",
            "bounded-buffer diagnostic");
    Require(parser.pending_size() <= VtInputParser::kMaxPendingBytes,
            "parser pending state remains bounded");
  }

  {
    VtInputParser parser;
    Require(parser.Feed("\x1b[1;").empty(), "partial modifier waits");
    const auto events = parser.Feed("6Bz");
    Require(events.size() == 2, "fragment plus following event");
    if (events.size() == 2) {
      RequireEvent(events[0], KeyboardEventKind::Key, KeyCode::ArrowDown, 0,
                   kModifierShift | kModifierControl,
                   "Shift+Ctrl+Down");
      RequireEvent(events[1], KeyboardEventKind::Text, KeyCode::None, U'z', 0,
                   "event after escape sequence");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " input parser assertion(s) failed\n";
    return 1;
  }

  std::cout << "VT input parser tests passed\n";
  return 0;
}
