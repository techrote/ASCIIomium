#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace asciiomium::render {

struct Rgb8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;

  friend constexpr bool operator==(const Rgb8&, const Rgb8&) = default;
};

struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

enum class PixelFormat {
  RGBA8,
  BGRA8,
};

struct ImageView {
  const std::uint8_t* data = nullptr;
  int width = 0;
  int height = 0;
  std::size_t stride_bytes = 0;
  PixelFormat format = PixelFormat::RGBA8;

  [[nodiscard]] bool valid() const noexcept {
    return data != nullptr && width > 0 && height > 0 &&
           stride_bytes >= static_cast<std::size_t>(width) * 4u;
  }
};

class ImageBuffer {
 public:
  ImageBuffer() = default;

  ImageBuffer(int width, int height, std::vector<std::uint8_t> rgba_bytes)
      : width_(width), height_(height), bytes_(std::move(rgba_bytes)) {
    if (width_ <= 0 || height_ <= 0) {
      throw std::invalid_argument("ImageBuffer dimensions must be positive");
    }
    const auto expected = static_cast<std::size_t>(width_) *
                          static_cast<std::size_t>(height_) * 4u;
    if (bytes_.size() != expected) {
      throw std::invalid_argument("ImageBuffer byte count does not match dimensions");
    }
  }

  [[nodiscard]] int width() const noexcept { return width_; }
  [[nodiscard]] int height() const noexcept { return height_; }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] ImageView view() const noexcept {
    return ImageView{bytes_.data(), width_, height_,
                     static_cast<std::size_t>(width_) * 4u,
                     PixelFormat::RGBA8};
  }

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> bytes_;
};

enum class TerminalColorKind : std::uint8_t {
  Rgb,
  Indexed,
};

// Logical cell colour ready for the VT emitter. `rgb` is always populated so
// tests, screenshots and non-terminal tools have a canonical visible colour.
// For Indexed colours, `index` is the standardized terminal palette index that
// the emitter should use instead of 24-bit SGR.
struct TerminalColor {
  TerminalColorKind kind = TerminalColorKind::Rgb;
  Rgb8 rgb{};
  std::uint8_t index = 0;

  [[nodiscard]] static constexpr TerminalColor Rgb(Rgb8 value) noexcept {
    return TerminalColor{TerminalColorKind::Rgb, value, 0};
  }

  [[nodiscard]] static constexpr TerminalColor Indexed(std::uint8_t index,
                                                       Rgb8 canonical_rgb) noexcept {
    return TerminalColor{TerminalColorKind::Indexed, canonical_rgb, index};
  }

  [[nodiscard]] constexpr bool is_indexed() const noexcept {
    return kind == TerminalColorKind::Indexed;
  }

  friend constexpr bool operator==(const TerminalColor&,
                                   const TerminalColor&) = default;
};

struct TerminalCell {
  char32_t glyph = U' ';
  TerminalColor foreground{};
  TerminalColor background{};

  friend constexpr bool operator==(const TerminalCell&, const TerminalCell&) =
      default;
};

class TerminalFrame {
 public:
  TerminalFrame() = default;

  TerminalFrame(int columns, int rows, std::vector<TerminalCell> cells)
      : columns_(columns), rows_(rows), cells_(std::move(cells)) {
    if (columns_ <= 0 || rows_ <= 0) {
      throw std::invalid_argument("TerminalFrame dimensions must be positive");
    }
    const auto expected = static_cast<std::size_t>(columns_) *
                          static_cast<std::size_t>(rows_);
    if (cells_.size() != expected) {
      throw std::invalid_argument("TerminalFrame cell count does not match dimensions");
    }
  }

  [[nodiscard]] int columns() const noexcept { return columns_; }
  [[nodiscard]] int rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t size() const noexcept { return cells_.size(); }
  [[nodiscard]] const std::vector<TerminalCell>& cells() const noexcept {
    return cells_;
  }

  [[nodiscard]] bool in_bounds(int column, int row) const noexcept {
    return column >= 0 && row >= 0 && column < columns_ && row < rows_;
  }

  [[nodiscard]] const TerminalCell* try_cell(int column, int row) const noexcept {
    if (!in_bounds(column, row)) {
      return nullptr;
    }
    return &cells_[static_cast<std::size_t>(row) *
                       static_cast<std::size_t>(columns_) +
                   static_cast<std::size_t>(column)];
  }

  [[nodiscard]] const TerminalCell& at(int column, int row) const {
    const auto* cell = try_cell(column, row);
    if (cell == nullptr) {
      throw std::out_of_range("TerminalFrame coordinate is outside the viewport");
    }
    return *cell;
  }

  friend bool operator==(const TerminalFrame&, const TerminalFrame&) = default;

 private:
  int columns_ = 0;
  int rows_ = 0;
  std::vector<TerminalCell> cells_;
};

}  // namespace asciiomium::render
