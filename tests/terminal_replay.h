#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

// Independent test-only VT subset interpreter. It consumes bytes rather than
// emitter plans/helpers and models pending autowrap, including bottom scrolling.
namespace terminal_replay {

struct Color {
  int kind = 0;  // 0: default, 1: indexed, 2: RGB
  int a = 0;
  int b = 0;
  int c = 0;
  friend bool operator==(const Color&, const Color&) = default;
};
struct Cell {
  char32_t glyph = U' ';
  Color foreground;
  Color background;
  friend bool operator==(const Cell&, const Cell&) = default;
};

class Screen {
 public:
  Screen(int columns, int rows)
      : columns_(columns), rows_(rows), cells_(columns * rows) {}

  void Apply(std::string_view bytes) {
    for (std::size_t at = 0; at < bytes.size();) {
      const auto lead = static_cast<unsigned char>(bytes[at]);
      if (lead == 27) {
        if (at + 1 >= bytes.size() || bytes[at + 1] != '[') Fail();
        at += 2;
        std::vector<int> parameters;
        int value = 0;
        bool digits = false;
        bool terminated = false;
        while (at < bytes.size()) {
          const char ch = bytes[at++];
          if (ch >= '0' && ch <= '9') {
            value = value * 10 + ch - '0';
            if (value > 100000) Fail();
            digits = true;
          } else if (ch == ';') {
            parameters.push_back(value);
            value = 0;
            digits = false;
          } else {
            if (digits || !parameters.empty()) parameters.push_back(value);
            Control(ch, parameters);
            terminated = true;
            break;
          }
        }
        if (!terminated) Fail();
      } else {
        char32_t cp = 0;
        int following = 0;
        if (lead < 0x80) cp = lead;
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; following = 1; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; following = 2; }
        else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; following = 3; }
        else Fail();
        ++at;
        for (int i = 0; i < following; ++i) {
          if (at >= bytes.size()) Fail();
          const auto next = static_cast<unsigned char>(bytes[at++]);
          if ((next & 0xC0) != 0x80) Fail();
          cp = (cp << 6) | (next & 0x3F);
        }
        if ((following == 1 && cp < 0x80) || (following == 2 && cp < 0x800) ||
            (following == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 ||
            (cp >= 0x7F && cp <= 0x9F)) Fail();
        Put(cp);
      }
    }
  }

  const std::vector<Cell>& cells() const { return cells_; }
  bool neutral() const {
    return x_ == 0 && y_ == 0 && !wrap_pending_ && foreground_ == Color{} &&
           background_ == Color{};
  }
  int scrolls() const { return scrolls_; }

 private:
  [[noreturn]] static void Fail() { throw std::runtime_error("invalid/unsupported test VT"); }

  void Control(char code, const std::vector<int>& values) {
    if (code == 'H') {
      if (values.size() > 2) Fail();
      const int row = values.empty() || values[0] == 0 ? 1 : values[0];
      const int column = values.size() < 2 || values[1] == 0 ? 1 : values[1];
      if (row < 1 || row > rows_ || column < 1 || column > columns_) Fail();
      x_ = column - 1;
      y_ = row - 1;
      wrap_pending_ = false;
    } else if (code == 'J') {
      if (values != std::vector<int>{2}) Fail();
      std::fill(cells_.begin(), cells_.end(), Cell{U' ', foreground_, background_});
    } else if (code == 'm') {
      if (values.empty() || values == std::vector<int>{0}) {
        foreground_ = {};
        background_ = {};
      } else {
        if (values[0] != 38 && values[0] != 48) Fail();
        Color color;
        if (values.size() == 3 && values[1] == 5) {
          if (values[2] < 0 || values[2] > 255) Fail();
          color = {1, values[2], 0, 0};
        } else if (values.size() == 5 && values[1] == 2) {
          for (int i = 2; i < 5; ++i) if (values[i] < 0 || values[i] > 255) Fail();
          color = {2, values[2], values[3], values[4]};
        } else Fail();
        (values[0] == 38 ? foreground_ : background_) = color;
      }
    } else Fail();
  }

  void Put(char32_t cp) {
    if (wrap_pending_) {
      x_ = 0;
      if (++y_ == rows_) {
        std::move(cells_.begin() + columns_, cells_.end(), cells_.begin());
        std::fill(cells_.end() - columns_, cells_.end(), Cell{});
        y_ = rows_ - 1;
        ++scrolls_;
      }
      wrap_pending_ = false;
    }
    cells_[y_ * columns_ + x_] = {cp, foreground_, background_};
    if (x_ + 1 == columns_) wrap_pending_ = true;
    else ++x_;
  }

  int columns_;
  int rows_;
  std::vector<Cell> cells_;
  int x_ = 0;
  int y_ = 0;
  bool wrap_pending_ = false;
  int scrolls_ = 0;
  Color foreground_;
  Color background_;
};

}  // namespace terminal_replay
