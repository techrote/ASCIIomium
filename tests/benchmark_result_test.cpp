#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark/benchmark_result.h"

namespace {

int failures = 0;
void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool Near(double a, double b, double epsilon = 1e-9) {
  return std::abs(a - b) <= epsilon;
}

}  // namespace

int main() {
  using namespace asciiomium::benchmark;

  const auto timing = SummarizeTimings({5.0, 1.0, 4.0, 2.0, 3.0});
  Require(Near(timing.min_ms, 1.0), "timing min");
  Require(Near(timing.mean_ms, 3.0), "timing mean");
  Require(Near(timing.p50_ms, 3.0), "timing p50");
  Require(Near(timing.p95_ms, 4.8), "timing p95 interpolation");
  Require(Near(timing.max_ms, 5.0), "timing max");

  BenchmarkAccumulator accumulator;
  accumulator.Add(PipelineSample{1.0, std::nullopt, 0.5, 100, 0, 0});
  accumulator.Add(PipelineSample{3.0, std::nullopt, 1.5, 300, 2, 1});

  BenchmarkConfig config;
  config.source_id = "unit-source";
  config.source_hash = "fnv1a64:0123456789abcdef";
  config.source_width = 10;
  config.source_height = 20;
  config.terminal_columns = 80;
  config.terminal_rows = 24;
  config.color_mode = "1024";
  config.sampling_filter = "box";
  config.frame_cap_fps = 20.0;
  config.samples = 999;  // Finalize must replace this with observed count.

  ProcessMetrics process;
  process.cpu_time_ms = 12.5;
  process.cpu_utilization_percent = 4.25;
  process.working_set_bytes = 123456;
  process.private_memory_bytes = 654321;

  const auto result = accumulator.Finalize(config, 100.0, process);
  Require(result.config.samples == 2, "observed sample count authoritative");
  Require(Near(result.source_frame_rate, 20.0), "source fps");
  Require(Near(result.rendered_terminal_frame_rate, 20.0), "render fps");
  Require(Near(result.emitted_terminal_frame_rate, 20.0), "emission fps");
  Require(result.source_frames_coalesced == 2, "coalesced count");
  Require(result.source_frames_dropped == 1, "dropped count");
  Require(Near(result.bytes_per_frame, 200.0), "bytes per frame");
  Require(Near(result.bytes_per_second, 4000.0), "bytes per second");
  Require(!result.diff.has_value(), "offline diff unavailable is explicit");
  Require(!result.interaction_latency_ms.has_value(),
          "offline interaction latency unavailable is explicit");

  const std::string json = SerializeJson(result);
  Require(json.find("\"schema\":\"asciiomium-benchmark-v1\"") != std::string::npos,
          "schema present");
  Require(json.find("\"version\":\"web-fixtures-v1\"") != std::string::npos,
          "fixture bundle version present");
  Require(json.find("\"glyph_encoder\":\"halfblock-u2580\"") != std::string::npos,
          "glyph encoder present");
  Require(json.find("\"color_mode\":\"1024\"") != std::string::npos,
          "colour mode present");
  Require(json.find("\"sampling_filter\":\"box\"") != std::string::npos,
          "filter present");
  Require(json.find("\"diff\":null") != std::string::npos,
          "unavailable diff serialized null");
  Require(json.find("\"interaction_latency_ms\":null") != std::string::npos,
          "unavailable interaction metric serialized null");

  bool empty_threw = false;
  try {
    BenchmarkAccumulator empty;
    (void)empty.Finalize(config, 1.0, process);
  } catch (const std::invalid_argument&) {
    empty_threw = true;
  }
  Require(empty_threw, "empty benchmark rejected");

  if (failures != 0) {
    std::cerr << failures << " benchmark assertion(s) failed\n";
    return 1;
  }
  std::cout << "benchmark result tests passed\n";
  return 0;
}
