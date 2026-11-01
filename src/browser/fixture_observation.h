#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asciiomium::browser {

inline constexpr std::string_view kFixtureObservationPrefix =
    "ASCIIOMIUM_FIXTURE_V1|";

struct FixtureObservation {
  bool valid = false;
  std::uint64_t sequence = 0;
  std::uint64_t ready_sequence = 0;
  std::uint64_t host_ready_sequence = 0;
  std::uint64_t focus_sequence = 0;
  std::uint64_t keydown_sequence = 0;
  std::uint64_t keyup_sequence = 0;
  std::uint64_t input_sequence = 0;
  std::uint64_t pointer_sequence = 0;
  std::uint64_t click_sequence = 0;
  std::uint64_t wheel_sequence = 0;
  std::uint64_t scroll_sequence = 0;
  std::uint64_t nested_scroll_sequence = 0;
  std::uint64_t host_complete_sequence = 0;
  int scroll_y = 0;
  int nested_scroll_top = 0;
  std::string active_target;
  std::string input_utf8_hex;
  std::string click_target;
  std::string wheel_target;
  std::string ordered_events;
};

[[nodiscard]] bool ParseFixtureObservation(
    std::string_view message,
    FixtureObservation* observation);

// Require a post-dispatch scroll event AND a genuine scroll-position change.
// Some Chromium compositor scrolls do not emit a DOM wheel callback.
[[nodiscard]] bool ScrollChangedAfter(
    const FixtureObservation& before,
    const FixtureObservation& after) noexcept;

[[nodiscard]] std::string Utf8Hex(std::string_view input);

}  // namespace asciiomium::browser
