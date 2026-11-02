#include "terminal/differential_frame_emitter.h"

#include <optional>
#include <stdexcept>

#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_sequences.h"

namespace asciiomium::terminal {
namespace {

struct ColorState {
  std::optional<render::TerminalColor> foreground;
  std::optional<render::TerminalColor> background;
};

std::size_t Digits(unsigned value) noexcept {
  std::size_t count = 1;
  while (value >= 10) {
    value /= 10;
    ++count;
  }
  return count;
}

std::size_t ColorBytes(const render::TerminalColor& color) noexcept {
  if (color.is_indexed()) return 8u + Digits(color.index);
  return 10u + Digits(color.rgb.r) + Digits(color.rgb.g) + Digits(color.rgb.b);
}

std::size_t GlyphBytes(char32_t codepoint) {
  if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    throw std::invalid_argument("terminal glyph is not a Unicode scalar value");
  }
  // Control characters are terminal commands, not single-cell glyphs. The
  // baseline renderer produces U+2580; rejecting controls protects this API
  // without pretending to implement arbitrary Unicode width measurement.
  if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F)) {
    throw std::invalid_argument("differential terminal glyph is a control character");
  }
  if (codepoint <= 0x7F) return 1;
  if (codepoint <= 0x7FF) return 2;
  if (codepoint <= 0xFFFF) return 3;
  return 4;
}

std::size_t CursorBytes(int row, int column) noexcept {
  if (row == 0 && column == 0) return 3;  // ESC [ H
  return 4u + Digits(static_cast<unsigned>(row + 1)) +
         Digits(static_cast<unsigned>(column + 1));
}

std::size_t CellBytes(const render::TerminalCell& cell, ColorState* state) {
  std::size_t bytes = GlyphBytes(cell.glyph);
  if (!state->foreground || *state->foreground != cell.foreground) {
    bytes += ColorBytes(cell.foreground);
    state->foreground = cell.foreground;
  }
  if (!state->background || *state->background != cell.background) {
    bytes += ColorBytes(cell.background);
    state->background = cell.background;
  }
  return bytes;
}

void AppendColor(std::string* output, const render::TerminalColor& color,
                 bool foreground) {
  if (color.is_indexed()) {
    *output += foreground ? BuildIndexedForeground(color.index)
                          : BuildIndexedBackground(color.index);
  } else {
    const auto rgb = color.rgb;
    *output += foreground ? BuildRgbForeground(rgb.r, rgb.g, rgb.b)
                          : BuildRgbBackground(rgb.r, rgb.g, rgb.b);
  }
}

void AppendCell(const render::TerminalCell& cell, ColorState* state,
                DifferentialEmission* emission) {
  (void)GlyphBytes(cell.glyph);
  if (!state->foreground || *state->foreground != cell.foreground) {
    AppendColor(&emission->bytes, cell.foreground, true);
    state->foreground = cell.foreground;
    ++emission->sgr_update_count;
  }
  if (!state->background || *state->background != cell.background) {
    AppendColor(&emission->bytes, cell.background, false);
    state->background = cell.background;
    ++emission->sgr_update_count;
  }
  emission->bytes += EncodeUtf8(cell.glyph);
  ++emission->written_cells;
}

}  // namespace

DifferentialPlan PlanDifferentialFrame(const render::TerminalFrame* previous,
                                      const render::TerminalFrame& current,
                                      bool force_full) {
  if (current.columns() <= 0 || current.rows() <= 0 || current.size() == 0) {
    throw std::invalid_argument("differential VT emitter requires a non-empty frame");
  }
  DifferentialPlan plan;
  plan.columns = current.columns();
  plan.rows = current.rows();
  const bool comparable = previous && previous->columns() == current.columns() &&
                          previous->rows() == current.rows();
  plan.full_reconstruction = force_full || !comparable;
  ColorState state;

  for (int row = 0; row < current.rows(); ++row) {
    int column = 0;
    while (column < current.columns()) {
      if (comparable && current.at(column, row) == previous->at(column, row)) {
        ++column;
        continue;
      }
      const int first = column;
      do {
        ++column;
      } while (column < current.columns() &&
               (!comparable || current.at(column, row) != previous->at(column, row)));
      plan.changed_cells += static_cast<std::size_t>(column - first);
      ++plan.changed_runs;
      if (plan.full_reconstruction) continue;

      int write_from = first;
      bool bridge = false;
      if (!plan.runs.empty() && plan.runs.back().row == row) {
        const auto& last = plan.runs.back();
        const int gap_start = last.first_column + last.cell_count;
        auto jump_state = state;
        const auto jump_bytes = CursorBytes(row, first) +
                                CellBytes(current.at(first, row), &jump_state);
        auto bridge_state = state;
        std::size_t bridge_bytes = 0;
        // Include the first changed cell: intervening colours can alter its
        // SGR cost. Stop once bridging cannot be cheaper, keeping work bounded.
        for (int gap = gap_start; gap <= first && bridge_bytes <= jump_bytes; ++gap) {
          bridge_bytes += CellBytes(current.at(gap, row), &bridge_state);
        }
        bridge = bridge_bytes <= jump_bytes;
        if (bridge) write_from = gap_start;
      }
      if (bridge) {
        plan.runs.back().cell_count = column - plan.runs.back().first_column;
      } else {
        plan.runs.push_back({row, first, column - first});
        plan.estimated_bytes += CursorBytes(row, first);
      }
      for (int c = write_from; c < column; ++c) {
        plan.estimated_bytes += CellBytes(current.at(c, row), &state);
        ++plan.written_cells;
      }
    }
    if (plan.full_reconstruction) {
      plan.runs.push_back({row, 0, current.columns()});
      plan.estimated_bytes += CursorBytes(row, 0);
      for (int c = 0; c < current.columns(); ++c) {
        plan.estimated_bytes += CellBytes(current.at(c, row), &state);
        ++plan.written_cells;
      }
    }
  }
  if (!plan.runs.empty()) {
    // Every nonempty payload leaves the cursor home and colours reset. No
    // cross-frame SGR assumptions survive beyond one plan/serialization pair.
    plan.estimated_bytes += 7;  // ESC [ H + ESC [ 0 m
    if (plan.full_reconstruction) plan.estimated_bytes += 8;  // reset + ED 2
  }
  return plan;
}

DifferentialEmission SerializeDifferentialFrame(const render::TerminalFrame& current,
                                                const DifferentialPlan& plan) {
  if (current.columns() <= 0 || current.rows() <= 0 ||
      current.columns() != plan.columns || current.rows() != plan.rows) {
    throw std::invalid_argument("differential plan/frame geometry mismatch");
  }
  DifferentialEmission emission;
  if (plan.runs.empty()) return emission;
  // A newly generated plan's exact byte estimate avoids repeated reallocations.
  // Bound the reservation by this frame, even for an accidentally corrupt plan.
  if (plan.estimated_bytes <= current.size() * 64u +
                                  static_cast<std::size_t>(current.rows()) * 32u + 16u) {
    emission.bytes.reserve(plan.estimated_bytes);
  }
  ColorState state;
  if (plan.full_reconstruction) {
    // ED uses the current background; reset first. This also removes unknown
    // inherited style state and erases cells outside a shrinking content grid.
    emission.bytes += kResetSgr;
    emission.bytes += kClearScreen;
    ++emission.sgr_update_count;
  }
  int prior_row = -1;
  int prior_end = 0;
  for (const auto& run : plan.runs) {
    if (run.row < 0 || run.row >= current.rows() || run.first_column < 0 ||
        run.cell_count <= 0 || run.first_column >= current.columns() ||
        run.cell_count > current.columns() - run.first_column ||
        run.row < prior_row || (run.row == prior_row && run.first_column < prior_end)) {
      throw std::invalid_argument("invalid differential cell run");
    }
    emission.bytes += run.row == 0 && run.first_column == 0
                          ? kCursorHome
                          : BuildCursorPosition(run.row + 1, run.first_column + 1);
    ++emission.cursor_move_count;
    for (int c = run.first_column; c < run.first_column + run.cell_count; ++c) {
      AppendCell(current.at(c, run.row), &state, &emission);
    }
    prior_row = run.row;
    prior_end = run.first_column + run.cell_count;
  }
  // CUP must precede any later printable byte after a right-margin write.
  emission.bytes += kCursorHome;
  emission.bytes += kResetSgr;
  ++emission.cursor_move_count;
  ++emission.sgr_update_count;
  return emission;
}

}  // namespace asciiomium::terminal
