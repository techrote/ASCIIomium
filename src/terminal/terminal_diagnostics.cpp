#include "terminal/terminal_diagnostics.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <string_view>

#include "terminal/terminal_sequences.h"

namespace asciiomium::terminal {
namespace {

constexpr std::string_view kUpperHalfBlock = "\xE2\x96\x80";

std::string ClipAscii(std::string text, int width) {
  if (width <= 0) {
    return {};
  }
  if (static_cast<int>(text.size()) > width) {
    text.resize(static_cast<std::size_t>(width));
  }
  return text;
}

void AppendPositionedPlainLine(std::string& output,
                               const TerminalGeometry& geometry,
                               int row,
                               std::string text) {
  if (row < 1 || row > geometry.rows) {
    return;
  }
  const int width = std::max(1, geometry.columns - 1);
  output += BuildCursorPosition(row, 1);
  output += ClipAscii(std::move(text), width);
}

void AppendColourBar(std::string& output,
                     const TerminalGeometry& geometry,
                     int row,
                     bool positioned) {
  if (positioned && (row < 1 || row > geometry.rows)) {
    return;
  }

  constexpr std::array<std::array<unsigned char, 3>, 8> colours{{
      {255, 72, 72},
      {255, 168, 64},
      {255, 230, 96},
      {92, 220, 128},
      {72, 196, 255},
      {104, 120, 255},
      {190, 96, 255},
      {255, 104, 196},
  }};

  const std::string label = "RGB ";
  const int max_columns = std::max(1, geometry.columns - 1);
  int used_columns = static_cast<int>(label.size());

  if (positioned) {
    output += BuildCursorPosition(row, 1);
  }
  output += ClipAscii(label, max_columns);

  for (const auto& colour : colours) {
    if (used_columns + 2 > max_columns) {
      break;
    }
    output += BuildRgbBackground(colour[0], colour[1], colour[2]);
    output += "  ";
    used_columns += 2;
  }
  output += kResetSgr;
}

void AppendHalfBlocks(std::string& output,
                      const TerminalGeometry& geometry,
                      int row,
                      bool positioned) {
  if (positioned && (row < 1 || row > geometry.rows)) {
    return;
  }

  const std::string label = "Blocks ";
  const int max_columns = std::max(1, geometry.columns - 1);
  int used_columns = static_cast<int>(label.size());

  if (positioned) {
    output += BuildCursorPosition(row, 1);
  }
  output += ClipAscii(label, max_columns);

  for (int index = 0; index < 24 && used_columns + 1 <= max_columns; ++index) {
    const auto red = static_cast<unsigned char>((index * 41) % 256);
    const auto green = static_cast<unsigned char>((index * 73 + 80) % 256);
    const auto blue = static_cast<unsigned char>((index * 29 + 160) % 256);
    const auto bg_red = static_cast<unsigned char>((red + 96) % 256);
    const auto bg_green = static_cast<unsigned char>((green + 48) % 256);
    const auto bg_blue = static_cast<unsigned char>((blue + 128) % 256);
    output += BuildRgbForeground(red, green, blue);
    output += BuildRgbBackground(bg_red, bg_green, bg_blue);
    output += kUpperHalfBlock;
    ++used_columns;
  }
  output += kResetSgr;
}

}  // namespace

std::string BuildDiagnosticFrame(const TerminalGeometry& geometry,
                                 std::uint64_t frame_counter) {
  std::string output;
  output.reserve(1024);
  output += kCursorHome;
  output += kClearScreen;

  AppendPositionedPlainLine(output, geometry, 1,
                            "ASCIIomium terminal diagnostics");

  std::ostringstream geometry_line;
  geometry_line << "Geometry: " << geometry.columns << 'x' << geometry.rows
                << "  generation=" << geometry.generation;
  AppendPositionedPlainLine(output, geometry, 2, geometry_line.str());

  std::ostringstream frame_line;
  frame_line << "Frame: " << frame_counter
             << "  Resize the terminal to exercise geometry tracking.";
  AppendPositionedPlainLine(output, geometry, 3, frame_line.str());

  AppendPositionedPlainLine(output, geometry, 4,
                            "Ctrl+C exits through RAII restoration. Mouse tracking is OFF.");
  AppendColourBar(output, geometry, 6, true);
  AppendHalfBlocks(output, geometry, 8, true);
  AppendPositionedPlainLine(output, geometry, 10,
                            "VT output + UTF-8 code pages active; browser/CEF remains idle.");

  return output;
}

std::string BuildDiagnosticSnapshot(const TerminalGeometry& geometry) {
  const int width = std::max(1, geometry.columns - 1);
  std::ostringstream header;
  header << "ASCIIomium terminal diagnostics snapshot\r\n"
         << "Geometry: " << geometry.columns << 'x' << geometry.rows
         << "  generation=" << geometry.generation << "\r\n"
         << "VT output + UTF-8 code pages active; alternate screen disabled.\r\n";

  std::string output = header.str();
  output += ClipAscii("RGB/Unicode capability samples:", width);
  output += "\r\n";
  AppendColourBar(output, geometry, 0, false);
  output += "\r\n";
  AppendHalfBlocks(output, geometry, 0, false);
  output += "\r\n";
  output += kResetSgr;
  return output;
}

}  // namespace asciiomium::terminal
