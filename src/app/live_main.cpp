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

#include "browser/osr_client.h"
#include "browser/popup_compositor.h"
#include "browser/source_frame.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_version.h"
#include "include/cef_version_info.h"
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
  bool freeze = false;
  bool terminal_output = true;
  bool quiet = false;
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
  std::size_t last_vt_bytes = 0;
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
      << "  --freeze                append ?freeze=1 to bundled fixture URL\n"
      << "  --duration-ms N         stop after N ms; 0 means until Ctrl+C (default 0)\n"
      << "  --max-frames N          stop after N emitted frames; 0 means unlimited\n"
      << "  --no-terminal           serialize frames without acquiring a console (CI/benchmark)\n"
      << "  --columns N --rows N    logical target for --no-terminal (default 120x40)\n"
      << "  --report FILE.json      write live-run metrics\n"
      << "  --evidence-svg FILE     write the final logical half-block frame as SVG\n"
      << "  --vt-output FILE        write the final full-frame VT payload verbatim\n"
      << "  --quiet                 suppress final one-line summary\n"
      << "\nCtrl+C exits terminal mode and restores the console.\n";
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
    } else if (arg == "--report") {
      if (!take_path(&options->report)) return false;
    } else if (arg == "--evidence-svg") {
      if (!take_path(&options->evidence_svg)) return false;
    } else if (arg == "--vt-output") {
      if (!take_path(&options->vt_output)) return false;
    } else if (arg == "--freeze") {
      options->freeze = true;
    } else if (arg == "--no-terminal") {
      options->terminal_output = false;
    } else if (arg == "--quiet") {
      options->quiet = true;
    } else {
      return false;
    }
  }

  // Headless/CI runs need a deterministic stop condition. Interactive terminal
  // mode may intentionally run forever until the terminal control handler sees
  // Ctrl+C.
  if (!options->terminal_output && options->duration_ms == 0 &&
      options->max_frames == 0) {
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
      << ",\"terminal_rows\":" << evidence.terminal_rows << "},\n"
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
  render_config.quantizer = &quantizer;

  int target_columns = options.columns;
  int target_rows = options.rows;

  try {
    if (options.terminal_output) {
      asciiomium::terminal::TerminalSessionOptions terminal_options;
      terminal_options.use_alternate_screen = true;
      terminal_options.hide_cursor = true;
      terminal_options.enable_vt_input = false;
      terminal_session =
          std::make_unique<asciiomium::terminal::TerminalSession>(terminal_options);
      target_columns = terminal_session->geometry().columns;
      target_rows = terminal_session->geometry().rows;
    }

    evidence.terminal_columns = target_columns;
    evidence.terminal_rows = target_rows;

    const auto started = Clock::now();
    auto last_geometry_poll = started;

    while (true) {
      ++evidence.loop_iterations;
      CefDoMessageLoopWork();
      const auto now = Clock::now();

      if (terminal_session && terminal_session->stop_requested()) {
        evidence.exit_reason = "ctrl-c";
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
        if (terminal_session->RefreshGeometry()) {
          target_columns = terminal_session->geometry().columns;
          target_rows = terminal_session->geometry().rows;
          evidence.terminal_columns = target_columns;
          evidence.terminal_rows = target_rows;
          scheduler.RequestRefresh();
        }
      }

      const auto state = frame_store->SnapshotState();
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

      const auto conversion_started = Clock::now();
      const auto composed = asciiomium::browser::ComposeViewAndPopup(
          presentation.view, presentation.popup);
      asciiomium::render::ImageView source_view{
          composed.bgra.data(), composed.width, composed.height,
          static_cast<std::size_t>(composed.width) * 4u,
          asciiomium::render::PixelFormat::BGRA8};
      auto terminal_frame = asciiomium::render::RenderHalfBlock(
          source_view,
          asciiomium::render::RenderTarget{target_columns, target_rows},
          render_config);
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
