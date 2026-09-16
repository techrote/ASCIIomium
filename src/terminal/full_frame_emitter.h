#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "render/render_types.h"

namespace asciiomium::terminal {

struct FullFrameEmission {
  std::string bytes;
  std::size_t cell_count = 0;
  std::size_t sgr_update_count = 0;
  std::size_t cursor_move_count = 0;
};

// Serialize one complete logical frame. Every row is explicitly positioned,
// no newline/carriage-return semantics are used, and the cursor is returned
// home immediately after the final cell so a pending right-margin wrap can
// never be consumed by a subsequent printable glyph.
[[nodiscard]] FullFrameEmission SerializeFullFrame(
    const render::TerminalFrame& frame);

// Exact UTF-8 encoder used for terminal glyphs. Throws std::invalid_argument
// for non-Unicode scalar values (surrogates or values above U+10FFFF).
[[nodiscard]] std::string EncodeUtf8(char32_t codepoint);

// Human-readable representation for golden tests/debugging. ESC is rendered
// as <ESC>; other non-ASCII bytes are rendered as \xHH.
[[nodiscard]] std::string EscapeVtForDebug(std::string_view bytes);

}  // namespace asciiomium::terminal
