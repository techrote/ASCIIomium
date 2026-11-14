#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "render/color_quantizer.h"
#include "terminal/differential_frame_emitter.h"
#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_sequences.h"
#include "terminal_replay.h"

namespace {
using namespace asciiomium::render;
using namespace asciiomium::terminal;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

TerminalColor Rgb(unsigned r, unsigned g, unsigned b) {
  return TerminalColor::Rgb({static_cast<std::uint8_t>(r),
                             static_cast<std::uint8_t>(g),
                             static_cast<std::uint8_t>(b)});
}
TerminalFrame Uniform(int columns, int rows, char32_t glyph = U'\u2580') {
  return TerminalFrame(columns, rows, std::vector<TerminalCell>(
      static_cast<std::size_t>(columns * rows), {glyph, Rgb(240, 240, 240), Rgb(0, 0, 0)}));
}
TerminalFrame Replace(const TerminalFrame& frame, std::size_t index, TerminalCell cell) {
  auto cells = frame.cells();
  cells.at(index) = cell;
  return TerminalFrame(frame.columns(), frame.rows(), std::move(cells));
}

DifferentialPlan Verify(const TerminalFrame* previous, const TerminalFrame& current,
                        terminal_replay::Screen* actual, bool force = false,
                        int screen_columns = 0, int screen_rows = 0) {
  const auto plan = PlanDifferentialFrame(previous, current, force);
  const auto emission = SerializeDifferentialFrame(current, plan);
  Require(plan.estimated_bytes == emission.bytes.size(), "cost estimate differs from real bytes");
  Require(plan.written_cells == emission.written_cells, "written cell count mismatch");
  actual->Apply(emission.bytes);
  terminal_replay::Screen expected(screen_columns ? screen_columns : current.columns(),
                                   screen_rows ? screen_rows : current.rows());
  expected.Apply(std::string(kResetSgr) + kClearScreen + SerializeFullFrame(current).bytes);
  Require(actual->cells() == expected.cells(), "delta logical display differs from full-frame oracle");
  Require(actual->neutral(), "non-neutral cursor/SGR/pending wrap after delta");
  Require(actual->scrolls() == 0, "emitter accidentally scrolled terminal");
  if (plan.full_reconstruction) {
    Require(emission.bytes == std::string(kResetSgr) + kClearScreen +
                                  SerializeFullFrame(current).bytes,
            "full reconstruction differs from unchanged reference emitter");
  }
  return plan;
}

void TestStaticAndChanges() {
  auto frame = Uniform(12, 5);
  terminal_replay::Screen screen(12, 5);
  auto plan = Verify(nullptr, frame, &screen);
  Require(plan.changed_cells == 60 && plan.changed_runs == 5, "initial counters");
  for (int i = 0; i < 20; ++i) {
    plan = Verify(&frame, frame, &screen);
    Require(plan.runs.empty() && plan.estimated_bytes == 0 && plan.changed_cells == 0,
            "identical frames emit nothing");
  }
  const std::array<std::size_t, 5> indices{0, 11, 23, 42, 59};
  const std::array<char32_t, 5> glyphs{U'A', U'\u2584', U' ', U'\u2588', U'\u00E9'};
  for (std::size_t i = 0; i < indices.size(); ++i) {
    auto cell = frame.cells()[indices[i]];
    cell.glyph = glyphs[i];
    auto next = Replace(frame, indices[i], cell);
    plan = Verify(&frame, next, &screen);
    Require(plan.changed_cells == 1 && plan.changed_runs == 1, "glyph-only change counted");
    frame = std::move(next);
  }
  auto row_cells = frame.cells();
  for (int column = 0; column < frame.columns(); ++column) {
    row_cells[frame.columns() * 2 + column].glyph = U'Z';
  }
  auto row_changed = TerminalFrame(frame.columns(), frame.rows(), row_cells);
  plan = Verify(&frame, row_changed, &screen);
  Require(plan.changed_cells == 12 && plan.changed_runs == 1 && plan.runs.size() == 1,
          "whole changed row is one run");
  frame = std::move(row_changed);
  auto cell = frame.at(0, 0);
  cell.foreground = Rgb(1, 20, 255);
  auto next = Replace(frame, 0, cell);
  Verify(&frame, next, &screen);
  frame = next;
  cell.background = TerminalColor::Indexed(123, XtermPaletteColor(123));
  next = Replace(frame, 0, cell);
  Verify(&frame, next, &screen);
  frame = next;
  for (int i = 0; i < 3; ++i) {
    plan = Verify(&frame, frame, &screen, true);
    Require(plan.full_reconstruction && plan.changed_cells == 0 && plan.written_cells == 60,
            "forced reconstruction distinguishes changes from written cells");
  }
}

void TestCostAwareGaps() {
  auto previous = Uniform(24, 1, U'a');
  auto cells = previous.cells();
  cells[1].glyph = U'b';
  cells[3].glyph = U'b';
  auto current = TerminalFrame(24, 1, cells);
  terminal_replay::Screen screen(24, 1);
  Verify(nullptr, previous, &screen);
  auto plan = Verify(&previous, current, &screen);
  Require(plan.changed_runs == 2 && plan.runs.size() == 1 && plan.written_cells == 3,
          "short cheap gap should be written through");

  previous = Uniform(24, 1);
  cells = previous.cells();
  cells[0].glyph = U'A';
  cells[23].glyph = U'B';
  current = TerminalFrame(24, 1, cells);
  Verify(nullptr, previous, &screen);
  plan = Verify(&previous, current, &screen);
  Require(plan.runs.size() == 2 && plan.written_cells == 2, "long gap should use cursor move");

  // A one-cell ASCII gap would be cheap by character count but both its
  // foreground and background transitions make jumping cheaper.
  cells = Uniform(8, 1, U'a').cells();
  cells[2].foreground = Rgb(1, 2, 3);
  cells[2].background = Rgb(255, 127, 11);
  previous = TerminalFrame(8, 1, cells);
  cells[1].glyph = U'b';
  cells[3].glyph = U'b';
  current = TerminalFrame(8, 1, cells);
  terminal_replay::Screen colors(8, 1);
  Verify(nullptr, previous, &colors);
  plan = Verify(&previous, current, &colors);
  Require(plan.runs.size() == 2 && plan.written_cells == 2,
          "gap planner must account for SGR transitions");
}

void TestGeometryAndCorruption() {
  auto previous = Uniform(25, 12);
  terminal_replay::Screen screen(25, 12);
  Verify(nullptr, previous, &screen);
  auto smaller = Uniform(5, 2, U' ');
  auto plan = Verify(&previous, smaller, &screen, false, 25, 12);
  Require(plan.full_reconstruction, "shrink invalidates reference");
  auto bigger = Uniform(13, 7, U'X');
  Verify(&smaller, bigger, &screen, false, 25, 12);
  screen.Apply("\x1b[1;1Hcorrupt\x1b[H");
  Verify(&bigger, bigger, &screen, true, 25, 12);
  auto cleared = Uniform(13, 7, U' ');
  Verify(&bigger, cleared, &screen, false, 25, 12);
}

void TestRandomTransitionsAndModes() {
  std::mt19937 random(0xA5C110);
  constexpr std::array<ColorMode, 5> modes{ColorMode::Indexed16, ColorMode::Indexed256,
      ColorMode::Rgb512, ColorMode::Rgb1024, ColorMode::TrueColor};
  constexpr std::array<char32_t, 5> glyphs{U'A', U' ', U'\u2580', U'\u2584', U'\u2588'};
  auto previous = Uniform(31, 9);
  terminal_replay::Screen screen(31, 9);
  Verify(nullptr, previous, &screen);
  for (int step = 0; step < 400; ++step) {
    auto cells = previous.cells();
    const ModeQuantizer quantizer(modes[step % modes.size()]);
    const int edits = step % 11 == 0 ? 279 : 1 + static_cast<int>(random() % 30);
    for (int i = 0; i < edits; ++i) {
      auto& cell = cells[random() % cells.size()];
      cell.glyph = glyphs[random() % glyphs.size()];
      cell.foreground = quantizer.Quantize({static_cast<std::uint8_t>(random()),
                                            static_cast<std::uint8_t>(random()),
                                            static_cast<std::uint8_t>(random())});
      cell.background = quantizer.Quantize({static_cast<std::uint8_t>(random()),
                                            static_cast<std::uint8_t>(random()),
                                            static_cast<std::uint8_t>(random())});
    }
    auto current = TerminalFrame(31, 9, std::move(cells));
    Verify(&previous, current, &screen, step % 29 == 0);
    previous = std::move(current);
  }
}

void TestBoundsAndFailures() {
  bool threw = false;
  try { (void)PlanDifferentialFrame(nullptr, {}); }
  catch (const std::invalid_argument&) { threw = true; }
  Require(threw, "default empty frame rejected");
  for (char32_t glyph : {char32_t(0xD800), char32_t(0x110000), char32_t(27), char32_t(10)}) {
    threw = false;
    try { (void)PlanDifferentialFrame(nullptr, Uniform(1, 1, glyph)); }
    catch (const std::invalid_argument&) { threw = true; }
    Require(threw, "invalid glyph rejected before output");
  }
  auto current = Uniform(2, 2);
  auto plan = PlanDifferentialFrame(nullptr, current);
  threw = false;
  try { (void)SerializeDifferentialFrame(Uniform(3, 2), plan); }
  catch (const std::invalid_argument&) { threw = true; }
  Require(threw, "plan from different geometry rejected");
  plan.runs[0].cell_count = 100;
  threw = false;
  try { (void)SerializeDifferentialFrame(current, plan); }
  catch (const std::invalid_argument&) { threw = true; }
  Require(threw, "corrupt plan bounds rejected");

  // The replay oracle actually detects the delayed-wrap bug we guard against.
  terminal_replay::Screen screen(2, 1);
  screen.Apply("ABC");
  Require(screen.scrolls() == 1, "replay oracle models bottom-right wrap scroll");
}
}  // namespace

int main() {
  try {
    TestStaticAndChanges();
    TestCostAwareGaps();
    TestGeometryAndCorruption();
    TestRandomTransitionsAndModes();
    TestBoundsAndFailures();
    std::cout << "differential emitter: independent VT replay, 400 seeded transitions, "
                 "all colour modes, geometry, invalidation and byte-cost checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
