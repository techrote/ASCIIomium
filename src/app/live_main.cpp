#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "browser/cef_key_translator.h"
#include "browser/cef_pointer_translator.h"
#include "browser/osr_client.h"
#include "browser/popup_compositor.h"
#include "browser/source_frame.h"
#include "geometry/viewport_transform.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_version.h"
#include "include/cef_version_info.h"
#include "input/vt_input_parser.h"
#include "render/color_quantizer.h"
#include "render/halfblock_renderer.h"
#include "render/render_types.h"
#include "runtime/frame_scheduler.h"
#include "runtime/timing_series.h"
#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_session.h"

namespace {

using Clock = asciiomium::runtime::FrameScheduler::Clock;

struct Options {
  std::string fixture = "motion-scroll";
  std::string url;
  int width = 960;
  int height = 540;
  int fps = 20;
  int cef_fps = 60;
  int columns = 120;
  int rows = 40;
  int duration_ms = 0;
  int max_frames = 0;
  double cell_aspect = 0.5;
  asciiomium::geometry::FitMode fit_mode =
      asciiomium::geometry::FitMode::Contain;
  bool freeze = false;
  bool terminal_output = true;
  bool quiet = false;
  bool geometry_debug = false;
  bool input_debug = false;
  int wheel_delta = 120;
  std::string input_script;
  std::string input_hex;
  asciiomium::render::ColorMode color_mode =
      asciiomium::render::ColorMode::Rgb1024;
  asciiomium::render::SamplingFilter filter =
      asciiomium::render::SamplingFilter::BoxAverage;
  std::filesystem::path report;
  std::filesystem::path evidence_svg;
  std::filesystem::path vt_output;
};

struct LiveEvidence {
  std::string url;
  std::string exit_reason = "unknown";
  bool load_complete = false;
  bool closed_cleanly = false;
  bool terminal_restored = false;
  int http_status = 0;
  int load_error = 0;
  std::string load_error_text;
  std::string failed_url;
  double duration_seconds = 0.0;
  int terminal_columns = 0;
  int terminal_rows = 0;
  std::uint64_t popup_composited_frames = 0;
  std::uint64_t loop_iterations = 0;
  std::uint64_t idle_iterations = 0;
  std::uint64_t input_decoded_events = 0;
  std::uint64_t input_unsupported_events = 0;
  std::uint64_t input_cef_events = 0;
  std::uint64_t pointer_decoded_events = 0;
  std::uint64_t pointer_cef_events = 0;
  std::uint64_t pointer_rejected_events = 0;
  bool input_script_sent = false;
  bool input_hex_sent = false;
  bool input_focus_observed = false;
  bool input_dom_observed = false;
  bool pointer_dom_observed = false;
  bool pointer_scroll_observed = false;
  bool input_paint_observed = false;
  std::uint64_t input_generation_before = 0;
  std::uint64_t input_generation_after = 0;
  std::size_t last_vt_bytes = 0;
  std::uint64_t geometry_generation = 0;
  std::uint64_t cef_resize_requests = 0;
  int sample_columns = 0;
  int sample_rows = 0;
  asciiomium::geometry::RectD content_sample_rect{};
  asciiomium::geometry::RectD content_terminal_rect{};
  asciiomium::browser::SourceFrameState source_state{};
  asciiomium::runtime::FrameSchedulerStats scheduler{};
  asciiomium::runtime::TimingSeries conversion_timing;
  asciiomium::runtime::TimingSeries serialization_timing;
  asciiomium::runtime::TimingSeries write_timing;
};

bool ParseInt(std::string_view text, int minimum, int maximum, int* value) {
  int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed < minimum || parsed > maximum) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParseHexBytes(std::string_view text, std::string* bytes) {
  if (bytes == nullptr || text.empty() || (text.size() % 2) != 0) {
    return false;
  }

  auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };

  std::string parsed;
  parsed.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int high = nibble(text[i]);
    const int low = nibble(text[i + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    parsed.push_back(static_cast<char>((high << 4) | low));
  }
  *bytes = std::move(parsed);
  return true;
}

bool ParseFilter(std::string_view text,
                 asciiomium::render::SamplingFilter* filter) {
  if (text == "box" || text == "box-average") {
    *filter = asciiomium::render::SamplingFilter::BoxAverage;
    return true;
  }
  if (text == "nearest") {
    *filter = asciiomium::render::SamplingFilter::Nearest;
    return true;
  }
  return false;
}

std::string_view FilterName(asciiomium::render::SamplingFilter filter) {
  switch (filter) {
    case asciiomium::render::SamplingFilter::Nearest:
      return "nearest";
    case asciiomium::render::SamplingFilter::BoxAverage:
      return "box";
  }
  return "unknown";
}

void PrintUsage() {
  std::cout
      << "Usage: asciiomium_live [options]\n"
      << "  --fixture NAME          index|flat-ui|colour-ramps|raster-imagery|motion-scroll|input-focus\n"
      << "  --url URL               explicit URL instead of a bundled fixture\n"
      << "  --width N --height N    CEF viewport (default 960x540)\n"
      << "  --fps N                 terminal max FPS, 1..60 (default 20; useful 10/15/20/30)\n"
      << "  --cef-fps N             CEF windowless frame-rate cap, 1..60 (default 60)\n"
      << "  --colors MODE           true|16|256|512|1024 (default 1024)\n"
      << "  --filter MODE           box|nearest (default box)\n"
      << "  --fit MODE              contain|stretch (default contain)\n"
      << "  --cell-aspect RATIO     cell width/height, 0.05..4 (default 0.5)\n"
      << "  --geometry-debug        log viewport generations and mappings to stderr\n"
      << "  --freeze                append ?freeze=1 to bundled fixture URL\n"
      << "  --duration-ms N         stop after N ms; 0 means until quit chord (default 0)\n"
      << "  --max-frames N          stop after N emitted frames; 0 means unlimited\n"
      << "  --no-terminal           serialize frames without acquiring a console (CI/benchmark)\n"
      << "  --columns N --rows N    logical target for --no-terminal (default 120x40)\n"
      << "  --report FILE.json      write live-run metrics\n"
      << "  --evidence-svg FILE     write the final logical half-block frame as SVG\n"
      << "  --vt-output FILE        write the final full-frame VT payload verbatim\n"
      << "  --input-debug           log raw/decoded/CEF keyboard and pointer events\n"
      << "  --input-script TEXT     inject UTF-8 bytes through the parser (CI/test aid)\n"
      << "  --input-hex HEX         inject raw bytes as hexadecimal (CI/test aid)\n"
      << "  --wheel-delta N         CEF wheel delta per terminal notch, 1..1200 (default 120)\n"
      << "  --quiet                 suppress final one-line summary\n"
      << "\nCtrl+Alt+Q exits interactive browser mode. Ctrl+C is forwarded to CEF.\n";
}

bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    }

    auto take_string = [&](std::string* destination) -> bool {
      if (i + 1 >= argc) return false;
      *destination = argv[++i];
      return true;
    };
    auto take_path = [&](std::filesystem::path* destination) -> bool {
      if (i + 1 >= argc) return false;
      *destination = std::filesystem::path(argv[++i]);
      return true;
    };
    auto take_int = [&](int minimum, int maximum, int* destination) -> bool {
      if (i + 1 >= argc ||
          !ParseInt(argv[i + 1], minimum, maximum, destination)) {
        return false;
      }
      ++i;
      return true;
    };

    if (arg == "--fixture") {
      if (!take_string(&options->fixture)) return false;
    } else if (arg == "--url") {
      if (!take_string(&options->url)) return false;
    } else if (arg == "--width") {
      if (!take_int(1, 8192, &options->width)) return false;
    } else if (arg == "--height") {
      if (!take_int(1, 8192, &options->height)) return false;
    } else if (arg == "--fps") {
      if (!take_int(1, 60, &options->fps)) return false;
    } else if (arg == "--cef-fps") {
      if (!take_int(1, 60, &options->cef_fps)) return false;
    } else if (arg == "--columns") {
      if (!take_int(1, 1024, &options->columns)) return false;
    } else if (arg == "--rows") {
      if (!take_int(1, 512, &options->rows)) return false;
    } else if (arg == "--duration-ms") {
      if (!take_int(0, 3'600'000, &options->duration_ms)) return false;
    } else if (arg == "--max-frames") {
      if (!take_int(0, 1'000'000, &options->max_frames)) return false;
    } else if (arg == "--colors") {
      if (i + 1 >= argc ||
          !asciiomium::render::TryParseColorMode(argv[i + 1],
                                                 &options->color_mode)) {
        return false;
      }
      ++i;
    } else if (arg == "--filter") {
      if (i + 1 >= argc || !ParseFilter(argv[i + 1], &options->filter)) {
        return false;
      }
      ++i;
    } else if (arg == "--fit") {
      if (i + 1 >= argc ||
          !asciiomium::geometry::TryParseFitMode(argv[i + 1],
                                                 &options->fit_mode)) {
        return false;
      }
      ++i;
    } else if (arg == "--cell-aspect") {
      if (i + 1 >= argc ||
          !asciiomium::geometry::TryParseCellAspect(argv[i + 1],
                                                    &options->cell_aspect)) {
        return false;
      }
      ++i;
    } else if (arg == "--report") {
      if (!take_path(&options->report)) return false;
    } else if (arg == "--evidence-svg") {
      if (!take_path(&options->evidence_svg)) return false;
    } else if (arg == "--vt-output") {
      if (!take_path(&options->vt_output)) return false;
    } else if (arg == "--input-script") {
      if (!take_string(&options->input_script)) return false;
    } else if (arg == "--input-hex") {
      if (!take_string(&options->input_hex)) return false;
      std::string parsed;
      if (!ParseHexBytes(options->input_hex, &parsed)) return false;
    } else if (arg == "--wheel-delta") {
      if (!take_int(1, 1200, &options->wheel_delta)) return false;
    } else if (arg == "--input-debug") {
      options->input_debug = true;
    } else if (arg == "--freeze") {
      options->freeze = true;
    } else if (arg == "--no-terminal") {
      options->terminal_output = false;
    } else if (arg == "--quiet") {
      options->quiet = true;
    } else if (arg == "--geometry-debug") {
      options->geometry_debug = true;
    } else {
      return false;
    }
  }

  // Headless/CI runs need a deterministic stop condition. Interactive terminal
  // mode may intentionally run forever until the browser-mode quit chord is
  // decoded (or a separate console-control shutdown occurs).
  if (!options->terminal_output && options->duration_ms == 0 &&
      options->max_frames == 0) {
    return false;
  }
  if (!options->input_script.empty() && !options->input_hex.empty()) {
    return false;
  }
  return true;
}

std::string FixtureFileName(std::string_view fixture) {
  if (fixture == "index") return "index.html";
  if (fixture == "flat-ui") return "flat-ui.html";
  if (fixture == "colour-ramps") return "colour-ramps.html";
  if (fixture == "raster-imagery") return "raster-imagery.html";
  if (fixture == "motion-scroll") return "motion-scroll.html";
  if (fixture == "input-focus") return "input-focus.html";
  return {};
}

bool IsUrlSafeByte(unsigned char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
         ch == '~' || ch == '/' || ch == ':';
}

std::string FileUrl(const std::filesystem::path& path) {
  const auto absolute = std::filesystem::absolute(path);
  const auto u8 = absolute.generic_u8string();
  std::ostringstream encoded;
  encoded << "file:///" << std::uppercase << std::hex;
  for (const char8_t byte : u8) {
    const auto ch = static_cast<unsigned char>(byte);
    if (IsUrlSafeByte(ch)) {
      encoded << static_cast<char>(ch);
    } else {
      encoded << '%' << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(ch) << std::setfill(' ');
    }
  }
  return encoded.str();
}

std::string ResolveUrl(const Options& options) {
  if (!options.url.empty()) {
    return options.url;
  }
  const std::string file = FixtureFileName(options.fixture);
  if (file.empty()) {
    throw std::runtime_error("unknown bundled fixture: " + options.fixture);
  }
  const std::filesystem::path path =
      std::filesystem::path(ASCIIOMIUM_SOURCE_DIR) / "fixtures" / "web" / file;
  std::string url = FileUrl(path);
  if (options.freeze) {
    url += "?freeze=1";
  }
  return url;
}

std::string JsonEscape(std::string_view text) {
  std::ostringstream out;
  for (const unsigned char ch : text) {
    switch (ch) {
      case '\"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
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

std::string JoinVersion(int a, int b, int c, int d) {
  std::ostringstream out;
  out << a << '.' << b << '.' << c << '.' << d;
  return out.str();
}

std::string CefRuntimeVersion() {
  std::ostringstream out;
  out << cef_version_info(0) << '.' << cef_version_info(1) << '.'
      << cef_version_info(2);
  return out.str();
}

std::string ChromiumRuntimeVersion() {
  return JoinVersion(cef_version_info(4), cef_version_info(5),
                     cef_version_info(6), cef_version_info(7));
}

void EnsureParentDirectory(const std::filesystem::path& path) {
  if (!path.empty() && path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
}

void WriteBinaryFile(const std::filesystem::path& path, std::string_view bytes) {
  EnsureParentDirectory(path);
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("unable to open output file: " + path.string());
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("failed while writing output file: " + path.string());
  }
}

void WriteTerminalFrameSvg(const std::filesystem::path& path,
                           const asciiomium::render::TerminalFrame& frame) {
  if (frame.columns() <= 0 || frame.rows() <= 0) {
    throw std::invalid_argument("cannot write SVG for empty terminal frame");
  }
  EnsureParentDirectory(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("unable to open terminal evidence SVG");
  }

  constexpr int kCellWidth = 4;
  constexpr int kCellHeight = 8;
  const int width = frame.columns() * kCellWidth;
  const int height = frame.rows() * kCellHeight;
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
      << "\" height=\"" << height << "\" viewBox=\"0 0 " << width << ' '
      << height << "\" shape-rendering=\"crispEdges\">\n";
  out << "<rect width=\"100%\" height=\"100%\" fill=\"black\"/>\n";

  auto rect = [&](int x, int y, const asciiomium::render::Rgb8& rgb) {
    out << "<rect x=\"" << x << "\" y=\"" << y
        << "\" width=\"4\" height=\"4\" fill=\"rgb("
        << static_cast<unsigned>(rgb.r) << ','
        << static_cast<unsigned>(rgb.g) << ','
        << static_cast<unsigned>(rgb.b) << ")\"/>\n";
  };

  for (int row = 0; row < frame.rows(); ++row) {
    for (int column = 0; column < frame.columns(); ++column) {
      const auto& cell = frame.at(column, row);
      const int x = column * kCellWidth;
      const int y = row * kCellHeight;
      rect(x, y, cell.foreground.rgb);
      rect(x, y + kCellHeight / 2, cell.background.rgb);
    }
  }
  out << "</svg>\n";
}

void WriteReport(const std::filesystem::path& path,
                 const Options& options,
                 const LiveEvidence& evidence) {
  if (path.empty()) return;
  EnsureParentDirectory(path);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("unable to open live report output");
  }

  const double duration = std::max(0.000001, evidence.duration_seconds);
  const double emitted_fps =
      static_cast<double>(evidence.scheduler.emitted_frames) / duration;
  const double source_fps =
      static_cast<double>(evidence.source_state.view_paint_count) / duration;

  out << std::fixed << std::setprecision(6)
      << "{\n"
      << "  \"schema\":\"asciiomium-live-v1\",\n"
      << "  \"cef_compile\":\"" << JsonEscape(CEF_VERSION) << "\",\n"
      << "  \"cef_runtime\":\"" << CefRuntimeVersion() << "\",\n"
      << "  \"chromium_runtime\":\"" << ChromiumRuntimeVersion() << "\",\n"
      << "  \"url\":\"" << JsonEscape(evidence.url) << "\",\n"
      << "  \"exit_reason\":\"" << JsonEscape(evidence.exit_reason) << "\",\n"
      << "  \"sandbox\":\"disabled-temporary-direct-executable-bootstrap\",\n"
      << "  \"load\":{\"complete\":"
      << (evidence.load_complete ? "true" : "false")
      << ",\"http_status\":" << evidence.http_status
      << ",\"error_code\":" << evidence.load_error
      << ",\"error_text\":\"" << JsonEscape(evidence.load_error_text)
      << "\",\"failed_url\":\"" << JsonEscape(evidence.failed_url)
      << "\"},\n"
      << "  \"source\":{\"viewport_width\":" << options.width
      << ",\"viewport_height\":" << options.height
      << ",\"cef_fps_cap\":" << options.cef_fps
      << ",\"presentation_generation\":"
      << evidence.source_state.presentation_generation
      << ",\"view_generation\":" << evidence.source_state.view_generation
      << ",\"view_paint_count\":" << evidence.source_state.view_paint_count
      << ",\"popup_generation\":" << evidence.source_state.popup_generation
      << ",\"popup_paint_count\":" << evidence.source_state.popup_paint_count
      << ",\"popup_visible\":"
      << (evidence.source_state.popup_visible ? "true" : "false")
      << ",\"observed_view_paints_per_second\":" << source_fps << "},\n"
      << "  \"renderer\":{\"glyph_encoder\":\"halfblock-u2580\""
      << ",\"color_mode\":\""
      << asciiomium::render::ColorModeName(options.color_mode)
      << "\",\"sampling_filter\":\"" << FilterName(options.filter)
      << "\",\"max_fps\":" << options.fps
      << ",\"terminal_columns\":" << evidence.terminal_columns
      << ",\"terminal_rows\":" << evidence.terminal_rows
      << ",\"fit_mode\":\""
      << asciiomium::geometry::FitModeName(options.fit_mode)
      << "\",\"cell_aspect\":" << options.cell_aspect << "},\n"
      << "  \"geometry\":{\"generation\":" << evidence.geometry_generation
      << ",\"sample_columns\":" << evidence.sample_columns
      << ",\"sample_rows\":" << evidence.sample_rows
      << ",\"content_sample_rect\":["
      << evidence.content_sample_rect.x << ',' << evidence.content_sample_rect.y
      << ',' << evidence.content_sample_rect.width << ','
      << evidence.content_sample_rect.height << "]"
      << ",\"content_terminal_rect\":["
      << evidence.content_terminal_rect.x << ','
      << evidence.content_terminal_rect.y << ','
      << evidence.content_terminal_rect.width << ','
      << evidence.content_terminal_rect.height << "]"
      << ",\"cef_resize_requests\":" << evidence.cef_resize_requests
      << "},\n"
      << "  \"scheduler\":{\"latest_source_generation\":"
      << evidence.scheduler.latest_source_generation
      << ",\"last_rendered_source_generation\":"
      << evidence.scheduler.last_rendered_source_generation
      << ",\"rendered_frames\":" << evidence.scheduler.rendered_frames
      << ",\"emitted_frames\":" << evidence.scheduler.emitted_frames
      << ",\"coalesced_generations\":"
      << evidence.scheduler.coalesced_source_generations
      << ",\"forced_refreshes\":" << evidence.scheduler.forced_refreshes
      << ",\"observed_emitted_fps\":" << emitted_fps << "},\n"
      << "  \"timing_ms\":{\"conversion\":{\"average\":"
      << evidence.conversion_timing.average_ms() << ",\"p95\":"
      << evidence.conversion_timing.percentile_ms(0.95)
      << "},\"serialization\":{\"average\":"
      << evidence.serialization_timing.average_ms() << ",\"p95\":"
      << evidence.serialization_timing.percentile_ms(0.95)
      << "},\"terminal_write\":{\"average\":"
      << evidence.write_timing.average_ms() << ",\"p95\":"
      << evidence.write_timing.percentile_ms(0.95) << "}},\n"
      << "  \"input\":{\"decoded_events\":" << evidence.input_decoded_events
      << ",\"unsupported_events\":" << evidence.input_unsupported_events
      << ",\"cef_events\":" << evidence.input_cef_events
      << ",\"pointer_decoded_events\":" << evidence.pointer_decoded_events
      << ",\"pointer_cef_events\":" << evidence.pointer_cef_events
      << ",\"pointer_rejected_events\":" << evidence.pointer_rejected_events
      << ",\"script_sent\":"
      << (evidence.input_script_sent ? "true" : "false")
      << ",\"hex_sent\":"
      << (evidence.input_hex_sent ? "true" : "false")
      << ",\"focus_observed\":"
      << (evidence.input_focus_observed ? "true" : "false")
      << ",\"dom_observed\":"
      << (evidence.input_dom_observed ? "true" : "false")
      << ",\"pointer_dom_observed\":"
      << (evidence.pointer_dom_observed ? "true" : "false")
      << ",\"pointer_scroll_observed\":"
      << (evidence.pointer_scroll_observed ? "true" : "false")
      << ",\"wheel_delta_per_notch\":" << options.wheel_delta
      << ",\"paint_observed\":"
      << (evidence.input_paint_observed ? "true" : "false")
      << ",\"generation_before\":" << evidence.input_generation_before
      << ",\"generation_after\":" << evidence.input_generation_after
      << "},\n"
      << "  \"runtime\":{\"duration_seconds\":" << evidence.duration_seconds
      << ",\"loop_iterations\":" << evidence.loop_iterations
      << ",\"idle_iterations\":" << evidence.idle_iterations
      << ",\"popup_composited_frames\":" << evidence.popup_composited_frames
      << ",\"last_vt_bytes\":" << evidence.last_vt_bytes
      << ",\"terminal_output\":"
      << (options.terminal_output ? "true" : "false")
      << ",\"terminal_restored\":"
      << (evidence.terminal_restored ? "true" : "false")
      << ",\"closed_cleanly\":"
      << (evidence.closed_cleanly ? "true" : "false") << "}\n"
      << "}\n";
}

double Milliseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool IsQuitChord(const asciiomium::input::KeyboardEvent& event) {
  using namespace asciiomium::input;
  return event.kind == KeyboardEventKind::Key &&
         event.key == KeyCode::Character &&
         (event.codepoint == U'Q' || event.codepoint == U'q') &&
         HasModifier(event.modifiers, kModifierControl) &&
         HasModifier(event.modifiers, kModifierAlt);
}

void ForwardInput(
    const std::vector<asciiomium::input::DecodedInputEvent>& decoded_events,
    CefRefPtr<asciiomium::browser::OsrClient> client,
    const asciiomium::geometry::ViewportSnapshot& viewport_snapshot,
    const Options& options,
    LiveEvidence* evidence,
    bool* quit_requested) {
  for (const auto& decoded : decoded_events) {
    ++evidence->input_decoded_events;

    if (decoded.is_pointer()) {
      ++evidence->pointer_decoded_events;
      if (options.input_debug) {
        std::cerr << "[input] raw="
                  << asciiomium::input::EscapeBytes(decoded.raw_bytes)
                  << " decoded="
                  << asciiomium::input::DescribePointerEvent(decoded.pointer);
        if (!decoded.diagnostic.empty()) {
          std::cerr << " note=" << decoded.diagnostic;
        }
        std::cerr << '\n';
      }

      if (!viewport_snapshot.valid()) {
        ++evidence->pointer_rejected_events;
        if (options.input_debug) {
          std::cerr << "[input] pointer rejected: no viewport transform\n";
        }
        continue;
      }

      const auto dispatch =
          asciiomium::browser::TranslatePointerEvent(
              decoded.pointer, *viewport_snapshot.transform,
              options.wheel_delta);
      if (!dispatch.has_value()) {
        ++evidence->pointer_rejected_events;
        if (options.input_debug) {
          std::cerr << "[input] pointer rejected geometry="
                    << viewport_snapshot.generation
                    << " (outside browser content or invalid mapping)\n";
        }
        continue;
      }

      if (options.input_debug) {
        std::cerr << "[input] geometry=" << viewport_snapshot.generation
                  << " cef="
                  << asciiomium::browser::DescribeCefPointerDispatch(
                         *dispatch)
                  << '\n';
      }
      if (client->SendPointerEvent(*dispatch)) {
        ++evidence->pointer_cef_events;
        ++evidence->input_cef_events;
      }
      continue;
    }

    if (options.input_debug) {
      std::cerr << "[input] raw="
                << asciiomium::input::EscapeBytes(decoded.raw_bytes)
                << " decoded="
                << asciiomium::input::DescribeKeyboardEvent(decoded.event);
      if (!decoded.diagnostic.empty()) {
        std::cerr << " note=" << decoded.diagnostic;
      }
      std::cerr << '\n';
    }

    if (decoded.event.kind ==
        asciiomium::input::KeyboardEventKind::Unsupported) {
      ++evidence->input_unsupported_events;
      continue;
    }

    if (IsQuitChord(decoded.event)) {
      *quit_requested = true;
      if (options.input_debug) {
        std::cerr << "[input] application quit chord consumed\n";
      }
      continue;
    }

    const auto cef_events =
        asciiomium::browser::TranslateKeyboardEvent(decoded.event);
    if (options.input_debug) {
      if (cef_events.empty()) {
        std::cerr << "[input] cef=<none> mapping-unavailable\n";
      } else {
        for (const auto& event : cef_events) {
          std::cerr << "[input] cef="
                    << asciiomium::browser::DescribeCefKeyEvent(event) << '\n';
        }
      }
    }
    evidence->input_cef_events += client->SendKeyEvents(cef_events);
  }
}

asciiomium::geometry::ViewportTransform MakeViewportTransform(
    const Options& options, int columns, int rows) {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = columns;
  config.terminal_rows = rows;
  config.browser_width = options.width;
  config.browser_height = options.height;
  config.cell_aspect = options.cell_aspect;
  config.fit_mode = options.fit_mode;
  return asciiomium::geometry::ViewportTransform::Create(config);
}

void RecordGeometry(const asciiomium::geometry::ViewportSnapshot& snapshot,
                    LiveEvidence* evidence) {
  if (!snapshot.valid()) {
    return;
  }
  evidence->geometry_generation = snapshot.generation;
  evidence->terminal_columns = snapshot.transform->terminal_columns();
  evidence->terminal_rows = snapshot.transform->terminal_rows();
  evidence->sample_columns = snapshot.transform->sample_columns();
  evidence->sample_rows = snapshot.transform->sample_rows();
  evidence->content_sample_rect = snapshot.transform->content_sample_rect();
  evidence->content_terminal_rect = snapshot.transform->content_terminal_rect();
}

void LogGeometry(const asciiomium::geometry::ViewportSnapshot& snapshot,
                 bool cef_resize_requested) {
  if (!snapshot.valid()) {
    return;
  }
  const auto& transform = *snapshot.transform;
  const auto& content = transform.content_sample_rect();
  std::cerr << "geometry generation=" << snapshot.generation
            << " terminal=" << transform.terminal_columns() << 'x'
            << transform.terminal_rows()
            << " browser=" << transform.browser_width() << 'x'
            << transform.browser_height()
            << " samples=" << transform.sample_columns() << 'x'
            << transform.sample_rows()
            << " fit=" << asciiomium::geometry::FitModeName(transform.fit_mode())
            << " cell_aspect=" << transform.cell_aspect()
            << " content_sample=[" << content.x << ',' << content.y << ','
            << content.width << ',' << content.height << ']'
            << " cef_resize=" << (cef_resize_requested ? "true" : "false")
            << '\n';
}

int BrowserProcessMain(const Options& options, const CefMainArgs& main_args) {
  CefSettings settings;
  settings.windowless_rendering_enabled = true;
  settings.multi_threaded_message_loop = false;
  settings.external_message_pump = false;
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_WARNING;

  if (!CefInitialize(main_args, settings, nullptr, nullptr)) {
    std::cerr << "CefInitialize failed with exit code " << CefGetExitCode() << '\n';
    return 3;
  }

  int result = 0;
  LiveEvidence evidence;
  evidence.url = ResolveUrl(options);

  auto frame_store = std::make_shared<asciiomium::browser::SourceFrameStore>();
  CefRefPtr<asciiomium::browser::OsrClient> client(
      new asciiomium::browser::OsrClient(frame_store, options.width,
                                         options.height));
  std::unique_ptr<asciiomium::terminal::TerminalSession> terminal_session;
  asciiomium::render::TerminalFrame last_terminal_frame;
  std::string last_vt_payload;
  bool have_terminal_frame = false;
  asciiomium::input::VtInputParser input_parser;
  bool quit_requested = false;
  bool input_script_focus_requested = false;
  bool input_script_injected = false;
  std::string raw_injected_input;
  if (!options.input_hex.empty()) {
    if (!ParseHexBytes(options.input_hex, &raw_injected_input)) {
      throw std::invalid_argument("invalid --input-hex payload");
    }
  }
  auto last_input_byte_at = Clock::now();

  CefWindowInfo window_info;
  window_info.SetAsWindowless(nullptr);

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = options.cef_fps;
  browser_settings.background_color = CefColorSetARGB(255, 16, 20, 26);

  if (!CefBrowserHost::CreateBrowser(window_info, client, evidence.url,
                                     browser_settings, nullptr, nullptr)) {
    std::cerr << "CefBrowserHost::CreateBrowser failed\n";
    client = nullptr;
    CefShutdown();
    return 4;
  }

  asciiomium::runtime::FrameScheduler scheduler(options.fps);
  asciiomium::render::ModeQuantizer quantizer(options.color_mode);
  asciiomium::render::RenderConfig render_config;
  render_config.filter = options.filter;
  render_config.fit_mode = options.fit_mode;
  render_config.cell_aspect = options.cell_aspect;
  render_config.quantizer = &quantizer;

  asciiomium::geometry::ViewportState viewport_state;
  int target_columns = options.columns;
  int target_rows = options.rows;

  try {
    if (options.terminal_output) {
      asciiomium::terminal::TerminalSessionOptions terminal_options;
      terminal_options.use_alternate_screen = true;
      terminal_options.hide_cursor = true;
      terminal_options.enable_vt_input = true;
      terminal_options.raw_keyboard_input = true;
      terminal_options.mouse_tracking =
          asciiomium::terminal::MouseTrackingMode::AnyEvent;
      terminal_session =
          std::make_unique<asciiomium::terminal::TerminalSession>(terminal_options);
      target_columns = terminal_session->geometry().columns;
      target_rows = terminal_session->geometry().rows;
    }

    auto initial_geometry =
        viewport_state.Publish(MakeViewportTransform(
            options, target_columns, target_rows));
    auto viewport_snapshot = initial_geometry.snapshot;
    RecordGeometry(viewport_snapshot, &evidence);
    if (options.geometry_debug) {
      // CEF was created with these dimensions already, so initial publication
      // is not itself a WasResized request.
      LogGeometry(viewport_snapshot, false);
    }

    const auto started = Clock::now();
    auto last_geometry_poll = started;

    auto refresh_terminal_geometry = [&]() {
      if (!terminal_session || !terminal_session->RefreshGeometry()) {
        return;
      }
      target_columns = terminal_session->geometry().columns;
      target_rows = terminal_session->geometry().rows;
      const auto next_transform =
          asciiomium::geometry::ViewportTransform::TryCreate(
              asciiomium::geometry::ViewportConfig{
                  target_columns, target_rows, 0,
                  options.width, options.height,
                  options.cell_aspect, options.fit_mode});
      if (!next_transform.has_value()) {
        if (options.geometry_debug) {
          std::cerr << "geometry rejected terminal=" << target_columns
                    << 'x' << target_rows
                    << " (invalid or unsafe dimensions)\n";
        }
        return;
      }

      const auto change = viewport_state.Publish(*next_transform);
      if (!change.changed) {
        return;
      }

      bool cef_resize_requested = false;
      if (change.browser_changed) {
        cef_resize_requested =
            client->Resize(change.snapshot.transform->browser_width(),
                           change.snapshot.transform->browser_height());
        if (cef_resize_requested) {
          ++evidence.cef_resize_requests;
        }
      }
      viewport_snapshot = change.snapshot;
      RecordGeometry(viewport_snapshot, &evidence);
      scheduler.RequestRefresh();
      if (options.geometry_debug) {
        LogGeometry(viewport_snapshot, cef_resize_requested);
      }
    };

    while (true) {
      ++evidence.loop_iterations;
      CefDoMessageLoopWork();
      const auto now = Clock::now();

      if (terminal_session && terminal_session->stop_requested()) {
        evidence.exit_reason = "console-control";
        break;
      }

      if (terminal_session && client->browser_created()) {
        const std::string input_bytes = terminal_session->ReadInputAvailable();
        if (!input_bytes.empty()) {
          // Mouse bytes can arrive immediately after a terminal resize. Publish
          // the latest #10 transform before decoding/mapping this input batch.
          refresh_terminal_geometry();
          viewport_snapshot = viewport_state.Snapshot();
          last_input_byte_at = now;
          ForwardInput(input_parser.Feed(input_bytes), client,
                       viewport_snapshot, options, &evidence,
                       &quit_requested);
        } else if (input_parser.has_pending() &&
                   now - last_input_byte_at >=
                       std::chrono::milliseconds(35)) {
          viewport_snapshot = viewport_state.Snapshot();
          ForwardInput(input_parser.FlushPending(), client,
                       viewport_snapshot, options, &evidence,
                       &quit_requested);
        }
      }
      if (quit_requested) {
        evidence.exit_reason = "quit-chord";
        break;
      }
      if (options.duration_ms > 0 &&
          now - started >= std::chrono::milliseconds(options.duration_ms)) {
        evidence.exit_reason = "duration";
        break;
      }
      if (options.max_frames > 0 &&
          scheduler.stats().emitted_frames >=
              static_cast<std::uint64_t>(options.max_frames)) {
        evidence.exit_reason = "max-frames";
        break;
      }
      if (client->load_error_code() != 0) {
        evidence.exit_reason = "load-error";
        result = 5;
        break;
      }

      if (terminal_session &&
          now - last_geometry_poll >= std::chrono::milliseconds(40)) {
        last_geometry_poll = now;
        refresh_terminal_geometry();
      }

      const auto state = frame_store->SnapshotState();

      if (!input_script_focus_requested && !options.input_script.empty() &&
          client->browser_created() && client->load_complete() &&
          state.view_generation > 0) {
        client->SetFocus(true);
        input_script_focus_requested =
            client->ExecuteMainFrameJavaScript(
                "document.querySelector('[autofocus]')?.focus()");
      }
      if (input_script_focus_requested && !evidence.input_focus_observed) {
        evidence.input_focus_observed =
            client->MainTitle() == "keyboard-focus-ready";
      }

      const bool keyboard_ready =
          !options.input_script.empty() && evidence.input_focus_observed;
      const bool pointer_ready =
          !options.input_hex.empty() && client->browser_created() &&
          client->load_complete() && state.view_generation > 0;
      if (!input_script_injected && (keyboard_ready || pointer_ready)) {
        input_script_injected = true;
        evidence.input_script_sent = keyboard_ready;
        evidence.input_hex_sent = pointer_ready;
        evidence.input_generation_before = state.presentation_generation;
        viewport_snapshot = viewport_state.Snapshot();
        const std::string_view bytes =
            keyboard_ready ? std::string_view(options.input_script)
                           : std::string_view(raw_injected_input);
        ForwardInput(input_parser.Feed(bytes), client, viewport_snapshot,
                     options, &evidence, &quit_requested);
        ForwardInput(input_parser.FlushPending(), client, viewport_snapshot,
                     options, &evidence, &quit_requested);
      }

      if (input_script_injected) {
        const std::string main_title = client->MainTitle();
        if (evidence.input_script_sent && !evidence.input_dom_observed) {
          evidence.input_dom_observed =
              main_title == "keyboard-input-" + options.input_script;
        }
        if (evidence.input_hex_sent) {
          evidence.pointer_dom_observed =
              evidence.pointer_dom_observed ||
              main_title.find("pointer-click-center") != std::string::npos;
          evidence.pointer_scroll_observed =
              evidence.pointer_scroll_observed ||
              main_title.find("pointer-scroll") != std::string::npos;
        }
      }
      if (quit_requested) {
        evidence.exit_reason = "quit-chord";
        break;
      }

      scheduler.ObserveSource(state.presentation_generation);
      if (!scheduler.Ready(now)) {
        ++evidence.idle_iterations;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      const auto presentation = frame_store->SnapshotPresentation();
      scheduler.ObserveSource(presentation.presentation_generation);
      if (!presentation.view.valid()) {
        ++evidence.idle_iterations;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      viewport_snapshot = viewport_state.Snapshot();
      if (!viewport_snapshot.valid()) {
        ++evidence.idle_iterations;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      const auto conversion_started = Clock::now();
      const auto composed = asciiomium::browser::ComposeViewAndPopup(
          presentation.view, presentation.popup);
      if (composed.width != viewport_snapshot.transform->browser_width() ||
          composed.height != viewport_snapshot.transform->browser_height()) {
        // A browser viewport change is not presentable until CEF supplies a
        // fresh OnPaint at the authoritative dimensions.
        ++evidence.idle_iterations;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      asciiomium::render::ImageView source_view{
          composed.bgra.data(), composed.width, composed.height,
          static_cast<std::size_t>(composed.width) * 4u,
          asciiomium::render::PixelFormat::BGRA8};
      auto terminal_frame = asciiomium::render::RenderHalfBlock(
          source_view, *viewport_snapshot.transform, render_config);
      const auto conversion_finished = Clock::now();

      const auto serialization_started = conversion_finished;
      auto emission =
          asciiomium::terminal::SerializeFullFrame(terminal_frame);
      const auto serialization_finished = Clock::now();

      const auto write_started = serialization_finished;
      if (terminal_session) {
        terminal_session->Write(emission.bytes);
      }
      const auto write_finished = Clock::now();

      evidence.conversion_timing.AddMilliseconds(
          Milliseconds(conversion_started, conversion_finished));
      evidence.serialization_timing.AddMilliseconds(
          Milliseconds(serialization_started, serialization_finished));
      evidence.write_timing.AddMilliseconds(
          Milliseconds(write_started, write_finished));
      if (composed.popup_composited) {
        ++evidence.popup_composited_frames;
      }

      scheduler.MarkRendered(write_finished,
                             presentation.presentation_generation);
      scheduler.MarkEmitted();

      last_vt_payload = std::move(emission.bytes);
      evidence.last_vt_bytes = last_vt_payload.size();
      last_terminal_frame = std::move(terminal_frame);
      have_terminal_frame = true;
    }

    evidence.duration_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
  } catch (const std::exception& error) {
    std::cerr << "Live render loop failed: " << error.what() << '\n';
    evidence.exit_reason = "exception";
    result = 6;
  }

  evidence.load_complete = client->load_complete();
  evidence.http_status = client->http_status_code();
  evidence.load_error = client->load_error_code();
  evidence.load_error_text = client->load_error_text();
  evidence.failed_url = client->failed_url();

  client->CloseBrowser();
  const auto close_deadline = Clock::now() + std::chrono::seconds(5);
  while (!client->closed() && Clock::now() < close_deadline) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  evidence.closed_cleanly = client->closed();
  if (!evidence.closed_cleanly && result == 0) {
    std::cerr << "CEF browser did not close before shutdown deadline\n";
    result = 7;
  }

  if (terminal_session) {
    terminal_session->Restore();
    evidence.terminal_restored = true;
    terminal_session.reset();
  } else {
    evidence.terminal_restored = true;
  }

  evidence.source_state = frame_store->SnapshotState();
  evidence.input_generation_after =
      evidence.source_state.presentation_generation;
  evidence.input_paint_observed =
      (evidence.input_script_sent || evidence.input_hex_sent) &&
      evidence.input_generation_after > evidence.input_generation_before;
  evidence.scheduler = scheduler.stats();

  client = nullptr;
  CefShutdown();

  if (!have_terminal_frame && result == 0) {
    std::cerr << "No terminal frame was produced\n";
    result = 8;
  }

  try {
    if (!options.evidence_svg.empty() && have_terminal_frame) {
      WriteTerminalFrameSvg(options.evidence_svg, last_terminal_frame);
    }
    if (!options.vt_output.empty() && have_terminal_frame) {
      WriteBinaryFile(options.vt_output, last_vt_payload);
    }
    WriteReport(options.report, options, evidence);
  } catch (const std::exception& error) {
    std::cerr << "Failed to write live evidence: " << error.what() << '\n';
    if (result == 0) result = 9;
  }

  if (!options.quiet) {
    std::cout << "ASCIIomium live: paints=" << evidence.source_state.view_paint_count
              << " rendered=" << evidence.scheduler.rendered_frames
              << " emitted=" << evidence.scheduler.emitted_frames
              << " coalesced="
              << evidence.scheduler.coalesced_source_generations
              << " conversion_avg_ms=" << std::fixed << std::setprecision(3)
              << evidence.conversion_timing.average_ms()
              << " conversion_p95_ms="
              << evidence.conversion_timing.percentile_ms(0.95)
              << " geometry_gen=" << evidence.geometry_generation
              << " exit=" << evidence.exit_reason << '\n';
  }

  return result;
}

}  // namespace

int main(int argc, char** argv) {
  CefMainArgs main_args(GetModuleHandle(nullptr));

  // This executable also services Chromium renderer/GPU/utility subprocesses.
  // Subprocesses must exit before terminal ownership or CLI validation occurs.
  const int subprocess_exit = CefExecuteProcess(main_args, nullptr, nullptr);
  if (subprocess_exit >= 0) {
    return subprocess_exit;
  }

  Options options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage();
    return 2;
  }

  try {
    return BrowserProcessMain(options, main_args);
  } catch (const std::exception& error) {
    std::cerr << "ASCIIomium live failed: " << error.what() << '\n';
    return 1;
  }
}
