#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "input/keyboard_event.h"

namespace asciiomium::browser {

enum class BrowserAction {
  None, Open, Back, Forward, Reload, HardReload, Stop, Status, Repaint, Quit,
};

struct BrowserCommand {
  BrowserAction action = BrowserAction::None;
  std::string argument;
  std::string error;
};

// Only explicit browser command mode consumes ordinary text. Page shortcuts
// continue through the existing VT parser/CEF translator unchanged.
class BrowserCommands {
 public:
  static constexpr std::size_t kMaximumBytes = 2048;
  bool Handle(const input::KeyboardEvent& event, BrowserCommand* command);
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const std::string& text() const noexcept { return text_; }
  [[nodiscard]] static BrowserCommand Parse(std::string_view text);

 private:
  bool active_ = false;
  std::string text_;
};

// Browser policy and diagnostics helpers. Reports retain only an origin, never
// URL credentials/path/query/fragment or page titles. UI text is ASCII-only so
// untrusted titles/URLs cannot inject VT, bidi controls or wide glyphs.
[[nodiscard]] bool IsAllowedNavigation(std::string_view url);
[[nodiscard]] std::string DiagnosticOrigin(std::string_view url);
[[nodiscard]] std::string TerminalSafeText(std::string_view text,
                                         std::size_t maximum = 2048);

}  // namespace asciiomium::browser
