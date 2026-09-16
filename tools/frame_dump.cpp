#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "render/halfblock_renderer.h"
#include "render/ppm_loader.h"

namespace {

struct Options {
  std::string input;
  int columns = 8;
  int rows = 4;
  asciiomium::render::SamplingFilter filter =
      asciiomium::render::SamplingFilter::BoxAverage;
  asciiomium::render::FitMode fit = asciiomium::render::FitMode::Stretch;
  double cell_aspect = 0.5;
};

bool ParseInt(std::string_view text, int* value) {
  int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParseDouble(std::string_view text, double* value) {
  std::string copy(text);
  char* end = nullptr;
  const double parsed = std::strtod(copy.c_str(), &end);
  if (end == copy.c_str() || *end != '\0' || !(parsed > 0.0)) {
    return false;
  }
  *value = parsed;
  return true;
}

void PrintUsage() {
  std::cout
      << "Usage: asciiomium_frame_dump --input <fixture.ppm> [options]\n"
      << "  --columns N             terminal columns (default 8)\n"
      << "  --rows N                terminal rows (default 4)\n"
      << "  --filter nearest|box    sampling filter (default box)\n"
      << "  --fit stretch|contain   viewport mapping (default stretch)\n"
      << "  --cell-aspect X         cell width/height for contain mode (default 0.5)\n";
}

bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    }
    if (arg == "--input" && i + 1 < argc) {
      options->input = argv[++i];
      continue;
    }
    if (arg == "--columns" && i + 1 < argc &&
        ParseInt(argv[i + 1], &options->columns)) {
      ++i;
      continue;
    }
    if (arg == "--rows" && i + 1 < argc &&
        ParseInt(argv[i + 1], &options->rows)) {
      ++i;
      continue;
    }
    if (arg == "--filter" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (value == "nearest") {
        options->filter = asciiomium::render::SamplingFilter::Nearest;
        continue;
      }
      if (value == "box") {
        options->filter = asciiomium::render::SamplingFilter::BoxAverage;
        continue;
      }
      return false;
    }
    if (arg == "--fit" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (value == "stretch") {
        options->fit = asciiomium::render::FitMode::Stretch;
        continue;
      }
      if (value == "contain") {
        options->fit = asciiomium::render::FitMode::Contain;
        continue;
      }
      return false;
    }
    if (arg == "--cell-aspect" && i + 1 < argc &&
        ParseDouble(argv[i + 1], &options->cell_aspect)) {
      ++i;
      continue;
    }
    return false;
  }
  return !options->input.empty();
}

void PrintColor(const asciiomium::render::Rgb8& color) {
  std::cout << static_cast<int>(color.r) << ','
            << static_cast<int>(color.g) << ','
            << static_cast<int>(color.b);
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }

  try {
    const auto image = asciiomium::render::LoadPpmP3(options.input);
    asciiomium::render::RenderConfig config;
    config.filter = options.filter;
    config.fit_mode = options.fit;
    config.cell_aspect = options.cell_aspect;

    const auto frame = asciiomium::render::RenderHalfBlock(
        image.view(), {options.columns, options.rows}, config);

    std::cout << "frame " << frame.columns() << 'x' << frame.rows() << '\n';
    for (int row = 0; row < frame.rows(); ++row) {
      for (int column = 0; column < frame.columns(); ++column) {
        const auto& cell = frame.at(column, row);
        std::cout << "cell " << column << ',' << row << " glyph=U+"
                  << std::uppercase << std::hex
                  << static_cast<std::uint32_t>(cell.glyph) << std::dec
                  << " fg=";
        PrintColor(cell.foreground);
        std::cout << " bg=";
        PrintColor(cell.background);
        std::cout << '\n';
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "frame dump failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
