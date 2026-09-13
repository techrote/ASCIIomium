#include <iostream>
#include <string>

#include "terminal/terminal_diagnostics.h"
#include "terminal/terminal_geometry.h"
#include "terminal/terminal_sequences.h"

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
  using namespace asciiomium::terminal;

  Require(BuildAcquireSequence(true, true) ==
              std::string(kEnterAlternateScreen) + kClearScreen + kCursorHome +
                  kResetSgr + kHideCursor,
          "alternate-screen acquire sequence");
  Require(BuildAcquireSequence(false, false) == kResetSgr,
          "minimal acquire sequence");
  Require(BuildRestoreSequence(true, true) ==
              std::string(kResetSgr) + kShowCursor + kLeaveAlternateScreen,
          "full restore sequence");
  Require(BuildRestoreSequence(false, false) == kResetSgr,
          "minimal restore sequence");
  Require(BuildCursorPosition(4, 9) == "\x1b[4;9H", "cursor positioning");
  Require(BuildCursorPosition(0, -5) == "\x1b[1;1H", "cursor clamp");
  Require(BuildRgbForeground(1, 2, 3) == "\x1b[38;2;1;2;3m",
          "RGB foreground sequence");
  Require(BuildRgbBackground(254, 128, 0) == "\x1b[48;2;254;128;0m",
          "RGB background sequence");

  GeometryTracker tracker;
  Require(!tracker.current().valid(), "initial geometry invalid");
  Require(!tracker.Update(0, 20), "invalid geometry rejected");
  Require(tracker.Update(80, 24), "first geometry update recorded");
  Require(tracker.current().columns == 80 && tracker.current().rows == 24,
          "geometry dimensions stored");
  Require(tracker.current().generation == 1, "first generation is one");
  Require(!tracker.Update(80, 24), "unchanged geometry not regenerated");
  Require(tracker.current().generation == 1,
          "unchanged geometry preserves generation");
  Require(tracker.Update(100, 31), "resize recorded");
  Require(tracker.current().generation == 2, "resize increments generation");

  const std::string frame = BuildDiagnosticFrame(tracker.current(), 42);
  Require(frame.find(kClearScreen) != std::string::npos,
          "diagnostic frame clears alternate screen");
  Require(frame.find("Geometry: 100x31") != std::string::npos,
          "diagnostic frame reports geometry");
  Require(frame.find("Frame: 42") != std::string::npos,
          "diagnostic frame reports counter");
  Require(frame.find("\x1b[48;2;") != std::string::npos,
          "diagnostic frame contains true-colour SGR");
  Require(frame.find("\xE2\x96\x80") != std::string::npos,
          "diagnostic frame contains UTF-8 half block");

  const std::string snapshot = BuildDiagnosticSnapshot(tracker.current());
  Require(snapshot.find(kEnterAlternateScreen) == std::string::npos,
          "non-alt snapshot never enters alternate screen");
  Require(snapshot.find(kClearScreen) == std::string::npos,
          "non-alt snapshot never clears main screen");
  Require(snapshot.find("alternate screen disabled") != std::string::npos,
          "non-alt snapshot is explicit");

  if (failures != 0) {
    std::cerr << failures << " terminal logic assertion(s) failed\n";
    return 1;
  }

  std::cout << "terminal logic tests passed\n";
  return 0;
}
