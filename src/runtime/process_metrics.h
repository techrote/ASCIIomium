#pragma once

#include <cstdint>
#include <optional>

namespace asciiomium::runtime {

// Current ASCIIomium browser process only; Chromium child processes are not
// included. Missing counters remain null rather than impersonating zero usage.
struct ProcessSnapshot {
  std::optional<std::uint64_t> cpu_time_100ns;
  // Midpoint of the steady-clock interval surrounding GetProcessTimes.
  std::optional<double> cpu_sample_time_seconds;
  std::optional<double> cpu_sample_span_seconds;
  std::optional<std::uint64_t> working_set_bytes;
  std::optional<std::uint64_t> private_bytes;
  std::optional<std::uint64_t> lifetime_peak_working_set_bytes;
  std::optional<unsigned> logical_processors;
};

struct ProcessMeasurement {
  ProcessSnapshot start;
  ProcessSnapshot end;
  std::optional<double> cpu_time_ms;
  std::optional<double> cpu_interval_seconds;
  std::optional<double> cpu_sampling_uncertainty_seconds;
  bool cpu_interval_uses_sample_clock = false;
  std::optional<double> cpu_percent_one_core;
  std::optional<double> cpu_percent_machine;
};

[[nodiscard]] ProcessSnapshot ReadProcessSnapshot() noexcept;
[[nodiscard]] ProcessMeasurement MeasureProcess(const ProcessSnapshot& start,
                                               const ProcessSnapshot& end,
                                               double wall_seconds) noexcept;

}  // namespace asciiomium::runtime
