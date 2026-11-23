#include "browser/browser_commands.h"
#include "browser/bounded_text.h"

#include <algorithm>
#include <cctype>

namespace asciiomium::browser {
namespace {
std::string Lower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

void AppendUtf8(std::string& out, char32_t cp) {
  if (cp < 0x20 || cp == 0x7f || cp > 0x10ffff ||
      (cp >= 0xd800 && cp <= 0xdfff)) return;
  if (cp <= 0x7f) out += static_cast<char>(cp);
  else if (cp <= 0x7ff) {
    out += static_cast<char>(0xc0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 63));
  } else if (cp <= 0xffff) {
    out += static_cast<char>(0xe0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  } else {
    out += static_cast<char>(0xf0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 63));
    out += static_cast<char>(0x80 | ((cp >> 6) & 63));
    out += static_cast<char>(0x80 | (cp & 63));
  }
}
}  // namespace

bool IsAllowedNavigation(std::string_view url) {
  if (url.empty() || url.size() > 8192) return false;
  for (const unsigned char c : url) {
    if (c <= 0x20 || c == 0x7f || c == '\\') return false;
  }
  const auto colon = url.find(':');
  if (colon == std::string_view::npos) return false;
  const auto scheme = Lower(url.substr(0, colon));
  if (scheme == "about") return url.substr(colon + 1) == "blank";
  if (scheme == "file") return url.substr(colon + 1).starts_with("///");
  if (scheme != "http" && scheme != "https") return false;
  const auto rest = url.substr(colon + 1);
  return rest.starts_with("//") && rest.size() > 2 &&
         rest[2] != '/' && rest[2] != '?' && rest[2] != '#';
}

bool IsAllowedNavigationUtf16(std::u16string_view url) {
  if (url.size() > 8192) return false;
  const auto converted = BoundedUtf16Text(url, 8192);
  return !converted.truncated && IsAllowedNavigation(converted.text);
}

std::string DiagnosticOrigin(std::string_view url, bool complete) {
  // A clipped authority can hide a later '@' and turn credentials into what
  // looks like a host. Only complete input is eligible for origin extraction.
  if (!complete) return "[redacted]";
  const auto separator = url.find("://");
  if (separator == std::string_view::npos) return "[redacted]";
  const auto scheme = Lower(url.substr(0, separator));
  if (scheme != "http" && scheme != "https") return scheme == "file" ? "file://[local]" : "[redacted]";
  auto authority = url.substr(separator + 3);
  authority = authority.substr(0, authority.find_first_of("/?#\\"));
  const auto at = authority.rfind('@');
  if (at != std::string_view::npos) authority.remove_prefix(at + 1);
  return scheme + "://" + TerminalSafeText(authority, 256);
}

std::string TerminalSafeText(std::string_view text, std::size_t maximum) {
  std::string result;
  for (const unsigned char c : text) {
    if (result.size() >= maximum) break;
    result += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?';
  }
  return result;
}

BrowserCommand BrowserCommands::Parse(std::string_view text) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  if (text == "back") return {BrowserAction::Back, {}, {}};
  if (text == "forward") return {BrowserAction::Forward, {}, {}};
  if (text == "reload") return {BrowserAction::Reload, {}, {}};
  if (text == "hard-reload") return {BrowserAction::HardReload, {}, {}};
  if (text == "stop") return {BrowserAction::Stop, {}, {}};
  if (text == "status") return {BrowserAction::Status, {}, {}};
  if (text == "repaint") return {BrowserAction::Repaint, {}, {}};
  if (text == "quit") return {BrowserAction::Quit, {}, {}};
  if (text.starts_with("open ")) {
    text.remove_prefix(5);
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    if (IsAllowedNavigation(text)) return {BrowserAction::Open, std::string(text), {}};
    return {BrowserAction::None, {}, "Open requires http://, https://, file:/// or about:blank"};
  }
  return {BrowserAction::None, {}, "Commands: open URL | back | forward | reload | hard-reload | stop | status | repaint | quit"};
}

bool BrowserCommands::Handle(const input::KeyboardEvent& event,
                             BrowserCommand* command) {
  using namespace input;
  *command = {};
  const bool chord = event.kind == KeyboardEventKind::Key &&
      event.key == KeyCode::Character &&
      HasModifier(event.modifiers, kModifierControl) &&
      HasModifier(event.modifiers, kModifierAlt);
  if (chord && (event.codepoint == U'q' || event.codepoint == U'Q')) {
    *command = {BrowserAction::Quit, {}, {}};
    return true;
  }
  if (chord && (event.codepoint == U'o' || event.codepoint == U'O')) {
    active_ = !active_;
    text_.clear();
    return true;
  }
  if (!active_) return false;
  if (event.key == KeyCode::Escape) {
    active_ = false;
    text_.clear();
  } else if (event.key == KeyCode::Enter) {
    *command = Parse(text_);
    active_ = false;
    text_.clear();
  } else if (event.key == KeyCode::Backspace && !text_.empty()) {
    auto last = text_.size() - 1;
    while (last > 0 && (static_cast<unsigned char>(text_[last]) & 0xc0) == 0x80) --last;
    text_.resize(last);
  } else if (event.kind == KeyboardEventKind::Text &&
             !HasModifier(event.modifiers, kModifierControl) &&
             !HasModifier(event.modifiers, kModifierAlt)) {
    std::string encoded;
    AppendUtf8(encoded, event.codepoint);
    if (text_.size() + encoded.size() <= kMaximumBytes) text_ += encoded;
  }
  return true;
}
}  // namespace asciiomium::browser
