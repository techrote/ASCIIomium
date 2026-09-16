#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "render/color_quantizer.h"
#include "render/halfblock_renderer.h"
#include "render/ppm_loader.h"
#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_session.h"

namespace {

struct Options {
  std::string input;
  bool ramp = false;
  asciiomium::render::ColorMode color_mode =
      asciiomium::render::ColorMode::Rgb1024;
  asciiomium::render::SamplingFilter filter =
      asciiomium::render::SamplingFilter::BoxAverage;
  asciiomium::render::FitMode fit = asciiomium::render::FitMode::Stretch;
  double cell_aspect = 0.5;
  int duration_ms = 0;
};

bool ParsePositiveInt(std::string_view text, int* value) {
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
      << "Usage:\n"
      << "  asciiomium_vt_preview --input <fixture.ppm> [options]\n"
      << "  asciiomium_vt_preview --ramp [options]\n\n"
      << "Options:\n"
      << "  --colors true|16|256|512|1024  colour mode (default 1024)\n"
      << "  --filter nearest|box            sampling filter (default box)\n"
      << "  --fit stretch|contain           viewport fit (default stretch)\n"
      << "  --cell-aspect X                 contain-mode cell ratio (default 0.5)\n"
      << "  --duration-ms N                 exit automatically after N ms\n\n"
      << "Without --duration-ms the preview runs until Ctrl+C. Resize the\n"
      << "Windows Terminal window to force a full-frame rerender.\n";
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
    if (arg == "--ramp") {
      options->ramp = true;
      continue;
    }
    if (arg == "--colors" && i + 1 < argc) {
      if (!asciiomium::render::TryParseColorMode(argv[++i],
                                                  &options->color_mode)) {
        return false;
      }
      continue;
    }
    if (arg == "--filter" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (value == "nearest") {
        options->filter = asciiomium::render::SamplingFilter::Nearest;
      } else if (value == "box") {
        options->filter = asciiomium::render::SamplingFilter::BoxAverage;
      } else {
        return false;
      }
      continue;
    }
    if (arg == "--fit" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (value == "stretch") {
        options->fit = asciiomium::render::FitMode::Stretch;
      } else if (value == "contain") {
        options->fit = asciiomium::render::FitMode::Contain;
      } else {
        return false;
      }
      continue;
    }
    if (arg == "--cell-aspect" && i + 1 < argc &&
        ParseDouble(argv[i + 1], &options->cell_aspect)) {
      ++i;
      continue;
    }
    if (arg == "--duration-ms" && i + 1 < argc &&
        ParsePositiveInt(argv[i + 1], &options->duration_ms)) {
      ++i;
      continue;
    }
    return false;
  }

  return options->ramp != !options->input.empty();
}

asciiomium::render::ImageBuffer MakeRamp() {
  constexpr int width = 256;
  constexpr int height = 96;
  std::vector<std::uint8_t> rgba;
  rgba.reserve(static_cast<std::size_t>(width) * height * 4u);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto v = static_cast<std::uint8_t>(x);
      std::uint8_t r = 0;
      std::uint8_t g = 0;
      std::uint8_t b = 0;
      if (y < 32) {
        r = g = b = v;
      } else if (y < 64) {
        r = v;
        g = static_cast<std::uint8_t>(255 - x);
        b = static_cast<std::uint8_t>((x * 3) & 0xFF);
      } else {
        const int segment = x / 43;
        const int local = (x % 43) * 6;
        switch (segment) {
          case 0: r = 255; g = static_cast<std::uint8_t>(local); b = 0; break;
          case 1: r = static_cast<std::uint8_t>(255 - local); g = 255; b = 0; break;
          case 2: r = 0; g = 255; b = static_cast<std::uint8_t>(local); break;
          case 3: r = 0; g = static_cast<std::uint8_t>(255 - local); b = 255; break;
          case 4: r = static_cast<std::uint8_t>(local); g = 0; b = 255; break;
          default: r = 255; g = 0; b = static_cast<std::uint8_t>(255 - local); break;
        }
      }
      rgba.push_back(r);
      rgba.push_back(g);
      rgba.push_back(b);
      rgba.push_back(255);
    }
  }

  return asciiomium::render::ImageBuffer(width, height, std::move(rgba));
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }

  try {
    asciiomium::render::ImageBuffer image =
        options.ramp ? MakeRamp() : asciiomium::render::LoadPpmP3(options.input);
    asciiomium::render::ModeQuantizer quantizer(options.color_mode);

    asciiomium::render::RenderConfig render_config;
    render_config.filter = options.filter;
    render_config.fit_mode = options.fit;
    render_config.cell_aspect = options.cell_aspect;
    render_config.quantizer = &quantizer;

    asciiomium::terminal::TerminalSessionOptions terminal_options;
    terminal_options.use_alternate_screen = true;
    terminal_options.hide_cursor = true;
    terminal_options.enable_vt_input = false;
    asciiomium::terminal::TerminalSession session(terminal_options);

    const auto started = std::chrono::steady_clock::now();
    std::uint64_t rendered_generation = 0;
    std::size_t last_bytes = 0;

    while (!session.stop_requested()) {
      (void)session.RefreshGeometry();
      const auto geometry = session.geometry();
      if (geometry.generation != rendered_generation) {
        const auto frame = asciiomium::render::RenderHalfBlock(
            image.view(), {geometry.columns, geometry.rows}, render_config);
        const auto emission =
            asciiomium::terminal::SerializeFullFrame(frame);
        session.Write(emission.bytes);
        rendered_generation = geometry.generation;
        last_bytes = emission.bytes.size();
      }

      if (options.duration_ms > 0 &&
          std::chrono::steady_clock::now() - started >=
              std::chrono::milliseconds(options.duration_ms)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // This is intentionally written after TerminalSession restores the normal
    // screen by leaving scope through the destructor below; keep diagnostics
    // out of the rendered framebuffer itself.
    (void)last_bytes;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "VT preview failed: " << error.what() << '\n';
    return 1;
  }
}
