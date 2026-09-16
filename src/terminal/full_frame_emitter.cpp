#include "terminal/full_frame_emitter.h"

#include <optional>
#include <stdexcept>

#include "terminal/terminal_sequences.h"

namespace asciiomium::terminal {
namespace {

void AppendColor(std::string* output,
                 const render::TerminalColor& color,
                 bool foreground) {
  if (color.is_indexed()) {
    *output += foreground ? BuildIndexedForeground(color.index)
                          : BuildIndexedBackground(color.index);
    return;
  }

  const auto rgb = color.rgb;
  *output += foreground ? BuildRgbForeground(rgb.r, rgb.g, rgb.b)
                        : BuildRgbBackground(rgb.r, rgb.g, rgb.b);
}

[[nodiscard]] bool IsUnicodeScalar(char32_t codepoint) noexcept {
  return codepoint <= 0x10FFFF &&
         !(codepoint >= 0xD800 && codepoint <= 0xDFFF);
}

}  // namespace

std::string EncodeUtf8(char32_t codepoint) {
  if (!IsUnicodeScalar(codepoint)) {
    throw std::invalid_argument("terminal glyph is not a Unicode scalar value");
  }

  std::string output;
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
    return output;
  }
  if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    return output;
  }
  if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    return output;
  }

  output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
  output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
  output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
  output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  return output;
}

FullFrameEmission SerializeFullFrame(const render::TerminalFrame& frame) {
  if (frame.columns() <= 0 || frame.rows() <= 0 || frame.size() == 0) {
    throw std::invalid_argument("full-frame VT emitter requires a non-empty frame");
  }

  FullFrameEmission emission;
  emission.cell_count = frame.size();

  // The output is intentionally assembled into one contiguous buffer. The
  // TerminalSession then writes it in large chunks rather than issuing a
  // syscall for each cell.
  emission.bytes.reserve(frame.size() * 8u);

  std::optional<render::TerminalColor> current_foreground;
  std::optional<render::TerminalColor> current_background;

  for (int row = 0; row < frame.rows(); ++row) {
    if (row == 0) {
      emission.bytes += kCursorHome;
    } else {
      emission.bytes += BuildCursorPosition(row + 1, 1);
    }
    ++emission.cursor_move_count;

    for (int column = 0; column < frame.columns(); ++column) {
      const auto& cell = frame.at(column, row);

      if (!current_foreground.has_value() ||
          current_foreground.value() != cell.foreground) {
        AppendColor(&emission.bytes, cell.foreground, true);
        current_foreground = cell.foreground;
        ++emission.sgr_update_count;
      }
      if (!current_background.has_value() ||
          current_background.value() != cell.background) {
        AppendColor(&emission.bytes, cell.background, false);
        current_background = cell.background;
        ++emission.sgr_update_count;
      }

      emission.bytes += EncodeUtf8(cell.glyph);
    }
  }

  // Right-margin safety: after the final cell, emit cursor motion before any
  // later graphic byte. VT autowrap only becomes dangerous when a subsequent
  // printable character consumes the pending wrap. CUP also avoids mutating
  // DECAWM, whose previous private-mode state cannot be queried reliably via
  // the Windows Console API.
  emission.bytes += kCursorHome;
  ++emission.cursor_move_count;
  emission.bytes += kResetSgr;
  ++emission.sgr_update_count;

  return emission;
}

std::string EscapeVtForDebug(std::string_view bytes) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(bytes.size() * 2u);

  for (const unsigned char byte : bytes) {
    if (byte == 0x1B) {
      escaped += "<ESC>";
      continue;
    }
    if (byte >= 0x20 && byte <= 0x7E) {
      escaped.push_back(static_cast<char>(byte));
      continue;
    }
    escaped += "\\x";
    escaped.push_back(kHex[(byte >> 4) & 0x0F]);
    escaped.push_back(kHex[byte & 0x0F]);
  }

  return escaped;
}

}  // namespace asciiomium::terminal
