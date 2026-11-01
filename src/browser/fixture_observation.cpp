#include "browser/fixture_observation.h"

#include <charconv>
#include <system_error>
#include <utility>

namespace asciiomium::browser {
namespace {

bool ParseUnsigned(std::string_view value, std::uint64_t* output) {
  if (value.empty() || output == nullptr) {
    return false;
  }
  std::uint64_t parsed = 0;
  const char* first = value.data();
  const char* last = first + value.size();
  const auto result = std::from_chars(first, last, parsed);
  if (result.ec != std::errc{} || result.ptr != last) {
    return false;
  }
  *output = parsed;
  return true;
}

bool ParseSigned(std::string_view value, int* output) {
  if (value.empty() || output == nullptr) {
    return false;
  }
  int parsed = 0;
  const char* first = value.data();
  const char* last = first + value.size();
  const auto result = std::from_chars(first, last, parsed);
  if (result.ec != std::errc{} || result.ptr != last) {
    return false;
  }
  *output = parsed;
  return true;
}

}  // namespace

bool ParseFixtureObservation(std::string_view message,
                             FixtureObservation* observation) {
  if (observation == nullptr ||
      !message.starts_with(kFixtureObservationPrefix)) {
    return false;
  }

  FixtureObservation parsed;
  bool saw_sequence = false;
  std::string_view payload = message.substr(kFixtureObservationPrefix.size());
  while (!payload.empty()) {
    const std::size_t separator = payload.find('|');
    const std::string_view field =
        separator == std::string_view::npos ? payload
                                            : payload.substr(0, separator);
    const std::size_t equals = field.find('=');
    if (equals != std::string_view::npos) {
      const std::string_view key = field.substr(0, equals);
      const std::string_view value = field.substr(equals + 1);

      if (key == "seq") {
        saw_sequence = ParseUnsigned(value, &parsed.sequence);
      } else if (key == "ready_seq") {
        ParseUnsigned(value, &parsed.ready_sequence);
      } else if (key == "host_ready_seq") {
        ParseUnsigned(value, &parsed.host_ready_sequence);
      } else if (key == "focus_seq") {
        ParseUnsigned(value, &parsed.focus_sequence);
      } else if (key == "keydown_seq") {
        ParseUnsigned(value, &parsed.keydown_sequence);
      } else if (key == "keyup_seq") {
        ParseUnsigned(value, &parsed.keyup_sequence);
      } else if (key == "input_seq") {
        ParseUnsigned(value, &parsed.input_sequence);
      } else if (key == "pointer_seq") {
        ParseUnsigned(value, &parsed.pointer_sequence);
      } else if (key == "click_seq") {
        ParseUnsigned(value, &parsed.click_sequence);
      } else if (key == "wheel_seq") {
        ParseUnsigned(value, &parsed.wheel_sequence);
      } else if (key == "scroll_seq") {
        ParseUnsigned(value, &parsed.scroll_sequence);
      } else if (key == "nested_scroll_seq") {
        ParseUnsigned(value, &parsed.nested_scroll_sequence);
      } else if (key == "host_complete_seq") {
        ParseUnsigned(value, &parsed.host_complete_sequence);
      } else if (key == "scroll_y") {
        ParseSigned(value, &parsed.scroll_y);
      } else if (key == "nested_scroll_top") {
        ParseSigned(value, &parsed.nested_scroll_top);
      } else if (key == "active") {
        parsed.active_target.assign(value);
      } else if (key == "input_hex") {
        parsed.input_utf8_hex.assign(value);
      } else if (key == "click_target") {
        parsed.click_target.assign(value);
      } else if (key == "wheel_target") {
        parsed.wheel_target.assign(value);
      } else if (key == "events") {
        parsed.ordered_events.assign(value);
      }
    }

    if (separator == std::string_view::npos) {
      break;
    }
    payload.remove_prefix(separator + 1);
  }

  if (!saw_sequence) {
    return false;
  }
  parsed.valid = true;
  *observation = std::move(parsed);
  return true;
}

bool ScrollChangedAfter(const FixtureObservation& before,
                        const FixtureObservation& after) noexcept {
  if (!before.valid || !after.valid) {
    return false;
  }
  const bool page_scrolled =
      after.scroll_sequence > before.sequence &&
      after.scroll_y != before.scroll_y;
  const bool nested_scrolled =
      after.nested_scroll_sequence > before.sequence &&
      after.nested_scroll_top != before.nested_scroll_top;
  return page_scrolled || nested_scrolled;
}

std::string Utf8Hex(std::string_view input) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.reserve(input.size() * 2);
  for (const char value : input) {
    const auto byte = static_cast<unsigned char>(value);
    output.push_back(kHex[(byte >> 4) & 0x0f]);
    output.push_back(kHex[byte & 0x0f]);
  }
  return output;
}

}  // namespace asciiomium::browser
