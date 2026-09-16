#include "render/color_quantizer.h"

#include <array>
#include <cstdint>
#include <limits>

namespace asciiomium::render {
namespace {

constexpr std::array<Rgb8, 16> kXterm16 = {{
    {0, 0, 0},       {128, 0, 0},     {0, 128, 0},     {128, 128, 0},
    {0, 0, 128},     {128, 0, 128},   {0, 128, 128},   {192, 192, 192},
    {128, 128, 128}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {0, 0, 255},     {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
}};

constexpr std::array<std::uint8_t, 6> kCubeLevels = {
    0, 95, 135, 175, 215, 255};

[[nodiscard]] std::uint32_t DistanceSquared(Rgb8 a, Rgb8 b) noexcept {
  const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
  const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
  const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
  return static_cast<std::uint32_t>(dr * dr + dg * dg + db * db);
}

void ConsiderIndexed(Rgb8 input,
                     std::uint8_t index,
                     std::uint32_t* best_distance,
                     std::uint8_t* best_index) noexcept {
  const auto distance = DistanceSquared(input, XtermPaletteColor(index));
  if (distance < *best_distance ||
      (distance == *best_distance && index < *best_index)) {
    *best_distance = distance;
    *best_index = index;
  }
}

[[nodiscard]] unsigned NearestCubeLevel(std::uint8_t value) noexcept {
  unsigned best = 0;
  unsigned best_distance = std::numeric_limits<unsigned>::max();
  for (unsigned i = 0; i < kCubeLevels.size(); ++i) {
    const int delta = static_cast<int>(value) - static_cast<int>(kCubeLevels[i]);
    const unsigned distance = static_cast<unsigned>(delta * delta);
    if (distance < best_distance) {
      best_distance = distance;
      best = i;
    }
  }
  return best;
}

[[nodiscard]] TerminalColor QuantizeIndexed16(Rgb8 input) noexcept {
  std::uint8_t best_index = 0;
  std::uint32_t best_distance = std::numeric_limits<std::uint32_t>::max();
  for (std::uint8_t index = 0; index < 16; ++index) {
    ConsiderIndexed(input, index, &best_distance, &best_index);
  }
  return TerminalColor::Indexed(best_index, XtermPaletteColor(best_index));
}

[[nodiscard]] TerminalColor QuantizeIndexed256(Rgb8 input) noexcept {
  std::uint8_t best_index = 0;
  std::uint32_t best_distance = std::numeric_limits<std::uint32_t>::max();

  // Base 16 colours can beat both cube and grayscale candidates.
  for (std::uint8_t index = 0; index < 16; ++index) {
    ConsiderIndexed(input, index, &best_distance, &best_index);
  }

  // Euclidean distance over the 6x6x6 cube is separable by channel, so one
  // nearest level per channel yields the globally nearest cube entry.
  const unsigned r = NearestCubeLevel(input.r);
  const unsigned g = NearestCubeLevel(input.g);
  const unsigned b = NearestCubeLevel(input.b);
  const auto cube_index = static_cast<std::uint8_t>(16u + 36u * r + 6u * g + b);
  ConsiderIndexed(input, cube_index, &best_distance, &best_index);

  // Grayscale is small enough to scan exactly. Ties always prefer the lower
  // terminal index, making mapping stable across implementations/runs.
  for (unsigned gray = 0; gray < 24; ++gray) {
    const auto index = static_cast<std::uint8_t>(232u + gray);
    ConsiderIndexed(input, index, &best_distance, &best_index);
  }

  return TerminalColor::Indexed(best_index, XtermPaletteColor(best_index));
}

[[nodiscard]] Rgb8 QuantizeRgbCube(Rgb8 input,
                                   unsigned red_levels,
                                   unsigned green_levels,
                                   unsigned blue_levels) noexcept {
  return Rgb8{QuantizeChannel(input.r, red_levels),
              QuantizeChannel(input.g, green_levels),
              QuantizeChannel(input.b, blue_levels)};
}

}  // namespace

std::string_view ColorModeName(ColorMode mode) noexcept {
  switch (mode) {
    case ColorMode::TrueColor:
      return "true";
    case ColorMode::Indexed16:
      return "16";
    case ColorMode::Indexed256:
      return "256";
    case ColorMode::Rgb512:
      return "512";
    case ColorMode::Rgb1024:
      return "1024";
  }
  return "unknown";
}

bool TryParseColorMode(std::string_view text, ColorMode* mode) noexcept {
  if (mode == nullptr) {
    return false;
  }
  if (text == "true" || text == "truecolor" || text == "24bit") {
    *mode = ColorMode::TrueColor;
    return true;
  }
  if (text == "16") {
    *mode = ColorMode::Indexed16;
    return true;
  }
  if (text == "256") {
    *mode = ColorMode::Indexed256;
    return true;
  }
  if (text == "512") {
    *mode = ColorMode::Rgb512;
    return true;
  }
  if (text == "1024") {
    *mode = ColorMode::Rgb1024;
    return true;
  }
  return false;
}

Rgb8 XtermPaletteColor(std::uint8_t index) noexcept {
  if (index < 16) {
    return kXterm16[index];
  }
  if (index < 232) {
    const unsigned cube = static_cast<unsigned>(index) - 16u;
    const unsigned r = cube / 36u;
    const unsigned g = (cube / 6u) % 6u;
    const unsigned b = cube % 6u;
    return Rgb8{kCubeLevels[r], kCubeLevels[g], kCubeLevels[b]};
  }
  const auto gray = static_cast<std::uint8_t>(8u +
      10u * (static_cast<unsigned>(index) - 232u));
  return Rgb8{gray, gray, gray};
}

std::uint8_t QuantizeChannel(std::uint8_t value, unsigned levels) noexcept {
  if (levels < 2) {
    return value;
  }
  const unsigned level =
      (static_cast<unsigned>(value) * (levels - 1u) + 127u) / 255u;
  const unsigned reconstructed =
      (level * 255u + (levels - 1u) / 2u) / (levels - 1u);
  return static_cast<std::uint8_t>(reconstructed);
}

TerminalColor ModeQuantizer::Quantize(Rgb8 color) const noexcept {
  switch (mode_) {
    case ColorMode::TrueColor:
      return TerminalColor::Rgb(color);
    case ColorMode::Indexed16:
      return QuantizeIndexed16(color);
    case ColorMode::Indexed256:
      return QuantizeIndexed256(color);
    case ColorMode::Rgb512:
      // 3/3/3 bits -> 8 x 8 x 8 = 512 fixed RGB colours.
      return TerminalColor::Rgb(QuantizeRgbCube(color, 8, 8, 8));
    case ColorMode::Rgb1024:
      // 3/4/3 bits -> 8 x 16 x 8 = 1024 fixed RGB colours.
      return TerminalColor::Rgb(QuantizeRgbCube(color, 8, 16, 8));
  }
  return TerminalColor::Rgb(color);
}

}  // namespace asciiomium::render
