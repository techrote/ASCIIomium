#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "runtime/process_metrics.h"
#include "runtime/timing_series.h"

namespace {
using Clock = std::chrono::steady_clock;
using asciiomium::runtime::TimingSeries;

std::uint64_t Work(std::uint64_t value) {
  for (int i = 0; i < 12; ++i) {
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
  }
  return value;
}

double Milliseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

struct Result {
  double elapsed_ms = 0;
  std::uint64_t checksum = 0;
};

Result Run(int iterations, bool instrumented,
           std::array<TimingSeries, 6>* stages) {
  std::uint64_t checksum = 0xA5C110;
  const auto begin = Clock::now();
  for (int n = 0; n < iterations; ++n) {
    if (!instrumented) {
      checksum = Work(checksum);
      continue;
    }
    // A conservative measurement-path microbenchmark: eight clock reads and
    // six bounded-history updates per render opportunity. It does not claim
    // to model CEF, renderer conversion, terminal writes or Windows scheduling.
    const auto t0 = Clock::now();
    checksum = Work(checksum);
    const auto t1 = Clock::now();
    const auto t2 = Clock::now();
    const auto t3 = Clock::now();
    const auto t4 = Clock::now();
    const auto t5 = Clock::now();
    const auto t6 = Clock::now();
    const auto t7 = Clock::now();
    (*stages)[0].AddMilliseconds(Milliseconds(t0, t1));
    (*stages)[1].AddMilliseconds(Milliseconds(t2, t3));
    (*stages)[2].AddMilliseconds(Milliseconds(t3, t4));
    (*stages)[3].AddMilliseconds(Milliseconds(t6, t7));
    (*stages)[4].AddMilliseconds(Milliseconds(t4, t5));
    (*stages)[5].AddMilliseconds(Milliseconds(t3, t4));
  }
  return {Milliseconds(begin, Clock::now()), checksum};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    int iterations = 100000;
    std::string path = "-";
    for (int n = 1; n < argc; ++n) {
      const std::string_view arg(argv[n]);
      if (n + 1 >= argc) throw std::invalid_argument("missing option value");
      const std::string_view value(argv[++n]);
      if (arg == "--json") path = value;
      else if (arg == "--iterations") {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), iterations);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            iterations < 1000 || iterations > 1000000) throw std::invalid_argument("iterations must be in 1000..1000000");
      } else throw std::invalid_argument("unknown option");
    }
    std::array<TimingSeries, 6> histories;
    std::array<double, 7> baseline_ms{}, instrumented_ms{}, incremental_ns{};
    std::uint64_t checksum = 0;
    for (std::size_t trial = 0; trial < baseline_ms.size(); ++trial) {
      Result baseline, instrumented;
      // Alternate order to reduce a systematic warm-up/order bias.
      if (trial % 2 == 0) {
        baseline = Run(iterations, false, &histories);
        instrumented = Run(iterations, true, &histories);
      } else {
        instrumented = Run(iterations, true, &histories);
        baseline = Run(iterations, false, &histories);
      }
      if (baseline.checksum != instrumented.checksum) throw std::runtime_error("instrumentation changed benchmark workload");
      checksum = baseline.checksum;
      baseline_ms[trial] = baseline.elapsed_ms;
      instrumented_ms[trial] = instrumented.elapsed_ms;
      incremental_ns[trial] = (instrumented.elapsed_ms - baseline.elapsed_ms) * 1e6 / iterations;
    }
    auto ordered = incremental_ns;
    std::sort(ordered.begin(), ordered.end());
    const auto probe_start = Clock::now();
    const auto process_start = asciiomium::runtime::ReadProcessSnapshot();
    const auto process_end = asciiomium::runtime::ReadProcessSnapshot();
    const auto probe_end = Clock::now();
    std::ofstream file;
    std::ostream* out = &std::cout;
    if (path != "-") {
      file.open(path, std::ios::binary);
      if (!file) throw std::runtime_error("cannot open overhead report");
      out = &file;
    }
    *out << std::fixed << std::setprecision(6)
         << "{\"schema\":\"asciiomium-telemetry-overhead-v1\","
         << "\"scope\":\"paired integer workload plus eight clock reads and six fixed-history updates; no CEF/terminal\","
         << "\"iterations_per_trial\":" << iterations << ",\"trials\":7,"
         << "\"clock_reads_per_instrumented_iteration\":8,\"history_updates_per_instrumented_iteration\":6,"
         << "\"history_capacity_each\":" << TimingSeries::kCapacity
         << ",\"history_storage_bytes_total\":" << sizeof(histories)
         << ",\"history_retained_samples_each\":" << histories[0].retained_count()
         << ",\"checksum\":" << checksum
         << ",\"median_incremental_ns_per_iteration\":" << ordered[3]
         << ",\"median_incremental_percent_of_50ms_budget\":" << ordered[3] / 500000.0
         << ",\"endpoint_process_probe_pair_ms\":";
#ifdef _WIN32
    *out << Milliseconds(probe_start, probe_end);
#else
    (void)probe_start;
    (void)probe_end;
    *out << "null";
#endif
    *out << ",\"endpoint_cpu_counter_available\":"
         << (process_start.cpu_time_100ns && process_end.cpu_time_100ns ? "true" : "false")
         << ",\"observations\":[";
    for (std::size_t trial = 0; trial < baseline_ms.size(); ++trial) {
      if (trial != 0) *out << ',';
      *out << "{\"baseline_ms\":" << baseline_ms[trial]
           << ",\"instrumented_ms\":" << instrumented_ms[trial]
           << ",\"incremental_ns_per_iteration\":" << incremental_ns[trial] << '}';
    }
    *out << "]}\n";
    out->flush();
    if (!*out) throw std::runtime_error("cannot finish overhead report");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "telemetry overhead: " << error.what() << '\n';
    return 1;
  }
}
