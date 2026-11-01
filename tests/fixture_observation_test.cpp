#include <cstdlib>
#include <iostream>
#include <string>

#include "browser/fixture_observation.h"

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "fixture_observation_test: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using asciiomium::browser::FixtureObservation;
  using asciiomium::browser::ParseFixtureObservation;
  using asciiomium::browser::Utf8Hex;

  // Model the old fixture channel directly: a later event replaced the only
  // observable title value, so polling after rapid focus/input updates could
  // no longer prove that focus had ever occurred.
  std::string legacy_title = "keyboard-focus-ready";
  legacy_title = "keyboard-input-asciiomium";
  Require(legacy_title != "keyboard-focus-ready",
          "legacy single-value channel did not demonstrate overwrite");

  FixtureObservation observation;
  const std::string durable_snapshot =
      "ASCIIOMIUM_FIXTURE_V1|seq=11|ready_seq=1|host_ready_seq=2|"
      "focus_seq=3|keydown_seq=4|keyup_seq=7|input_seq=6|"
      "pointer_seq=8|click_seq=9|wheel_seq=10|scroll_seq=11|"
      "nested_scroll_seq=0|host_complete_seq=0|scroll_y=120|"
      "nested_scroll_top=0|active=input-text|input_hex=61736369696f6d69756d|"
      "click_target=center|wheel_target=center|"
      "events=1:ready:fixture,2:host-ready:host,3:focusin:input-text,"
      "4:keydown:input-text,5:input:input-text,6:input:input-text,"
      "7:keyup:input-text,8:pointerdown:center,9:click:center,"
      "10:wheel:center,11:scroll:window";

  Require(ParseFixtureObservation(durable_snapshot, &observation),
          "durable snapshot did not parse");
  Require(observation.valid && observation.sequence == 11,
          "snapshot sequence missing");
  Require(observation.ready_sequence == 1 &&
              observation.host_ready_sequence == 2,
          "ready acknowledgements missing");
  Require(observation.focus_sequence == 3 &&
              observation.input_sequence == 6,
          "earlier keyboard events were lost");
  Require(observation.click_sequence == 9 &&
              observation.wheel_sequence == 10 &&
              observation.scroll_sequence == 11,
          "rapid pointer/wheel observations were not retained");
  Require(observation.active_target == "input-text" &&
              observation.input_utf8_hex == "61736369696f6d69756d",
          "DOM state was not preserved separately from events");
  Require(observation.ordered_events.find("3:focusin:input-text") !=
              std::string::npos &&
              observation.ordered_events.find("10:wheel:center") !=
                  std::string::npos,
          "ordered ledger did not retain overlapping observations");

  FixtureObservation malformed;
  Require(!ParseFixtureObservation("keyboard-input-asciiomium", &malformed),
          "legacy transient title unexpectedly parsed as durable state");
  Require(!ParseFixtureObservation("ASCIIOMIUM_FIXTURE_V1|seq=not-a-number",
                                   &malformed),
          "malformed sequence was accepted");

  Require(Utf8Hex("asciiomium") == "61736369696f6d69756d",
          "ASCII UTF-8 hex encoding changed");
  Require(Utf8Hex("\xC2\xA3") == "c2a3",
          "multibyte UTF-8 hex encoding changed");

  return 0;
}
