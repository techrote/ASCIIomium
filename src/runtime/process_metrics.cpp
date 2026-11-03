#include "runtime/process_metrics.h"

#include <cmath>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace asciiomium::runtime {

ProcessSnapshot ReadProcessSnapshot() noexcept {
  ProcessSnapshot snapshot;
#ifdef _WIN32
  FILETIME creation{}, exit{}, kernel{}, user{};
  const auto cpu_sample_start = std::chrono::steady_clock::now();
  if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
    const auto cpu_sample_end = std::chrono::steady_clock::now();
    const double span = std::chrono::duration<double>(cpu_sample_end - cpu_sample_start).count();
    snapshot.cpu_sample_time_seconds =
        std::chrono::duration<double>(cpu_sample_start.time_since_epoch()).count() + span * 0.5;
    snapshot.cpu_sample_span_seconds = span;
    const auto ticks = [](const FILETIME& time) {
      return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) |
             time.dwLowDateTime;
    };
    snapshot.cpu_time_100ns = ticks(kernel) + ticks(user);
  }
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                          sizeof(memory))) {
    snapshot.working_set_bytes = memory.WorkingSetSize;
    snapshot.private_bytes = memory.PrivateUsage;
    snapshot.lifetime_peak_working_set_bytes = memory.PeakWorkingSetSize;
  }
  const DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (processors > 0) snapshot.logical_processors = processors;
#endif
  return snapshot;
}

ProcessMeasurement MeasureProcess(const ProcessSnapshot& start,
                                  const ProcessSnapshot& end,
                                  double wall_seconds) noexcept {
  ProcessMeasurement result;
  result.start = start;
  result.end = end;
  if (start.cpu_time_100ns && end.cpu_time_100ns &&
      *end.cpu_time_100ns >= *start.cpu_time_100ns) {
    result.cpu_time_ms = static_cast<double>(*end.cpu_time_100ns -
                                            *start.cpu_time_100ns) / 10000.0;
    if (start.cpu_sample_time_seconds && end.cpu_sample_time_seconds) {
      const double interval = *end.cpu_sample_time_seconds - *start.cpu_sample_time_seconds;
      if (std::isfinite(interval) && interval > 0.0) {
        result.cpu_interval_seconds = interval;
        result.cpu_interval_uses_sample_clock = true;
      }
      if (start.cpu_sample_span_seconds && end.cpu_sample_span_seconds) {
        const double uncertainty = (*start.cpu_sample_span_seconds + *end.cpu_sample_span_seconds) * 0.5;
        if (std::isfinite(uncertainty) && uncertainty >= 0.0) {
          result.cpu_sampling_uncertainty_seconds = uncertainty;
        }
      }
    } else if (std::isfinite(wall_seconds) && wall_seconds > 0.0) {
      // Pure callers without timed snapshots can still provide an explicitly
      // identified wall-interval approximation. Windows uses sample clocks.
      result.cpu_interval_seconds = wall_seconds;
    }
    if (result.cpu_interval_seconds) {
      const double percent = *result.cpu_time_ms / (*result.cpu_interval_seconds * 10.0);
      if (std::isfinite(percent)) result.cpu_percent_one_core = percent;
      if (result.cpu_percent_one_core && start.logical_processors && end.logical_processors &&
          *end.logical_processors > 0 &&
          *start.logical_processors == *end.logical_processors) {
        result.cpu_percent_machine = *result.cpu_percent_one_core / *end.logical_processors;
      }
    }
  }
  return result;
}

}  // namespace asciiomium::runtime
