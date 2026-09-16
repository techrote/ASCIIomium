#include "benchmark/benchmark_result.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace asciiomium::benchmark {
namespace {

double Percentile(const std::vector<double>& sorted, double p) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double position = p * static_cast<double>(sorted.size() - 1);
  const auto low = static_cast<std::size_t>(std::floor(position));
  const auto high = static_cast<std::size_t>(std::ceil(position));
  if (low == high) {
    return sorted[low];
  }
  const double fraction = position - static_cast<double>(low);
  return sorted[low] + (sorted[high] - sorted[low]) * fraction;
}

std::string EscapeJson(const std::string& value) {
  std::ostringstream out;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(ch) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str();
}

void WriteTiming(std::ostringstream& out, const TimingSummary& timing) {
  out << "{\"min_ms\":" << timing.min_ms
      << ",\"mean_ms\":" << timing.mean_ms
      << ",\"p50_ms\":" << timing.p50_ms
      << ",\"p95_ms\":" << timing.p95_ms
      << ",\"max_ms\":" << timing.max_ms << '}';
}

void WriteOptionalDouble(std::ostringstream& out,
                         const std::optional<double>& value) {
  if (value.has_value()) {
    out << *value;
  } else {
    out << "null";
  }
}

void WriteOptionalInteger(std::ostringstream& out,
                          const std::optional<std::uint64_t>& value) {
  if (value.has_value()) {
    out << *value;
  } else {
    out << "null";
  }
}

}  // namespace

TimingSummary SummarizeTimings(std::vector<double> values) {
  if (values.empty()) {
    throw std::invalid_argument("cannot summarize an empty timing set");
  }
  std::sort(values.begin(), values.end());
  const double total = std::accumulate(values.begin(), values.end(), 0.0);
  return TimingSummary{values.front(),
                       total / static_cast<double>(values.size()),
                       Percentile(values, 0.50),
                       Percentile(values, 0.95),
                       values.back()};
}

void BenchmarkAccumulator::Add(PipelineSample sample) {
  samples_.push_back(std::move(sample));
}

BenchmarkResult BenchmarkAccumulator::Finalize(const BenchmarkConfig& config,
                                                double wall_time_ms,
                                                ProcessMetrics process) const {
  if (samples_.empty()) {
    throw std::invalid_argument("benchmark requires at least one sample");
  }
  if (!(wall_time_ms > 0.0) || !std::isfinite(wall_time_ms)) {
    throw std::invalid_argument("benchmark wall time must be finite and positive");
  }

  std::vector<double> render;
  std::vector<double> diff;
  std::vector<double> vt;
  render.reserve(samples_.size());
  diff.reserve(samples_.size());
  vt.reserve(samples_.size());

  std::uint64_t total_bytes = 0;
  std::uint64_t coalesced = 0;
  std::uint64_t dropped = 0;
  for (const auto& sample : samples_) {
    render.push_back(sample.render_conversion_ms);
    if (sample.diff_ms.has_value()) {
      diff.push_back(*sample.diff_ms);
    }
    vt.push_back(sample.vt_serialization_ms);
    total_bytes += sample.bytes_emitted;
    coalesced += sample.source_frames_coalesced;
    dropped += sample.source_frames_dropped;
  }

  BenchmarkResult result;
  result.config = config;
  result.config.samples = samples_.size();
  result.wall_time_ms = wall_time_ms;
  const double seconds = wall_time_ms / 1000.0;
  const double fps = static_cast<double>(samples_.size()) / seconds;
  result.source_frame_rate = fps;
  result.rendered_terminal_frame_rate = fps;
  result.emitted_terminal_frame_rate = fps;
  result.source_frames_coalesced = coalesced;
  result.source_frames_dropped = dropped;
  result.render_conversion = SummarizeTimings(std::move(render));
  if (!diff.empty()) {
    result.diff = SummarizeTimings(std::move(diff));
  }
  result.vt_serialization = SummarizeTimings(std::move(vt));
  result.bytes_per_frame = static_cast<double>(total_bytes) /
                           static_cast<double>(samples_.size());
  result.bytes_per_second = static_cast<double>(total_bytes) / seconds;
  result.process = process;
  return result;
}

std::string SerializeJson(const BenchmarkResult& result) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6);
  out << "{\n";
  out << "  \"schema\":\"" << kBenchmarkSchemaVersion << "\",\n";
  out << "  \"fixture_bundle\":{\"version\":\""
      << EscapeJson(result.config.fixture_bundle_version)
      << "\",\"manifest\":\"fixtures/web/MANIFEST.sha256\"},\n";
  out << "  \"source\":{\"id\":\"" << EscapeJson(result.config.source_id)
      << "\",\"hash\":\"" << EscapeJson(result.config.source_hash)
      << "\",\"viewport\":{\"width\":" << result.config.source_width
      << ",\"height\":" << result.config.source_height << "}},\n";
  out << "  \"terminal\":{\"columns\":" << result.config.terminal_columns
      << ",\"rows\":" << result.config.terminal_rows << "},\n";
  out << "  \"renderer\":{\"glyph_encoder\":\""
      << EscapeJson(result.config.glyph_encoder) << "\",\"color_mode\":\""
      << EscapeJson(result.config.color_mode) << "\",\"sampling_filter\":\""
      << EscapeJson(result.config.sampling_filter) << "\"},\n";
  out << "  \"run\":{\"samples\":" << result.config.samples
      << ",\"frame_cap_fps\":" << result.config.frame_cap_fps
      << ",\"wall_time_ms\":" << result.wall_time_ms << "},\n";
  out << "  \"rates\":{\"source_fps\":" << result.source_frame_rate
      << ",\"rendered_terminal_fps\":" << result.rendered_terminal_frame_rate
      << ",\"emitted_terminal_fps\":" << result.emitted_terminal_frame_rate
      << ",\"source_frames_coalesced\":" << result.source_frames_coalesced
      << ",\"source_frames_dropped\":" << result.source_frames_dropped << "},\n";
  out << "  \"timing\":{\"render_conversion\":";
  WriteTiming(out, result.render_conversion);
  out << ",\"diff\":";
  if (result.diff.has_value()) {
    WriteTiming(out, *result.diff);
  } else {
    out << "null";
  }
  out << ",\"vt_serialization\":";
  WriteTiming(out, result.vt_serialization);
  out << "},\n";
  out << "  \"bandwidth\":{\"bytes_per_frame\":" << result.bytes_per_frame
      << ",\"bytes_per_second\":" << result.bytes_per_second << "},\n";
  out << "  \"process\":{\"cpu_time_ms\":" << result.process.cpu_time_ms
      << ",\"cpu_utilization_percent\":";
  WriteOptionalDouble(out, result.process.cpu_utilization_percent);
  out << ",\"working_set_bytes\":";
  WriteOptionalInteger(out, result.process.working_set_bytes);
  out << ",\"private_memory_bytes\":";
  WriteOptionalInteger(out, result.process.private_memory_bytes);
  out << "},\n";
  out << "  \"interaction_latency_ms\":";
  WriteOptionalDouble(out, result.interaction_latency_ms);
  out << "\n}\n";
  return out.str();
}

}  // namespace asciiomium::benchmark
