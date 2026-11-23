#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "runtime/process_metrics.h"
#include "runtime/timing_series.h"

namespace asciiomium::runtime {

inline constexpr char kRuntimeTelemetrySchema[] = "asciiomium-runtime-telemetry-v1";

// Integer duration + mandatory ms/s/m suffix; 100 ms .. one hour. Full input
// consumption and checked multiplication prevent overflow or silent truncation.
[[nodiscard]] std::optional<int> ParseBenchmarkDuration(std::string_view text) noexcept;

struct TelemetryIdentity {
  std::string asciiomium_version;
  std::string source_revision;
  std::string source_tree;
  bool source_identity_available = false;
  bool source_dirty = false;
  std::string build_configuration;
  std::string compiler;
  std::string architecture;
  std::string cef_compile;
  std::string cef_runtime;
  std::string chromium_runtime;
  bool cef_headers_match_runtime = false;
  std::string fixture_bundle_sha256;
};

struct TelemetryConfig {
  std::optional<std::string> fixture_id;
  std::string fixture_bundle_version = "web-fixtures-v1";
  std::string glyph_encoder = "halfblock-u2580";
  std::string colors;
  std::string sampling;
  std::string fit;
  std::string emitter;
  double cell_aspect = 0.5;
  int source_width = 0;
  int source_height = 0;
  int columns = 0;
  int rows = 0;
  int frame_cap = 0;
  int cef_frame_cap = 0;
  int requested_duration_ms = 0;
  int full_repaint_ms = 0;
  bool fixture_frozen = false;
  bool benchmark_mode = false;
  bool terminal_output = false;
  bool comparison_enabled = false;
  bool timings_enabled = true;
};

struct TelemetryCounters {
  std::uint64_t source_view_paints_start = 0;
  std::uint64_t source_view_paints_end = 0;
  std::uint64_t source_popup_paints_start = 0;
  std::uint64_t source_popup_paints_end = 0;
  std::uint64_t source_generation_start = 0;
  std::uint64_t source_generation_end = 0;
  std::uint64_t last_rendered_generation = 0;
  std::uint64_t distinct_generations_rendered = 0;
  std::uint64_t rendered_frames = 0;
  std::uint64_t emitted_frames = 0;
  std::uint64_t empty_payload_frames = 0;
  std::uint64_t coalesced_generations = 0;
  std::uint64_t full_reconstructions = 0;
  std::uint64_t bytes_serialized = 0;
  std::uint64_t written_cells = 0;
  std::uint64_t emitted_runs = 0;
  std::optional<std::uint64_t> changed_cells;
  std::optional<std::uint64_t> changed_runs;
  std::optional<std::uint64_t> same_frame_full_bytes;
  std::optional<std::uint64_t> same_frame_differential_bytes;
};

// Non-owning references avoid copying several 2048-sample histories just to
// produce the final report. Null means unavailable; an empty history has zero
// samples and null timing values, not a measured zero-duration operation.
struct TelemetryTimings {
  const TimingSeries* conversion = nullptr;
  const TimingSeries* planning = nullptr;
  const TimingSeries* serialization = nullptr;
  const TimingSeries* terminal_write = nullptr;
  const TimingSeries* compared_full_serialization = nullptr;
  const TimingSeries* compared_differential_serialization = nullptr;
};

struct TelemetryReport {
  TelemetryIdentity identity;
  TelemetryConfig config;
  TelemetryCounters counters;
  TelemetryTimings timings;
  std::optional<ProcessMeasurement> process;
  double wall_seconds = 0.0;
  bool completed_normally = false;
  std::string exit_reason;
};

[[nodiscard]] std::string SerializeTelemetry(const TelemetryReport& report);
[[nodiscard]] std::string SerializeTiming(const TimingSeries* history);

}  // namespace asciiomium::runtime
