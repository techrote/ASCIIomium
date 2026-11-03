#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "runtime/telemetry.h"

namespace {
using namespace asciiomium::runtime;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void Contains(const std::string& json, std::string_view expected, const char* message) {
  Require(json.find(expected) != std::string::npos, message);
}

void TestDuration() {
  Require(ParseBenchmarkDuration("30s") == 30000, "seconds duration");
  Require(ParseBenchmarkDuration("500ms") == 500, "milliseconds duration");
  Require(ParseBenchmarkDuration("2m") == 120000, "minutes duration");
  Require(ParseBenchmarkDuration("100ms") == 100, "minimum duration");
  Require(ParseBenchmarkDuration("60m") == 3600000, "maximum duration");
  for (auto value : {"", "30", "0s", "99ms", "61m", "3601s", "-1s", "+1s", "1.5s", "1e3s",
                     "1 s", "1s ", "1msx", "100MS", "18446744073709551615m", "184467440737095516160s"}) {
    Require(!ParseBenchmarkDuration(value), "invalid duration accepted");
  }
}

void TestTimingAvailabilityAndWindow() {
  Require(SerializeTiming(nullptr) == "null", "unavailable timing must be null");
  TimingSeries series;
  auto json = SerializeTiming(&series);
  Contains(json, "\"sample_count\":0", "empty sample count");
  Contains(json, "\"mean_ms\":null", "empty mean must not pretend to be zero");
  Contains(json, "\"p95_ms\":null", "empty percentile must be null");
  series.AddMilliseconds(0);
  Contains(SerializeTiming(&series), "\"mean_ms\":0.000000", "measured zero must remain zero");
  series.AddMilliseconds(-1);
  series.AddMilliseconds(std::numeric_limits<double>::infinity());
  Require(series.count() == 1, "invalid timing samples count as measurements");
  series.AddMilliseconds(1000);
  for (std::size_t n = 0; n < TimingSeries::kCapacity; ++n) series.AddMilliseconds(2);
  Require(series.count() == TimingSeries::kCapacity + 2, "whole-run count lost");
  Require(series.retained_count() == TimingSeries::kCapacity, "percentile storage unbounded");
  Require(series.percentile_ms(0.5) == 2 && series.percentile_ms(0.95) == 2, "rolling window contains expired samples");
  Require(series.average_ms() > 2, "whole-run mean silently became rolling mean");
  json = SerializeTiming(&series);
  Contains(json, "\"percentile_sample_count\":2048", "rolling count absent");
  Contains(json, "ceil(p*(N-1))", "percentile definition absent");
}

void TestProcessSemantics() {
  ProcessSnapshot begin, end;
  begin.cpu_time_100ns = 10000000;
  end.cpu_time_100ns = 15000000;
  begin.logical_processors = 8;
  end.logical_processors = 8;
  const auto process = MeasureProcess(begin, end, 2.0);
  Require(process.cpu_time_ms == 500.0, "100 ns CPU tick conversion wrong");
  Require(process.cpu_percent_one_core == 25.0, "one-core CPU percent wrong");
  Require(process.cpu_percent_machine == 3.125, "normalized CPU percent wrong");
  Require(!MeasureProcess({}, {}, 2).cpu_time_ms, "missing CPU counters must be null");
  Require(!MeasureProcess(end, begin, 2).cpu_time_ms, "regressing CPU counter accepted");
  Require(!MeasureProcess(begin, end, 0).cpu_percent_one_core, "zero-wall CPU percent defined");
  Require(!MeasureProcess(begin, end, std::numeric_limits<double>::quiet_NaN()).cpu_percent_one_core,
          "NaN-wall CPU percent defined");
  Require(!MeasureProcess(begin, end, 1e-320).cpu_percent_one_core, "overflowing CPU percent emitted");
  begin.cpu_sample_time_seconds = 10.0;
  end.cpu_sample_time_seconds = 11.0;
  begin.cpu_sample_span_seconds = 0.000002;
  end.cpu_sample_span_seconds = 0.000004;
  const auto sampled = MeasureProcess(begin, end, 1000.0);
  Require(sampled.cpu_interval_seconds == 1.0 && sampled.cpu_percent_one_core == 50.0,
          "CPU denominator must use counter sample interval, not a wider render-loop interval");
  Require(sampled.cpu_interval_uses_sample_clock && sampled.cpu_sampling_uncertainty_seconds,
          "CPU sample midpoint approximation must be explicit");
  end.cpu_sample_time_seconds = 9.0;
  Require(!MeasureProcess(begin, end, 2.0).cpu_percent_one_core,
          "invalid sample clock silently fell back to unrelated wall interval");
  end.cpu_sample_time_seconds = 11.0;
  end.logical_processors = 4;
  Require(!MeasureProcess(begin, end, 2).cpu_percent_machine, "topology-change CPU denominator accepted");
#ifndef _WIN32
  const auto unavailable = ReadProcessSnapshot();
  Require(!unavailable.cpu_time_100ns && !unavailable.working_set_bytes,
          "unsupported platform must not fabricate Windows process counters");
#endif
}

std::string TestReportSemantics() {
  TimingSeries conversion, serialization;
  conversion.AddMilliseconds(1);
  conversion.AddMilliseconds(3);
  serialization.AddMilliseconds(0);
  TelemetryReport report;
  report.identity.asciiomium_version = "unit-fixture";
  report.identity.compiler = "a\"b\\c\n";
  report.config.fixture_id = "flat-ui";
  report.config.emitter = "differential";
  report.config.benchmark_mode = true;
  report.config.requested_duration_ms = 2500;
  report.config.comparison_enabled = true;
  report.wall_seconds = 2.5;
  report.completed_normally = true;
  report.exit_reason = "duration";
  report.counters.rendered_frames = 10;
  report.counters.emitted_frames = 7;
  report.counters.empty_payload_frames = 3;
  report.counters.bytes_serialized = 10000;
  report.counters.source_view_paints_start = 2;
  report.counters.source_view_paints_end = 20;
  report.counters.source_generation_end = 22;
  report.counters.last_rendered_generation = 20;
  report.counters.changed_cells = 0;
  report.counters.same_frame_full_bytes = 40000;
  report.counters.same_frame_differential_bytes = 10000;
  report.timings.conversion = &conversion;
  report.timings.serialization = &serialization;
  report.timings.terminal_write = &serialization;  // Must still be null when headless.
  const auto json = SerializeTelemetry(report);
  Contains(json, "\"source_revision\":null", "missing source identity fabricated");
  Contains(json, "\"compiler\":\"a\\\"b\\\\c\\u000a\"", "JSON string escaping invalid");
  Contains(json, "\"changed_cells\":0", "available zero counter must remain zero");
  Contains(json, "\"changed_runs\":null", "unavailable counter not null");
  Contains(json, "\"view_paints_per_second\":7.200000", "source rate includes paints outside measurement");
  Contains(json, "\"rendered_fps\":4.000000", "render rate wrong");
  Contains(json, "\"bytes_per_second\":4000.000000", "bandwidth denominator wrong");
  Contains(json, "\"bytes_written_to_terminal\":null", "headless run fabricated terminal writes");
  Contains(json, "\"terminal_write\":null", "headless run fabricated output latency");
  Contains(json, "\"same_frame_differential_to_full_ratio\":0.250000", "paired ratio wrong");
  Contains(json, "\"frame_accounting_consistent\":true", "empty/nonempty render accounting wrong");
  Contains(json, "\"process\":null", "unavailable process not null");

  report.config.terminal_output = true;
  Contains(SerializeTelemetry(report), "\"bytes_written_to_terminal\":10000", "successful terminal writes not counted");
  report.completed_normally = false;
  Contains(SerializeTelemetry(report), "\"bytes_written_to_terminal\":null", "incomplete run claims exact terminal bytes");
  report.config.terminal_output = false;
  report.config.timings_enabled = false;
  Contains(SerializeTelemetry(report), "\"conversion\":null", "disabled stage timings exposed");
  report.wall_seconds = 0;
  report.counters.rendered_frames = 0;
  report.counters.same_frame_full_bytes = 0;
  const auto zero = SerializeTelemetry(report);
  Contains(zero, "\"rendered_fps\":null", "zero duration invents zero measured FPS");
  Contains(zero, "\"bytes_per_rendered_frame\":null", "zero frames rate defined");
  Contains(zero, "\"same_frame_differential_to_full_ratio\":null", "zero reference denominator defined");
  Contains(zero, "\"frame_accounting_consistent\":false", "inconsistent accounting silently accepted");
  report.wall_seconds = std::numeric_limits<double>::infinity();
  bool rejected = false;
  try { (void)SerializeTelemetry(report); } catch (const std::invalid_argument&) { rejected = true; }
  Require(rejected, "infinite report duration accepted");
  return json;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    TestDuration();
    TestTimingAvailabilityAndWindow();
    TestProcessSemantics();
    const auto sample = TestReportSemantics();
    if (argc == 3 && std::string_view(argv[1]) == "--report") {
      std::ofstream file(argv[2], std::ios::binary);
      file << sample << '\n';
      if (!file) throw std::runtime_error("unable to write telemetry test sample");
    } else if (argc != 1) throw std::runtime_error("usage: runtime_telemetry_tests [--report FILE]");
    std::cout << "runtime telemetry: duration bounds, rolling windows, JSON/rates/nulls and process counter semantics passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
