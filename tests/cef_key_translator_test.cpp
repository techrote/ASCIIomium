#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <vector>

#include "browser/cef_key_translator.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using namespace asciiomium::input;
  using asciiomium::browser::TranslateKeyboardEvent;

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Text, KeyCode::None, U'a', 0});
    Require(events.size() == 3, "mappable plain text emits down/char/up");
    if (events.size() == 3) {
      Require(events[0].type == KEYEVENT_RAWKEYDOWN,
              "plain text RAWKEYDOWN type");
      Require(events[0].windows_key_code == 'A',
              "plain text virtual key");
      Require(events[1].type == KEYEVENT_CHAR, "plain text CHAR type");
      Require(events[1].character == u'a', "plain text character");
      Require(events[2].type == KEYEVENT_KEYUP, "plain text KEYUP type");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Text, KeyCode::None,
                      U'\U0001F600', 0});
    Require(events.size() == 2, "non-BMP text emits UTF-16 surrogate pair");
    if (events.size() == 2) {
      Require(events[0].type == KEYEVENT_CHAR &&
                  events[1].type == KEYEVENT_CHAR,
              "surrogate pair CHAR types");
      Require(events[0].character == 0xD83D &&
                  events[1].character == 0xDE00,
              "surrogate pair values");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Key, KeyCode::ArrowLeft, 0,
                      kModifierControl});
    Require(events.size() == 2, "navigation key emits down/up only");
    if (events.size() == 2) {
      Require(events[0].type == KEYEVENT_RAWKEYDOWN,
              "navigation RAWKEYDOWN");
      Require(events[0].windows_key_code == VK_LEFT,
              "navigation VK_LEFT");
      Require((events[0].modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
              "navigation Ctrl modifier");
      Require(events[1].type == KEYEVENT_KEYUP, "navigation KEYUP");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Key, KeyCode::Enter, 0, 0});
    Require(events.size() == 3, "Enter emits down/char/up");
    if (events.size() == 3) {
      Require(events[0].type == KEYEVENT_RAWKEYDOWN, "Enter down");
      Require(events[1].type == KEYEVENT_CHAR &&
                  events[1].character == u'\r',
              "Enter char");
      Require(events[2].type == KEYEVENT_KEYUP, "Enter up");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Key, KeyCode::Character, U'C',
                      kModifierControl});
    Require(events.size() == 2, "Ctrl+C does not emit CHAR");
    if (events.size() == 2) {
      Require(events[0].windows_key_code == 'C', "Ctrl+C virtual key");
      Require((events[0].modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
              "Ctrl+C modifier");
      Require(events[0].type == KEYEVENT_RAWKEYDOWN &&
                  events[1].type == KEYEVENT_KEYUP,
              "Ctrl+C down/up");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Text, KeyCode::None, U'x',
                      kModifierAlt});
    Require(events.size() == 2, "Alt+x emits shortcut down/up");
    if (events.size() == 2) {
      Require((events[0].modifiers & EVENTFLAG_ALT_DOWN) != 0,
              "Alt+x modifier");
      Require(events[0].is_system_key != 0, "Alt+x system-key flag");
    }
  }

  {
    const auto events = TranslateKeyboardEvent(
        KeyboardEvent{KeyboardEventKind::Unsupported, KeyCode::None, 0, 0});
    Require(events.empty(), "unsupported input is not forwarded");
  }

  if (failures != 0) {
    std::cerr << failures << " CEF key translator assertion(s) failed\n";
    return 1;
  }

  std::cout << "CEF key translator tests passed\n";
  return 0;
}
