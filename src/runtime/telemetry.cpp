#include "runtime/telemetry.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace asciiomium::runtime {
namespace {
std::string Quote(std::string_view text) {
  constexpr char hex[] = "0123456789abcdef";
  std::string out = "\"";
  for (const unsigned char ch : text) {
    if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
    else if (ch < 0x20) {
      out += "\\u00";
      out += hex[ch >> 4]; out += hex[ch & 15];
    } else out += static_cast<char>(ch);
  }
  out += '"';
  return out;
}
const char* Boolean(bool value) { return value ? "true" : "false"; }

template <typename T> void Optional(std::ostream& out, const std::optional<T>& value) {
  if (!value) out << "null";
  else if constexpr (std::is_floating_point_v<T>) {
    if (std::isfinite(*value)) out << *value;
    else out << "null";
  } else out << *value;
}
void Number(std::ostream& out, double value) {
  if (std::isfinite(value)) out << value;
  else out << "null";
}
void Ratio(std::ostream& out, std::uint64_t numerator, double denominator) {
  if (std::isfinite(denominator) && denominator > 0.0) Number(out, numerator / denominator);
  else out << "null";
}
void Snapshot(std::ostream& out, const ProcessSnapshot& snapshot) {
  out << "{\"working_set_bytes\":"; Optional(out, snapshot.working_set_bytes);
  out << ",\"private_bytes\":"; Optional(out, snapshot.private_bytes);
  out << ",\"lifetime_peak_working_set_bytes\":"; Optional(out, snapshot.lifetime_peak_working_set_bytes);
  out << ",\"logical_processors\":"; Optional(out, snapshot.logical_processors);
  out << '}';
}
}  // namespace

std::optional<int> ParseBenchmarkDuration(std::string_view text) noexcept {
  unsigned multiplier = 0;
  if (text.ends_with("ms")) { multiplier = 1; text.remove_suffix(2); }
  else if (text.ends_with('s')) { multiplier = 1000; text.remove_suffix(1); }
  else if (text.ends_with('m')) { multiplier = 60000; text.remove_suffix(1); }
  else return {};
  if (text.empty()) return {};
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value > 3'600'000u / multiplier) return {};
  const auto milliseconds = value * multiplier;
  if (milliseconds < 100) return {};
  return static_cast<int>(milliseconds);
}

std::string SerializeTiming(const TimingSeries* history) {
  if (!history) return "null";
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"sample_count\":" << history->count()
      << ",\"mean_scope\":\"all accepted samples in measurement interval\""
      << ",\"mean_ms\":";
  if (history->count() > 0) Number(out, history->average_ms());
  else out << "null";
  out << ",\"percentile_scope\":\"most recent samples\""
      << ",\"percentile_capacity\":" << TimingSeries::kCapacity
      << ",\"percentile_sample_count\":" << history->retained_count()
      << ",\"percentile_index\":\"ceil(p*(N-1)) on ascending samples\""
      << ",\"p50_ms\":";
  if (history->count() > 0) Number(out, history->percentile_ms(0.50));
  else out << "null";
  out << ",\"p95_ms\":";
  if (history->count() > 0) Number(out, history->percentile_ms(0.95));
  else out << "null";
  out << '}';
  return out.str();
}

std::string SerializeTelemetry(const TelemetryReport& r) {
  if (!std::isfinite(r.wall_seconds) || r.wall_seconds < 0.0) {
    throw std::invalid_argument("telemetry duration must be finite and nonnegative");
  }
  const auto& i = r.identity;
  const auto& c = r.config;
  const auto& n = r.counters;
  std::ostringstream out;
  out << std::fixed << std::setprecision(6)
      << "{\"schema\":" << Quote(kRuntimeTelemetrySchema)
      << ",\"identity\":{\"asciiomium_version\":" << Quote(i.asciiomium_version)
      << ",\"source_revision\":" << (i.source_identity_available ? Quote(i.source_revision) : "null")
      << ",\"source_tree\":" << (i.source_identity_available ? Quote(i.source_tree) : "null")
      << ",\"source_dirty\":" << (i.source_identity_available ? Boolean(i.source_dirty) : "null")
      << ",\"build_configuration\":" << Quote(i.build_configuration)
      << ",\"compiler\":" << Quote(i.compiler) << ",\"architecture\":" << Quote(i.architecture)
      << ",\"cef_compile\":" << Quote(i.cef_compile)
      << ",\"cef_runtime\":" << Quote(i.cef_runtime)
      << ",\"chromium_runtime\":" << Quote(i.chromium_runtime)
      << ",\"cef_headers_match_runtime\":" << Boolean(i.cef_headers_match_runtime) << '}';
  out << ",\"configuration\":{\"source_kind\":" << Quote(c.fixture_id ? "bundled-fixture" : "supplied-url")
      << ",\"fixture_id\":" << (c.fixture_id ? Quote(*c.fixture_id) : "null")
      << ",\"fixture_bundle_version\":" << Quote(c.fixture_bundle_version)
      << ",\"fixture_manifest_sha256\":" << Quote(i.fixture_bundle_sha256)
      << ",\"fixture_frozen\":" << Boolean(c.fixture_frozen)
      << ",\"source_width\":" << c.source_width << ",\"source_height\":" << c.source_height
      << ",\"terminal_columns\":" << c.columns << ",\"terminal_rows\":" << c.rows
      << ",\"glyph_encoder\":" << Quote(c.glyph_encoder) << ",\"color_mode\":" << Quote(c.colors)
      << ",\"sampling_filter\":" << Quote(c.sampling) << ",\"fit_mode\":" << Quote(c.fit)
      << ",\"cell_aspect\":"; Number(out, c.cell_aspect);
  out << ",\"emitter\":" << Quote(c.emitter) << ",\"frame_cap_fps\":" << c.frame_cap
      << ",\"cef_frame_cap_fps\":" << c.cef_frame_cap
      << ",\"full_repaint_interval_ms\":" << c.full_repaint_ms
      << ",\"terminal_output\":" << Boolean(c.terminal_output)
      << ",\"comparison_enabled\":" << Boolean(c.comparison_enabled) << '}';
  out << ",\"measurement\":{\"benchmark_mode\":" << Boolean(c.benchmark_mode)
      << ",\"requested_duration_ms\":" << c.requested_duration_ms
      << ",\"wall_seconds\":" << r.wall_seconds
      << ",\"scope\":\"render loop; excludes CEF initialization and shutdown\""
      << ",\"completed_normally\":" << Boolean(r.completed_normally)
      << ",\"exit_reason\":" << Quote(r.exit_reason) << '}';
  out << ",\"source\":{\"view_paints_start\":" << n.source_view_paints_start
      << ",\"view_paints_end\":" << n.source_view_paints_end
      << ",\"popup_paints_start\":" << n.source_popup_paints_start
      << ",\"popup_paints_end\":" << n.source_popup_paints_end
      << ",\"presentation_generation_start\":" << n.source_generation_start
      << ",\"presentation_generation_end\":" << n.source_generation_end
      << ",\"last_rendered_generation\":" << n.last_rendered_generation
      << ",\"distinct_generations_rendered\":" << n.distinct_generations_rendered
      << ",\"coalesced_generations\":" << n.coalesced_generations
      << ",\"unrendered_tail_generations\":";
  if (n.source_generation_end >= n.last_rendered_generation) out << n.source_generation_end - n.last_rendered_generation;
  else out << "null";
  out << ",\"view_paints_per_second\":";
  if (n.source_view_paints_end >= n.source_view_paints_start) Ratio(out, n.source_view_paints_end - n.source_view_paints_start, r.wall_seconds);
  else out << "null";
  out << '}';
  out << ",\"output\":{\"rendered_frames\":" << n.rendered_frames
      << ",\"emitted_frames\":" << n.emitted_frames
      << ",\"empty_payload_frames\":" << n.empty_payload_frames
      << ",\"frame_accounting_consistent\":" << Boolean(n.emitted_frames <= n.rendered_frames &&
          n.empty_payload_frames == n.rendered_frames - n.emitted_frames)
      << ",\"full_reconstructions\":" << n.full_reconstructions
      << ",\"bytes_serialized\":" << n.bytes_serialized
      << ",\"bytes_written_to_terminal\":" << (c.terminal_output && r.completed_normally ? std::to_string(n.bytes_serialized) : "null")
      << ",\"terminal_write_scope\":\"completed successful payloads; partial failed write byte counts unavailable\""
      << ",\"changed_cells\":"; Optional(out, n.changed_cells);
  out << ",\"changed_runs\":"; Optional(out, n.changed_runs);
  out << ",\"written_cells\":" << n.written_cells << ",\"emitted_runs\":" << n.emitted_runs
      << ",\"rendered_fps\":"; Ratio(out, n.rendered_frames, r.wall_seconds);
  out << ",\"emitted_fps\":"; Ratio(out, n.emitted_frames, r.wall_seconds);
  out << ",\"bytes_per_rendered_frame\":"; Ratio(out, n.bytes_serialized, static_cast<double>(n.rendered_frames));
  out << ",\"bytes_per_second\":"; Ratio(out, n.bytes_serialized, r.wall_seconds);
  out << ",\"same_frame_full_bytes\":"; Optional(out, n.same_frame_full_bytes);
  out << ",\"same_frame_differential_bytes\":"; Optional(out, n.same_frame_differential_bytes);
  out << ",\"same_frame_differential_to_full_ratio\":";
  if (n.same_frame_full_bytes && n.same_frame_differential_bytes) Ratio(out, *n.same_frame_differential_bytes, static_cast<double>(*n.same_frame_full_bytes));
  else out << "null";
  out << '}';
  out << ",\"instrumentation\":{\"timings_and_process_enabled\":" << Boolean(c.timings_enabled)
      << ",\"timing_capacity_per_stage\":" << TimingSeries::kCapacity
      << ",\"timing_series_storage_bytes\":" << sizeof(TimingSeries)
      << ",\"process_samples\":" << (r.process && c.timings_enabled ? 2 : 0)
      << ",\"comparison_note\":\"comparison adds a second serializer; do not treat paired runtime as normal single-mode throughput\"}"
      << ",\"timing\":{\"conversion\":" << SerializeTiming(c.timings_enabled ? r.timings.conversion : nullptr)
      << ",\"diff_planning\":" << SerializeTiming(c.timings_enabled ? r.timings.planning : nullptr)
      << ",\"selected_serialization\":" << SerializeTiming(c.timings_enabled ? r.timings.serialization : nullptr)
      << ",\"terminal_write\":" << SerializeTiming(c.timings_enabled && c.terminal_output ? r.timings.terminal_write : nullptr)
      << ",\"compared_full_serialization\":" << SerializeTiming(c.timings_enabled && c.comparison_enabled ? r.timings.compared_full_serialization : nullptr)
      << ",\"compared_differential_serialization\":" << SerializeTiming(c.timings_enabled && c.comparison_enabled ? r.timings.compared_differential_serialization : nullptr) << '}';
  out << ",\"process\":";
  if (!r.process || !c.timings_enabled) out << "null";
  else {
    out << "{\"scope\":\"ASCIIomium browser process only; excludes CEF child processes\",\"start\":";
    Snapshot(out, r.process->start);
    out << ",\"end\":"; Snapshot(out, r.process->end);
    out << ",\"cpu_time_ms\":"; Optional(out, r.process->cpu_time_ms);
    out << ",\"cpu_interval_seconds\":"; Optional(out, r.process->cpu_interval_seconds);
    out << ",\"cpu_sampling_uncertainty_seconds\":"; Optional(out, r.process->cpu_sampling_uncertainty_seconds);
    out << ",\"cpu_interval_source\":";
    if (r.process->cpu_interval_seconds) {
      out << Quote(r.process->cpu_interval_uses_sample_clock
          ? "steady-clock midpoints around GetProcessTimes" : "render-loop wall fallback when sample timestamps unavailable");
    } else out << "null";
    out << ",\"cpu_percent_one_core\":"; Optional(out, r.process->cpu_percent_one_core);
    out << ",\"cpu_percent_machine\":"; Optional(out, r.process->cpu_percent_machine);
    out << '}';
  }
  out << '}';
  return out.str();
}

}  // namespace asciiomium::runtime
