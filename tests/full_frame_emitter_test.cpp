#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "render/render_types.h"
#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_sequences.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

asciiomium::render::TerminalColor Rgb(unsigned r, unsigned g, unsigned b) {
  return asciiomium::render::TerminalColor::Rgb(
      {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
       static_cast<std::uint8_t>(b)});
}

asciiomium::render::TerminalColor Indexed(unsigned index,
                                          unsigned r,
                                          unsigned g,
                                          unsigned b) {
  return asciiomium::render::TerminalColor::Indexed(
      static_cast<std::uint8_t>(index),
      {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
       static_cast<std::uint8_t>(b)});
}

void TestUtf8() {
  using asciiomium::terminal::EncodeUtf8;
  Require(EncodeUtf8(U'A') == "A", "ASCII UTF-8");
  Require(EncodeUtf8(U'\u2580') == "\xE2\x96\x80", "half-block UTF-8");
  Require(EncodeUtf8(U'\U0001F600') == "\xF0\x9F\x98\x80",
          "four-byte UTF-8");

  bool surrogate_threw = false;
  try {
    (void)EncodeUtf8(static_cast<char32_t>(0xD800));
  } catch (const std::invalid_argument&) {
    surrogate_threw = true;
  }
  Require(surrogate_threw, "surrogate rejected");

  bool range_threw = false;
  try {
    (void)EncodeUtf8(static_cast<char32_t>(0x110000));
  } catch (const std::invalid_argument&) {
    range_threw = true;
  }
  Require(range_threw, "out-of-range Unicode rejected");
}

void TestSingleRowGoldenAndStateCompression() {
  using asciiomium::render::TerminalCell;
  using asciiomium::render::TerminalFrame;
  using asciiomium::terminal::SerializeFullFrame;

  const auto foreground = Indexed(9, 255, 0, 0);
  const auto background = Rgb(1, 2, 3);
  const TerminalFrame frame(
      2, 1,
      std::vector<TerminalCell>{{U'\u2580', foreground, background},
                                {U'A', foreground, background}});

  const auto emission = SerializeFullFrame(frame);
  const std::string expected =
      std::string("\x1b[H") + "\x1b[38;5;9m" + "\x1b[48;2;1;2;3m" +
      "\xE2\x96\x80" + "A" + "\x1b[H" + "\x1b[0m";

  Require(emission.bytes == expected, "single-row exact VT bytes");
  Require(emission.cell_count == 2, "single-row cell count");
  Require(emission.cursor_move_count == 2,
          "single-row start/end cursor positioning");
  Require(emission.sgr_update_count == 3,
          "unchanged colours suppress redundant SGR");
  Require(emission.bytes.find('\n') == std::string::npos,
          "emitter never uses newline wrapping");
  Require(emission.bytes.find('\r') == std::string::npos,
          "emitter never uses carriage-return wrapping");
}

void TestRowsTransitionsAndLastColumnSafety() {
  using asciiomium::render::TerminalCell;
  using asciiomium::render::TerminalFrame;
  using asciiomium::terminal::SerializeFullFrame;

  const auto black = Rgb(0, 0, 0);
  const auto red = Indexed(9, 255, 0, 0);
  const auto green = Indexed(10, 0, 255, 0);
  const TerminalFrame frame(
      2, 2,
      std::vector<TerminalCell>{{U'A', red, black}, {U'B', red, black},
                                {U'C', green, black}, {U'D', green, black}});

  const auto emission = SerializeFullFrame(frame);
  Require(emission.bytes.find("\x1b[2;1H") != std::string::npos,
          "second row explicitly positioned");
  Require(emission.bytes.ends_with(std::string("D") +
                                   asciiomium::terminal::kCursorHome +
                                   asciiomium::terminal::kResetSgr),
          "final-column glyph followed immediately by cursor motion then reset");
  Require(emission.cursor_move_count == 3,
          "two rows plus final cursor home");
  Require(emission.sgr_update_count == 4,
          "foreground transition only updates changed SGR state");
}

void TestDebugEscaping() {
  const std::string raw = std::string("\x1b[H") + "\xE2\x96\x80";
  Require(asciiomium::terminal::EscapeVtForDebug(raw) ==
              "<ESC>[H\\xE2\\x96\\x80",
          "debug escape representation");
}

void TestRepresentative160x50ByteCount() {
  using asciiomium::render::TerminalCell;
  using asciiomium::render::TerminalFrame;
  using asciiomium::terminal::SerializeFullFrame;

  constexpr int columns = 160;
  constexpr int rows = 50;
  std::vector<TerminalCell> cells;
  cells.reserve(columns * rows);
  const auto foreground = Rgb(255, 255, 255);
  const auto background = Rgb(0, 0, 0);
  for (int i = 0; i < columns * rows; ++i) {
    cells.push_back({U'\u2580', foreground, background});
  }

  const TerminalFrame frame(columns, rows, std::move(cells));
  const auto emission = SerializeFullFrame(frame);
  Require(emission.cell_count == 8000, "160x50 cell count");
  Require(emission.bytes.size() > 24000 && emission.bytes.size() < 26000,
          "160x50 uniform full frame stays near 24 KiB");
  Require(emission.sgr_update_count == 3,
          "uniform 160x50 frame has two colour SGR updates plus reset");
}

}  // namespace

int main() {
  TestUtf8();
  TestSingleRowGoldenAndStateCompression();
  TestRowsTransitionsAndLastColumnSafety();
  TestDebugEscaping();
  TestRepresentative160x50ByteCount();

  if (failures != 0) {
    std::cerr << failures << " full-frame emitter assertion(s) failed\n";
    return 1;
  }

  std::cout << "full-frame VT emitter tests passed\n";
  return 0;
}
