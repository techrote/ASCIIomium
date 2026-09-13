#include "terminal/terminal_geometry.h"

namespace asciiomium::terminal {

bool GeometryTracker::Update(int columns, int rows) noexcept {
  if (columns <= 0 || rows <= 0) {
    return false;
  }
  if (current_.columns == columns && current_.rows == rows) {
    return false;
  }
  current_.columns = columns;
  current_.rows = rows;
  ++current_.generation;
  return true;
}

}  // namespace asciiomium::terminal
