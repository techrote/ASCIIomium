#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "render/color_quantizer.h"
#include "runtime/timing_series.h"
#include "terminal/differential_frame_emitter.h"
#include "terminal/full_frame_emitter.h"

namespace {
using Clock = std::chrono::steady_clock;
using namespace asciiomium;

struct Options {
  int samples = 240;
  int fps = 20;
  render::ColorMode colors = render::ColorMode::Rgb1024;
  std::string json = "-";
};

int ParseInt(std::string_view text, int maximum) {
  int value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value < 1 || value > maximum) throw std::invalid_argument("invalid integer argument");
  return value;
}

render::TerminalFrame MakeFrame(int sample, bool motion, render::ColorMode mode) {
  constexpr int columns = 160, rows = 50;
  const render::ModeQuantizer quantizer(mode);
  std::vector<render::TerminalCell> cells;
  cells.reserve(columns * rows);
  const int left = (sample * 3) % (columns - 12);
  const int top = 8 + (sample / 7) % 24;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      const bool moving_patch = motion && column >= left && column < left + 12 &&
                                row >= top && row < top + 5;
      const auto fg = moving_patch ? render::Rgb8{240, 180, 40}
                                  : render::Rgb8{static_cast<std::uint8_t>(40 + column / 8),
                                                 static_cast<std::uint8_t>(80 + row * 2), 130};
      const auto bg = moving_patch ? render::Rgb8{30, 210, 180}
                                  : render::Rgb8{15, 20, static_cast<std::uint8_t>(30 + column / 16)};
      cells.push_back({U'\u2580', quantizer.Quantize(fg), quantizer.Quantize(bg)});
    }
  }
  return render::TerminalFrame(columns, rows, std::move(cells));
}

double Milliseconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

void Timing(std::ostream& out, const runtime::TimingSeries& values) {
  out << "{\"mean_ms\":" << values.average_ms()
      << ",\"p50_ms\":" << values.percentile_ms(0.50)
      << ",\"p95_ms\":" << values.percentile_ms(0.95)
      << ",\"sample_count\":" << values.count() << '}';
}

void Run(std::ostream& out, const Options& options, bool motion) {
  render::TerminalFrame previous;
  bool have_previous = false;
  std::uint64_t full_bytes = 0, differential_bytes = 0, changed_cells = 0,
                changed_runs = 0, written_cells = 0, emitted_runs = 0, empty_frames = 0;
  std::uint64_t initial_full_bytes = 0, initial_differential_bytes = 0;
  runtime::TimingSeries full_timing, plan_timing, differential_timing;
  const auto started = Clock::now();
  for (int i = 0; i < options.samples; ++i) {
    auto current = MakeFrame(i, motion, options.colors);
    const auto full_start = Clock::now();
    const auto reference = terminal::SerializeFullFrame(current);
    const auto full_end = Clock::now();
    const auto plan = terminal::PlanDifferentialFrame(have_previous ? &previous : nullptr, current);
    const auto plan_end = Clock::now();
    const auto delta = terminal::SerializeDifferentialFrame(current, plan);
    const auto delta_end = Clock::now();
    if (delta.bytes.size() != plan.estimated_bytes) throw std::runtime_error("byte estimate mismatch");
    full_bytes += reference.bytes.size();
    differential_bytes += delta.bytes.size();
    changed_cells += plan.changed_cells;
    changed_runs += plan.changed_runs;
    written_cells += plan.written_cells;
    emitted_runs += plan.runs.size();
    if (delta.bytes.empty()) ++empty_frames;
    if (i == 0) {
      initial_full_bytes = reference.bytes.size();
      initial_differential_bytes = delta.bytes.size();
    }
    full_timing.AddMilliseconds(Milliseconds(full_start, full_end));
    plan_timing.AddMilliseconds(Milliseconds(full_end, plan_end));
    differential_timing.AddMilliseconds(Milliseconds(plan_end, delta_end));
    previous = std::move(current);
    have_previous = true;
  }
  const double seconds = static_cast<double>(options.samples) / options.fps;
  const double measured_seconds = std::chrono::duration<double>(Clock::now() - started).count();
  out << "{\"scenario\":\"" << (motion ? "synthetic-moving-12x5-patch" : "synthetic-static")
      << "\",\"frames\":" << options.samples
      << ",\"nominal_playback_seconds\":" << seconds
      << ",\"measured_compute_seconds\":" << measured_seconds
      << ",\"full_bytes\":" << full_bytes
      << ",\"differential_bytes\":" << differential_bytes
      << ",\"full_bytes_per_frame\":" << static_cast<double>(full_bytes) / options.samples
      << ",\"differential_bytes_per_frame\":" << static_cast<double>(differential_bytes) / options.samples
      << ",\"full_bytes_per_nominal_second\":" << full_bytes / seconds
      << ",\"differential_bytes_per_nominal_second\":" << differential_bytes / seconds
      << ",\"differential_to_full_ratio\":" << static_cast<double>(differential_bytes) / full_bytes
      << ",\"initial_full_bytes\":" << initial_full_bytes
      << ",\"initial_differential_bytes\":" << initial_differential_bytes
      << ",\"steady_full_bytes\":" << full_bytes - initial_full_bytes
      << ",\"steady_differential_bytes\":" << differential_bytes - initial_differential_bytes
      << ",\"changed_cells\":" << changed_cells << ",\"changed_runs\":" << changed_runs
      << ",\"written_cells\":" << written_cells << ",\"emitted_runs\":" << emitted_runs
      << ",\"empty_frames\":" << empty_frames << ",\"timing\":{\"full_serialization\":";
  Timing(out, full_timing);
  out << ",\"differential_planning\":";
  Timing(out, plan_timing);
  out << ",\"differential_serialization\":";
  Timing(out, differential_timing);
  out << "}}";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Options options;
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg(argv[i]);
      if (arg == "--help") {
        std::cout << "asciiomium_differential_benchmark [--samples 1..2000] [--fps 1..240] "
                     "[--colors true|16|256|512|1024] [--json PATH|-]\n"
                     "Fixed 160x50 paired synthetic frames; no CEF, terminal I/O or real-time pacing.\n";
        return 0;
      }
      if (i + 1 == argc) throw std::invalid_argument("missing argument value");
      const std::string_view value(argv[++i]);
      if (arg == "--samples") options.samples = ParseInt(value, 2000);
      else if (arg == "--fps") options.fps = ParseInt(value, 240);
      else if (arg == "--colors") {
        if (!render::TryParseColorMode(value, &options.colors)) throw std::invalid_argument("unknown colour mode");
      } else if (arg == "--json") options.json = value;
      else throw std::invalid_argument("unknown option");
    }
    std::ofstream file;
    std::ostream* out = &std::cout;
    if (options.json != "-") {
      file.open(options.json, std::ios::binary);
      if (!file) throw std::runtime_error("cannot open benchmark report");
      out = &file;
    }
    *out << std::fixed << std::setprecision(6)
         << "{\n\"schema\":\"asciiomium-differential-benchmark-v1\","
         << "\n\"source\":\"paired-synthetic-terminal-frames-v1\","
         << "\n\"terminal\":{\"columns\":160,\"rows\":50},"
         << "\n\"color_mode\":\"" << render::ColorModeName(options.colors) << "\","
         << "\n\"nominal_fps\":" << options.fps
         << ",\n\"timing_scope\":\"all samples, percentile index ceil(p*(N-1)); no terminal writes\","
         << "\n\"results\":[";
    Run(*out, options, false);
    *out << ",\n";
    Run(*out, options, true);
    *out << "]\n}\n";
    out->flush();
    if (!*out) throw std::runtime_error("failed to write benchmark report");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "differential benchmark: " << error.what() << '\n';
    return 1;
  }
}
