#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "render/render_types.h"

namespace asciiomium::terminal {

struct CellRun {
  int row = 0;
  int first_column = 0;
  int cell_count = 0;
};

// A plan refers to the immutable current frame supplied to PlanDifferentialFrame.
// Runs are ordered, non-overlapping and never cross a row. Memory is O(cells).
struct DifferentialPlan {
  int columns = 0;
  int rows = 0;
  std::vector<CellRun> runs;
  std::size_t changed_cells = 0;
  std::size_t changed_runs = 0;
  std::size_t written_cells = 0;
  std::size_t estimated_bytes = 0;
  bool full_reconstruction = false;
};

struct DifferentialEmission {
  std::string bytes;
  std::size_t written_cells = 0;
  std::size_t sgr_update_count = 0;
  std::size_t cursor_move_count = 0;
};

// The caller owns the last successfully written frame. Never replace that
// reference until output succeeds; invalidate it after any recovered partial
// write, external terminal write, geometry change or loss of terminal ownership.
// A null reference, changed geometry or force_full clears and reconstructs the
// screen. Identical trusted frames produce no runs and no bytes.
[[nodiscard]] DifferentialPlan PlanDifferentialFrame(
    const render::TerminalFrame* previous,
    const render::TerminalFrame& current,
    bool force_full = false);

// All printable glyphs must occupy exactly one terminal cell, as the baseline
// half-block renderer does. Wide/combining glyph layout is outside this contract.
// The plan and frame must remain paired; serialize before mutating/replacing it.
[[nodiscard]] DifferentialEmission SerializeDifferentialFrame(
    const render::TerminalFrame& current,
    const DifferentialPlan& plan);

}  // namespace asciiomium::terminal
