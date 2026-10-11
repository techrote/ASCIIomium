#include <cmath>
#include <iostream>
#include <limits>

#include "geometry/viewport_transform.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void RequireNear(double actual, double expected, double tolerance,
                 const char* message) {
  Require(std::abs(actual - expected) <= tolerance, message);
}

asciiomium::geometry::ViewportTransform MakeStretch() {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 4;
  config.terminal_rows = 2;
  config.browser_width = 400;
  config.browser_height = 200;
  config.fit_mode = asciiomium::geometry::FitMode::Stretch;
  return asciiomium::geometry::ViewportTransform::Create(config);
}

void TestStretchMapping() {
  const auto transform = MakeStretch();

  Require(transform.sample_columns() == 4 && transform.sample_rows() == 4,
          "half-block geometry doubles logical sample rows");
  Require(transform.content_terminal_rect() ==
              asciiomium::geometry::RectD{0.0, 0.0, 4.0, 2.0},
          "stretch fills the terminal content rectangle");

  const auto top_left = transform.TerminalCellToBrowser(0, 0);
  const auto top_right = transform.TerminalCellToBrowser(3, 0);
  const auto bottom_left = transform.TerminalCellToBrowser(0, 1);
  const auto bottom_right = transform.TerminalCellToBrowser(3, 1);
  Require(top_left == asciiomium::geometry::BrowserPoint{50, 50},
          "top-left terminal cell centre maps deterministically");
  Require(top_right == asciiomium::geometry::BrowserPoint{350, 50},
          "top-right terminal cell centre maps deterministically");
  Require(bottom_left == asciiomium::geometry::BrowserPoint{50, 150},
          "bottom-left terminal cell centre maps deterministically");
  Require(bottom_right == asciiomium::geometry::BrowserPoint{350, 150},
          "bottom-right terminal cell centre maps deterministically");

  Require(!transform.TerminalCellToBrowser(-1, 0).has_value() &&
              !transform.TerminalCellToBrowser(4, 0).has_value() &&
              !transform.TerminalCellToBrowser(0, 2).has_value(),
          "terminal coordinates outside the viewport are rejected");
  Require(!transform.BrowserPixelToTerminal(-1, 0).has_value() &&
              !transform.BrowserPixelToTerminal(400, 0).has_value() &&
              !transform.BrowserPixelToTerminal(0, 200).has_value(),
          "browser coordinates use exclusive right/bottom bounds");

  Require(transform.BrowserPixelToTerminal(50, 50) ==
              asciiomium::geometry::TerminalPoint{0, 0},
          "browser-to-terminal inverse maps first-cell sample");
  Require(transform.BrowserPixelToTerminal(350, 150) ==
              asciiomium::geometry::TerminalPoint{3, 1},
          "browser-to-terminal inverse maps final-cell sample");
}

void TestContainAndCellAspect() {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 80;
  config.terminal_rows = 40;
  config.browser_width = 1600;
  config.browser_height = 900;
  config.cell_aspect = 0.5;
  config.fit_mode = asciiomium::geometry::FitMode::Contain;

  const auto transform =
      asciiomium::geometry::ViewportTransform::Create(config);
  const auto sample = transform.content_sample_rect();
  RequireNear(sample.x, 0.0, 1e-9,
              "16:9 contain uses full logical sample width");
  RequireNear(sample.width, 80.0, 1e-9,
              "contain width matches available sample columns");
  RequireNear(sample.y, 17.5, 1e-9,
              "contain introduces symmetric vertical letterbox");
  RequireNear(sample.height, 45.0, 1e-9,
              "cell aspect participates in contain height");
  Require(!transform.TerminalCellToBrowser(40, 8).has_value(),
          "terminal cell centres in top letterbox are rejected");
  Require(transform.TerminalCellToBrowser(40, 9).has_value(),
          "first terminal row whose centre is in content is accepted");
  Require(transform.TerminalCellToBrowser(40, 30).has_value(),
          "last terminal row whose centre is in content is accepted");
  Require(!transform.TerminalCellToBrowser(40, 31).has_value(),
          "terminal cell centres in bottom letterbox are rejected");

  config.cell_aspect = 0.25;
  const auto narrow_cells =
      asciiomium::geometry::ViewportTransform::Create(config);
  RequireNear(narrow_cells.content_sample_rect().height, 22.5, 1e-9,
              "changing cell aspect deterministically changes fit geometry");
}

void TestOddSizesAndReservedRows() {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 7;
  config.terminal_rows = 7;
  config.reserved_rows = 2;
  config.browser_width = 101;
  config.browser_height = 79;
  config.cell_aspect = 0.61;
  config.fit_mode = asciiomium::geometry::FitMode::Stretch;

  const auto transform =
      asciiomium::geometry::ViewportTransform::Create(config);
  Require(transform.content_rows() == 5 && transform.sample_rows() == 10,
          "reserved terminal rows are excluded from browser sample lattice");
  Require(!transform.TerminalCellToBrowser(3, 1).has_value(),
          "reserved UI rows do not map into the browser");
  Require(transform.TerminalCellToBrowser(3, 2).has_value(),
          "first browser-content terminal row maps to browser");
  Require(transform.BrowserPixelToTerminal(50, 39) ==
              asciiomium::geometry::TerminalPoint{3, 4},
          "odd browser dimensions map centre through reserved row offset");
}

void TestInvalidAndPathologicalGeometry() {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 80;
  config.terminal_rows = 40;
  config.browser_width = 960;
  config.browser_height = 540;

  auto invalid = config;
  invalid.terminal_columns = 0;
  Require(!asciiomium::geometry::ViewportTransform::TryCreate(invalid),
          "zero terminal width is rejected");

  invalid = config;
  invalid.reserved_rows = invalid.terminal_rows;
  Require(!asciiomium::geometry::ViewportTransform::TryCreate(invalid),
          "all rows cannot be reserved away");

  invalid = config;
  invalid.cell_aspect = std::numeric_limits<double>::infinity();
  Require(!asciiomium::geometry::ViewportTransform::TryCreate(invalid),
          "non-finite cell aspect is rejected");

  invalid = config;
  invalid.terminal_columns = asciiomium::geometry::kMaxTerminalColumns;
  invalid.terminal_rows = asciiomium::geometry::kMaxTerminalRows;
  Require(!asciiomium::geometry::ViewportTransform::TryCreate(invalid),
          "pathological terminal cell count is rejected");

  invalid = config;
  invalid.browser_width = asciiomium::geometry::kMaxBrowserDimension + 1;
  Require(!asciiomium::geometry::ViewportTransform::TryCreate(invalid),
          "pathological browser dimension is rejected");
}

void TestPublicationTransitions() {
  asciiomium::geometry::ViewportState state;
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 120;
  config.terminal_rows = 40;
  config.browser_width = 960;
  config.browser_height = 540;
  config.cell_aspect = 0.5;
  config.fit_mode = asciiomium::geometry::FitMode::Contain;

  auto transform = asciiomium::geometry::ViewportTransform::Create(config);
  auto first = state.Publish(transform);
  Require(first.changed && first.snapshot.generation == 1,
          "first geometry publication creates generation one");
  Require(first.terminal_changed && first.browser_changed &&
              first.sampling_changed,
          "first publication marks every geometry domain initialised");

  const auto same = state.Publish(transform);
  Require(!same.changed && same.snapshot.generation == 1,
          "unchanged geometry does not invent a generation");

  config.terminal_columns = 121;
  transform = asciiomium::geometry::ViewportTransform::Create(config);
  const auto terminal_resize = state.Publish(transform);
  Require(terminal_resize.changed &&
              terminal_resize.snapshot.generation == 2 &&
              terminal_resize.terminal_changed &&
              !terminal_resize.browser_changed,
          "terminal-only resize republishes geometry without browser resize");

  config.browser_width = 1000;
  transform = asciiomium::geometry::ViewportTransform::Create(config);
  const auto browser_resize = state.Publish(transform);
  Require(browser_resize.changed &&
              browser_resize.snapshot.generation == 3 &&
              browser_resize.browser_changed,
          "browser viewport change is distinguished from terminal resize");

  std::cout << "geometry transition: generation=" << first.snapshot.generation
            << " terminal=120x40 browser=960x540 samples=120x80\n";
  std::cout << "geometry transition: generation="
            << terminal_resize.snapshot.generation
            << " terminal=121x40 browser=960x540 samples=121x80"
            << " browser_resize=false\n";
  std::cout << "geometry transition: generation="
            << browser_resize.snapshot.generation
            << " terminal=121x40 browser=1000x540 samples=121x80"
            << " browser_resize=true\n";
}

void TestCliParsingHelpers() {
  double aspect = 0.0;
  Require(asciiomium::geometry::TryParseCellAspect("0.5", &aspect) &&
              std::abs(aspect - 0.5) < 1e-12,
          "cell-aspect CLI helper accepts a normal ratio");
  Require(!asciiomium::geometry::TryParseCellAspect("0", &aspect),
          "cell-aspect CLI helper rejects zero");
  Require(!asciiomium::geometry::TryParseCellAspect("-0.5", &aspect),
          "cell-aspect CLI helper rejects negative values");
  Require(!asciiomium::geometry::TryParseCellAspect("nan", &aspect),
          "cell-aspect CLI helper rejects NaN");
  Require(!asciiomium::geometry::TryParseCellAspect("5", &aspect),
          "cell-aspect CLI helper rejects unsafe extremes");
  Require(!asciiomium::geometry::TryParseCellAspect("0.5junk", &aspect),
          "cell-aspect CLI helper rejects trailing garbage");

  asciiomium::geometry::FitMode mode =
      asciiomium::geometry::FitMode::Stretch;
  Require(asciiomium::geometry::TryParseFitMode("contain", &mode) &&
              mode == asciiomium::geometry::FitMode::Contain,
          "fit CLI helper parses contain");
  Require(asciiomium::geometry::TryParseFitMode("stretch", &mode) &&
              mode == asciiomium::geometry::FitMode::Stretch,
          "fit CLI helper parses stretch");
  Require(!asciiomium::geometry::TryParseFitMode("crop", &mode),
          "unsupported fit policy is rejected");
}

}  // namespace

int main() {
  TestStretchMapping();
  TestContainAndCellAspect();
  TestOddSizesAndReservedRows();
  TestInvalidAndPathologicalGeometry();
  TestPublicationTransitions();
  TestCliParsingHelpers();

  if (failures != 0) {
    std::cerr << failures << " viewport-transform assertion(s) failed\n";
    return 1;
  }

  std::cout << "viewport transform tests passed\n";
  return 0;
}
