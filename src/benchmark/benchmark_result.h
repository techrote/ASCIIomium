#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace asciiomium::benchmark {

inline constexpr char kBenchmarkSchemaVersion[] = "asciiomium-benchmark-v1";

struct TimingSummary {
  double min_ms = 0.0;
  double mean_ms = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double max_ms = 0.0;
};

struct BenchmarkConfig {
  std::string fixture_bundle_version = "web-fixtures-v1";
  std::string source_id;
  std::string source_hash;
  int source_width = 0;
  int source_height = 0;
  int terminal_columns = 0;
  int terminal_rows = 0;
  std::string glyph_encoder = "halfblock-u2580";
  std::string color_mode;
  std::string sampling_filter;
  double frame_cap_fps = 0.0;
  std::size_t samples = 0;
};

struct PipelineSample {
  double render_conversion_ms = 0.0;
  std::optional<double> diff_ms;
  double vt_serialization_ms = 0.0;
  std::uint64_t bytes_emitted = 0;
  std::uint64_t source_frames_coalesced = 0;
  std::uint64_t source_frames_dropped = 0;
};

struct ProcessMetrics {
  double cpu_time_ms = 0.0;
  std::optional<double> cpu_utilization_percent;
  std::optional<std::uint64_t> working_set_bytes;
  std::optional<std::uint64_t> private_memory_bytes;
};

struct BenchmarkResult {
  BenchmarkConfig config;
  double wall_time_ms = 0.0;
  double source_frame_rate = 0.0;
  double rendered_terminal_frame_rate = 0.0;
  double emitted_terminal_frame_rate = 0.0;
  std::uint64_t source_frames_coalesced = 0;
  std::uint64_t source_frames_dropped = 0;
  TimingSummary render_conversion;
  std::optional<TimingSummary> diff;
  TimingSummary vt_serialization;
  double bytes_per_frame = 0.0;
  double bytes_per_second = 0.0;
  ProcessMetrics process;
  std::optional<double> interaction_latency_ms;
};

class BenchmarkAccumulator {
 public:
  void Add(PipelineSample sample);
  [[nodiscard]] BenchmarkResult Finalize(const BenchmarkConfig& config,
                                         double wall_time_ms,
                                         ProcessMetrics process) const;

 private:
  std::vector<PipelineSample> samples_;
};

[[nodiscard]] TimingSummary SummarizeTimings(std::vector<double> values);
[[nodiscard]] std::string SerializeJson(const BenchmarkResult& result);

}  // namespace asciiomium::benchmark
