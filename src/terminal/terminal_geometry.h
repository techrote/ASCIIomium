#pragma once

#include <cstdint>

namespace asciiomium::terminal {

struct TerminalGeometry {
  int columns = 0;
  int rows = 0;
  std::uint64_t generation = 0;

  [[nodiscard]] bool valid() const noexcept {
    return columns > 0 && rows > 0;
  }
};

class GeometryTracker {
 public:
  [[nodiscard]] bool Update(int columns, int rows) noexcept;
  [[nodiscard]] const TerminalGeometry& current() const noexcept { return current_; }

 private:
  TerminalGeometry current_;
};

}  // namespace asciiomium::terminal
