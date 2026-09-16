#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "benchmark/benchmark_result.h"
#include "render/color_quantizer.h"
#include "render/halfblock_renderer.h"
#include "render/render_types.h"
#include "terminal/full_frame_emitter.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  int source_width = 1280;
  int source_height = 720;
  int columns = 160;
  int rows = 50;
  int samples = 120;
  double frame_cap_fps = 0.0;
  asciiomium::render::SamplingFilter filter =
      asciiomium::render::SamplingFilter::BoxAverage;
  asciiomium::render::ColorMode color_mode =
      asciiomium::render::ColorMode::Rgb1024;
  std::string json_path = "-";
};

bool ParsePositiveInt(std::string_view text, int* value) {
  int parsed = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParseNonNegativeDouble(std::string_view text, double* value) {
  try {
    std::size_t consumed = 0;
    const std::string copy(text);
    const double parsed = std::stod(copy, &consumed);
    if (consumed != copy.size() || parsed < 0.0) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

void Usage() {
  std::cout
      << "ASCIIomium offline benchmark\n"
      << "Usage: asciiomium_benchmark [options]\n"
      << "  --samples N           conversion iterations (default 120)\n"
      << "  --source-width N      synthetic RGBA width (default 1280)\n"
      << "  --source-height N     synthetic RGBA height (default 720)\n"
      << "  --columns N           terminal columns (default 160)\n"
      << "  --rows N              terminal rows (default 50)\n"
      << "  --filter box|nearest  sampling filter (default box)\n"
      << "  --colors true|16|256|512|1024 (default 1024)\n"
      << "  --frame-cap FPS       optional pacing cap, 0 = uncapped\n"
      << "  --json PATH|-         JSON output file or stdout (default -)\n";
}

bool Parse(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      Usage();
      std::exit(0);
    }
    if (arg == "--samples" && i + 1 < argc && ParsePositiveInt(argv[i + 1], &options->samples)) { ++i; continue; }
    if (arg == "--source-width" && i + 1 < argc && ParsePositiveInt(argv[i + 1], &options->source_width)) { ++i; continue; }
    if (arg == "--source-height" && i + 1 < argc && ParsePositiveInt(argv[i + 1], &options->source_height)) { ++i; continue; }
    if (arg == "--columns" && i + 1 < argc && ParsePositiveInt(argv[i + 1], &options->columns)) { ++i; continue; }
    if (arg == "--rows" && i + 1 < argc && ParsePositiveInt(argv[i + 1], &options->rows)) { ++i; continue; }
    if (arg == "--frame-cap" && i + 1 < argc && ParseNonNegativeDouble(argv[i + 1], &options->frame_cap_fps)) { ++i; continue; }
    if (arg == "--json" && i + 1 < argc) { options->json_path = argv[++i]; continue; }
    if (arg == "--filter" && i + 1 < argc) {
      const std::string_view value(argv[++i]);
      if (value == "box") options->filter = asciiomium::render::SamplingFilter::BoxAverage;
      else if (value == "nearest") options->filter = asciiomium::render::SamplingFilter::Nearest;
      else return false;
      continue;
    }
    if (arg == "--colors" && i + 1 < argc) {
      if (!asciiomium::render::TryParseColorMode(argv[++i], &options->color_mode)) return false;
      continue;
    }
    return false;
  }
  return true;
}

asciiomium::render::ImageBuffer MakeSynthetic(int width, int height) {
  std::vector<std::uint8_t> bytes;
  bytes.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) * 4u;
      const auto scale = [](int value, int extent) -> std::uint8_t {
        if (extent <= 1) return 0;
        return static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) * 255u) /
                                         static_cast<std::uint64_t>(extent - 1));
      };
      const std::uint8_t checker = ((x / 7 + y / 5) & 1) ? 37u : 0u;
      bytes[offset + 0] = scale(x, width);
      bytes[offset + 1] = scale(y, height);
      bytes[offset + 2] = static_cast<std::uint8_t>((x * 13 + y * 7 + checker) & 0xff);
      bytes[offset + 3] = 255u;
    }
  }
  return asciiomium::render::ImageBuffer(width, height, std::move(bytes));
}

std::string Fnv1a64Hex(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  std::ostringstream out;
  out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

std::uint64_t FileTimeTicks(const FILETIME& value) {
  ULARGE_INTEGER integer{};
  integer.LowPart = value.dwLowDateTime;
  integer.HighPart = value.dwHighDateTime;
  return integer.QuadPart;
}

std::uint64_t ProcessCpuTicks() {
  FILETIME creation{}, exit{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0;
  return FileTimeTicks(kernel) + FileTimeTicks(user);
}

asciiomium::benchmark::ProcessMetrics ReadProcessMetrics(std::uint64_t cpu_start,
                                                         double wall_ms) {
  asciiomium::benchmark::ProcessMetrics metrics;
  const std::uint64_t cpu_end = ProcessCpuTicks();
  if (cpu_end >= cpu_start) {
    metrics.cpu_time_ms = static_cast<double>(cpu_end - cpu_start) / 10000.0;
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    if (wall_ms > 0.0) {
      metrics.cpu_utilization_percent =
          100.0 * metrics.cpu_time_ms / wall_ms / static_cast<double>(threads);
    }
  }
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                           sizeof(memory))) {
    metrics.working_set_bytes = static_cast<std::uint64_t>(memory.WorkingSetSize);
    metrics.private_memory_bytes = static_cast<std::uint64_t>(memory.PrivateUsage);
  }
  return metrics;
}

double Milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!Parse(argc, argv, &options)) {
    Usage();
    return 2;
  }

  try {
    auto image = MakeSynthetic(options.source_width, options.source_height);
    asciiomium::render::ModeQuantizer quantizer(options.color_mode);
    asciiomium::render::RenderConfig render_config;
    render_config.filter = options.filter;
    render_config.quantizer = &quantizer;

    asciiomium::benchmark::BenchmarkConfig config;
    config.source_id = "synthetic-rgba-v1";
    config.source_hash = Fnv1a64Hex(image.bytes());
    config.source_width = options.source_width;
    config.source_height = options.source_height;
    config.terminal_columns = options.columns;
    config.terminal_rows = options.rows;
    config.color_mode = std::string(asciiomium::render::ColorModeName(options.color_mode));
    config.sampling_filter = options.filter == asciiomium::render::SamplingFilter::BoxAverage ? "box" : "nearest";
    config.frame_cap_fps = options.frame_cap_fps;
    config.samples = static_cast<std::size_t>(options.samples);

    asciiomium::benchmark::BenchmarkAccumulator accumulator;
    const auto run_start = Clock::now();
    const std::uint64_t cpu_start = ProcessCpuTicks();
    auto next_frame = run_start;
    const auto frame_period = options.frame_cap_fps > 0.0
        ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / options.frame_cap_fps))
        : Clock::duration::zero();

    for (int sample = 0; sample < options.samples; ++sample) {
      if (frame_period != Clock::duration::zero()) {
        next_frame += frame_period;
      }

      const auto render_start = Clock::now();
      const auto frame = asciiomium::render::RenderHalfBlock(
          image.view(), {options.columns, options.rows}, render_config);
      const auto render_end = Clock::now();

      const auto vt_start = Clock::now();
      const auto emission = asciiomium::terminal::SerializeFullFrame(frame);
      const auto vt_end = Clock::now();

      asciiomium::benchmark::PipelineSample pipeline;
      pipeline.render_conversion_ms = Milliseconds(render_end - render_start);
      pipeline.vt_serialization_ms = Milliseconds(vt_end - vt_start);
      pipeline.bytes_emitted = static_cast<std::uint64_t>(emission.bytes.size());
      accumulator.Add(pipeline);

      if (frame_period != Clock::duration::zero()) {
        std::this_thread::sleep_until(next_frame);
      }
    }

    const auto run_end = Clock::now();
    const double wall_ms = Milliseconds(run_end - run_start);
    const auto process = ReadProcessMetrics(cpu_start, wall_ms);
    const auto result = accumulator.Finalize(config, wall_ms, process);
    const std::string json = asciiomium::benchmark::SerializeJson(result);

    if (options.json_path == "-") {
      std::cout << json;
    } else {
      std::ofstream output(options.json_path, std::ios::binary);
      if (!output) {
        std::cerr << "unable to open benchmark JSON output: " << options.json_path << '\n';
        return 3;
      }
      output << json;
      std::cout << "wrote benchmark JSON: " << options.json_path << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
