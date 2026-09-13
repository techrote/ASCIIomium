#pragma once

#include <cstdint>
#include <string>

#include "terminal/terminal_geometry.h"

namespace asciiomium::terminal {

std::string BuildDiagnosticFrame(const TerminalGeometry& geometry,
                                 std::uint64_t frame_counter);
std::string BuildDiagnosticSnapshot(const TerminalGeometry& geometry);

}  // namespace asciiomium::terminal
