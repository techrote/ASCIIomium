#pragma once

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "input/keyboard_event.h"

namespace asciiomium::browser {

// Convert the transport-neutral keyboard event into the synthetic CEF event
// sequence required by the windowless browser. Text and key semantics remain
// separate: terminal UTF-8 text becomes CHAR events, while navigation and
// shortcut keys become RAWKEYDOWN/KEYUP pairs.
[[nodiscard]] std::vector<CefKeyEvent> TranslateKeyboardEvent(
    const asciiomium::input::KeyboardEvent& event);

[[nodiscard]] std::string DescribeCefKeyEvent(const CefKeyEvent& event);

}  // namespace asciiomium::browser
