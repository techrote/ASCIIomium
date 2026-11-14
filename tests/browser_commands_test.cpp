#include "browser/browser_commands.h"
#include "input/vt_input_parser.h"

#include <iostream>
#include <stdexcept>

using namespace asciiomium::browser;
using namespace asciiomium::input;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    BrowserCommands commands;
    VtInputParser parser;
    BrowserCommand command;
    auto feed = [&](std::string_view bytes) {
      bool consumed = false;
      for (const auto& event : parser.Feed(bytes)) {
        Require(!event.is_pointer(), "unexpected pointer in key test");
        consumed = commands.Handle(event.event, &command);
      }
      return consumed;
    };
    Require(!feed("x"), "page text must pass through");
    Require(!feed("\x03"), "Ctrl+C must pass through");
    Require(!feed("\x01"), "Ctrl+A must pass through");
    Require(feed("\x1b\x0f") && commands.active(), "Ctrl+Alt+O opens commands");
    Require(feed("open https://example.com/path?token=secret"), "command consumes text");
    Require(feed("\r") && command.action == BrowserAction::Open &&
            command.argument == "https://example.com/path?token=secret", "command URL preserved for navigation");
    Require(!commands.active(), "Enter dismisses overlay");
    Require(DiagnosticOrigin(command.argument) == "https://example.com", "report drops path/query");
    Require(DiagnosticOrigin("https://user:password@host.test:8443/a#token") ==
            "https://host.test:8443", "report drops credentials and fragment");
    Require(DiagnosticOrigin("file:///C:/private/secret.html") == "file://[local]", "local path redacted");
    Require(TerminalSafeText("a\x1b[2J\r\nb") == "a?[2J??b", "untrusted UI cannot inject VT");
    Require(!IsAllowedNavigation("javascript:alert(1)"), "javascript navigation blocked");
    Require(!IsAllowedNavigation("shell:AppsFolder"), "external protocol blocked");
    Require(!IsAllowedNavigation("https://a.test/\nsecret"), "control characters blocked");
    Require(!IsAllowedNavigation("https://a\\b"), "ambiguous slash blocked");
    Require(IsAllowedNavigation("https://example.com") && IsAllowedNavigation("file:///C:/a.html") &&
            IsAllowedNavigation("about:blank"), "supported URLs allowed");
    for (const auto* text : {"back", "forward", "reload", "hard-reload", "stop", "status", "repaint", "quit"})
      Require(BrowserCommands::Parse(text).action != BrowserAction::None, "command missing");
    Require(!BrowserCommands::Parse("open javascript:alert(1)").error.empty(), "bad open explained");
    Require(!BrowserCommands::Parse("wat").error.empty(), "unknown command explained");
    feed("\x1b\x0f");
    feed("\xc3\xa9");
    feed("\x7f");
    Require(commands.text().empty(), "backspace removes complete UTF-8 codepoint");
    feed(std::string(5000, 'x'));
    Require(commands.text().size() == BrowserCommands::kMaximumBytes, "command memory bounded");
    feed("\x1b\x11");
    Require(command.action == BrowserAction::Quit, "quit works inside command mode");
    std::cout << "browser_commands: pass (real VT parser, shortcuts, bounds, URL/log policy)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
