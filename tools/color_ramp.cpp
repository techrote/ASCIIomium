#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "render/color_quantizer.h"

namespace {

using asciiomium::render::ColorMode;
using asciiomium::render::ModeQuantizer;
using asciiomium::render::Rgb8;

constexpr int kRampWidth = 640;
constexpr int kLabelWidth = 92;
constexpr int kRowHeight = 40;
constexpr int kSwatchHeight = 28;
constexpr int kTop = 24;

Rgb8 SourceColor(int x) {
  const int section = x / 160;
  const int local = x % 160;
  const auto ramp = static_cast<std::uint8_t>((local * 255 + 79) / 159);
  switch (section) {
    case 0:
      return {ramp, ramp, ramp};
    case 1:
      return {static_cast<std::uint8_t>(255 - ramp), ramp, 32};
    case 2:
      return {16, ramp, 255};
    default:
      return {ramp, static_cast<std::uint8_t>(255 - ramp),
              static_cast<std::uint8_t>((static_cast<unsigned>(ramp) * 3u) / 4u)};
  }
}

std::string HexColor(Rgb8 color) {
  std::ostringstream out;
  out << '#' << std::hex << std::setfill('0') << std::setw(2)
      << static_cast<unsigned>(color.r) << std::setw(2)
      << static_cast<unsigned>(color.g) << std::setw(2)
      << static_cast<unsigned>(color.b);
  return out.str();
}

void WriteSvg(const std::filesystem::path& output) {
  const std::array<ColorMode, 5> modes = {
      ColorMode::Indexed16, ColorMode::Indexed256, ColorMode::Rgb512,
      ColorMode::Rgb1024, ColorMode::TrueColor};
  const int width = kLabelWidth + kRampWidth + 16;
  const int height = kTop + static_cast<int>(modes.size()) * kRowHeight + 20;

  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("unable to create SVG: " + output.string());
  }

  stream << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
         << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' '
         << height << "\">\n"
         << "<rect width=\"100%\" height=\"100%\" fill=\"#111\"/>\n"
         << "<style>text{font-family:monospace;font-size:14px;fill:#eee}</style>\n"
         << "<text x=\"8\" y=\"16\">ASCIIomium deterministic colour-mode reference</text>\n";

  for (std::size_t row = 0; row < modes.size(); ++row) {
    const ColorMode mode = modes[row];
    const ModeQuantizer quantizer(mode);
    const int y = kTop + static_cast<int>(row) * kRowHeight;
    stream << "<text x=\"8\" y=\"" << (y + 19) << "\">"
           << asciiomium::render::ColorModeName(mode) << "</text>\n";
    for (int x = 0; x < kRampWidth; ++x) {
      const auto output_color = quantizer.Quantize(SourceColor(x));
      stream << "<rect x=\"" << (kLabelWidth + x) << "\" y=\"" << y
             << "\" width=\"1\" height=\"" << kSwatchHeight
             << "\" fill=\"" << HexColor(output_color.rgb) << "\"/>\n";
    }
  }

  stream << "<text x=\"" << kLabelWidth << "\" y=\"" << (height - 5)
         << "\">gray | red-green | green-blue | mixed ramp</text>\n</svg>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || std::string_view(argv[1]) != "--output") {
    std::cerr << "Usage: asciiomium_color_ramp --output <file.svg>\n";
    return 2;
  }

  try {
    WriteSvg(argv[2]);
  } catch (const std::exception& error) {
    std::cerr << "colour ramp generation failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "wrote colour comparison: " << argv[2] << '\n';
  return 0;
}
