#pragma once

#include <cstdint>
#include <string_view>

#include "render/render_types.h"

namespace asciiomium::render {

enum class ColorMode : std::uint8_t {
  TrueColor,
  Indexed16,
  Indexed256,
  Rgb512,
  Rgb1024,
};

[[nodiscard]] std::string_view ColorModeName(ColorMode mode) noexcept;
[[nodiscard]] bool TryParseColorMode(std::string_view text,
                                     ColorMode* mode) noexcept;

// Canonical xterm reference RGB for an indexed colour. The first 16 entries
// are the traditional xterm base colours; 16..231 are the 6x6x6 cube and
// 232..255 are the 24-step grayscale ramp.
[[nodiscard]] Rgb8 XtermPaletteColor(std::uint8_t index) noexcept;

class ColorQuantizer {
 public:
  virtual ~ColorQuantizer() = default;
  [[nodiscard]] virtual TerminalColor Quantize(Rgb8 color) const noexcept = 0;
  [[nodiscard]] virtual ColorMode mode() const noexcept = 0;
};

// One small value object implements all baseline modes. Indexed modes retain
// their terminal index as well as canonical RGB; 512/1024 and true colour
// return RGB terminal colours only.
class ModeQuantizer final : public ColorQuantizer {
 public:
  explicit constexpr ModeQuantizer(ColorMode mode) noexcept : mode_(mode) {}

  [[nodiscard]] TerminalColor Quantize(Rgb8 color) const noexcept override;
  [[nodiscard]] ColorMode mode() const noexcept override { return mode_; }

 private:
  ColorMode mode_;
};

// Deterministic uniform channel quantisation used by the fixed RGB gamuts.
// `levels` must be >= 2. Endpoints map exactly: 0 -> 0 and 255 -> 255.
[[nodiscard]] std::uint8_t QuantizeChannel(std::uint8_t value,
                                           unsigned levels) noexcept;

}  // namespace asciiomium::render
