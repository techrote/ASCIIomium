#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "browser/osr_client.h"
#include "browser/source_frame.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_version.h"
#include "include/cef_version_info.h"

namespace {

struct Options {
  std::string fixture = "colour-ramps";
  std::string url;
  int width = 640;
  int height = 360;
  int resize_width = 0;
  int resize_height = 0;
  int min_paints = 1;
  int timeout_ms = 12000;
  int frame_rate = 30;
  bool freeze = false;
  std::filesystem::path output = "cef-capture.bmp";
  std::filesystem::path report = "cef-capture.json";
};

bool ParsePositiveInt(std::string_view text, int* value) {
  int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

void PrintUsage() {
  std::cout
      << "Usage: asciiomium_cef_capture [options]\n"
      << "  --fixture NAME          index|flat-ui|colour-ramps|raster-imagery|motion-scroll|input-focus\n"
      << "  --url URL               load an explicit URL instead of a bundled fixture\n"
      << "  --width N --height N    initial OSR view size (default 640x360)\n"
      << "  --resize-width N --resize-height N\n"
      << "                         request one resize after the first view paint\n"
      << "  --min-paints N          require at least N PET_VIEW paints (default 1)\n"
      << "  --frame-rate N          CEF windowless frame-rate cap (default 30)\n"
      << "  --timeout-ms N          capture timeout (default 12000)\n"
      << "  --freeze                append ?freeze=1 to bundled fixture URL\n"
      << "  --output FILE.bmp       source-frame diagnostic image\n"
      << "  --report FILE.json      machine-readable capture report\n";
}

bool ParseArgs(int argc, char** argv, Options* options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    }
    auto take_string = [&](std::string* destination) -> bool {
      if (i + 1 >= argc) {
        return false;
      }
      *destination = argv[++i];
      return true;
    };
    auto take_path = [&](std::filesystem::path* destination) -> bool {
      if (i + 1 >= argc) {
        return false;
      }
      *destination = std::filesystem::path(argv[++i]);
      return true;
    };
    auto take_int = [&](int* destination) -> bool {
      if (i + 1 >= argc || !ParsePositiveInt(argv[i + 1], destination)) {
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
      if (!take_int(&options->width)) return false;
    } else if (arg == "--height") {
      if (!take_int(&options->height)) return false;
    } else if (arg == "--resize-width") {
      if (!take_int(&options->resize_width)) return false;
    } else if (arg == "--resize-height") {
      if (!take_int(&options->resize_height)) return false;
    } else if (arg == "--min-paints") {
      if (!take_int(&options->min_paints)) return false;
    } else if (arg == "--frame-rate") {
      if (!take_int(&options->frame_rate)) return false;
    } else if (arg == "--timeout-ms") {
      if (!take_int(&options->timeout_ms)) return false;
    } else if (arg == "--output") {
      if (!take_path(&options->output)) return false;
    } else if (arg == "--report") {
      if (!take_path(&options->report)) return false;
    } else if (arg == "--freeze") {
      options->freeze = true;
    } else {
      return false;
    }
  }

  const bool any_resize = options->resize_width > 0 || options->resize_height > 0;
  const bool complete_resize =
      options->resize_width > 0 && options->resize_height > 0;
  return !any_resize || complete_resize;
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
  encoded << "file:///";
  encoded << std::uppercase << std::hex;
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
  std::filesystem::path path =
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
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
}

void WriteBmp32(const std::filesystem::path& path,
                const asciiomium::browser::SourceFrameSnapshot& frame) {
  if (!frame.valid()) {
    throw std::runtime_error("cannot write invalid source frame");
  }
  EnsureParentDirectory(path);

  BITMAPFILEHEADER file_header{};
  BITMAPINFOHEADER info_header{};
  info_header.biSize = sizeof(BITMAPINFOHEADER);
  info_header.biWidth = frame.width;
  info_header.biHeight = -frame.height;  // top-down, matching CEF's origin.
  info_header.biPlanes = 1;
  info_header.biBitCount = 32;
  info_header.biCompression = BI_RGB;
  info_header.biSizeImage = static_cast<DWORD>(frame.bgra.size());

  file_header.bfType = 0x4D42;
  file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  file_header.bfSize = file_header.bfOffBits + info_header.biSizeImage;

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("unable to open BMP output");
  }
  output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
  output.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));
  output.write(reinterpret_cast<const char*>(frame.bgra.data()),
               static_cast<std::streamsize>(frame.bgra.size()));
  if (!output) {
    throw std::runtime_error("failed while writing BMP output");
  }
}

struct CaptureEvidence {
  bool load_complete = false;
  bool closed_cleanly = false;
  bool resize_requested = false;
  bool resize_observed = false;
  int http_status = 0;
  int load_error = 0;
  std::string load_error_text;
  std::string failed_url;
  std::string url;
  asciiomium::browser::SourceFrameSnapshot view;
  asciiomium::browser::PopupFrameSnapshot popup;
  std::size_t view_capacity = 0;
  std::size_t popup_capacity = 0;
};

void WriteReport(const std::filesystem::path& path,
                 const Options& options,
                 const CaptureEvidence& evidence) {
  EnsureParentDirectory(path);
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("unable to open capture report output");
  }

  output << "{\n"
         << "  \"schema\":\"asciiomium-cef-capture-v1\",\n"
         << "  \"cef_compile\":\"" << JsonEscape(CEF_VERSION) << "\",\n"
         << "  \"cef_runtime\":\"" << CefRuntimeVersion() << "\",\n"
         << "  \"chromium_runtime\":\"" << ChromiumRuntimeVersion() << "\",\n"
         << "  \"url\":\"" << JsonEscape(evidence.url) << "\",\n"
         << "  \"sandbox\":\"disabled-temporary-direct-executable-bootstrap\",\n"
         << "  \"pixel_contract\":\"BGRA8, tightly packed width*height*4, upper-left origin\",\n"
         << "  \"device_scale_factor\":1.0,\n"
         << "  \"load\":{\"complete\":"
         << (evidence.load_complete ? "true" : "false")
         << ",\"http_status\":" << evidence.http_status
         << ",\"error_code\":" << evidence.load_error
         << ",\"error_text\":\"" << JsonEscape(evidence.load_error_text)
         << "\",\"failed_url\":\"" << JsonEscape(evidence.failed_url)
         << "\"},\n"
         << "  \"view\":{\"width\":" << evidence.view.width
         << ",\"height\":" << evidence.view.height
         << ",\"generation\":" << evidence.view.generation
         << ",\"paint_count\":" << evidence.view.paint_count
         << ",\"dirty_rect_count\":" << evidence.view.dirty_rects.size()
         << ",\"alpha_min\":" << static_cast<unsigned>(evidence.view.alpha_min)
         << ",\"alpha_max\":" << static_cast<unsigned>(evidence.view.alpha_max)
         << ",\"storage_capacity_bytes\":" << evidence.view_capacity << "},\n"
         << "  \"popup\":{\"visible\":"
         << (evidence.popup.visible ? "true" : "false")
         << ",\"x\":" << evidence.popup.bounds.x
         << ",\"y\":" << evidence.popup.bounds.y
         << ",\"width\":" << evidence.popup.width
         << ",\"height\":" << evidence.popup.height
         << ",\"generation\":" << evidence.popup.generation
         << ",\"paint_count\":" << evidence.popup.paint_count
         << ",\"storage_capacity_bytes\":" << evidence.popup_capacity << "},\n"
         << "  \"resize\":{\"requested\":"
         << (evidence.resize_requested ? "true" : "false")
         << ",\"observed\":" << (evidence.resize_observed ? "true" : "false")
         << ",\"target_width\":" << options.resize_width
         << ",\"target_height\":" << options.resize_height << "},\n"
         << "  \"bounded_frame_model\":\"one newest view buffer plus one newest popup buffer; no frame queue\",\n"
         << "  \"closed_cleanly\":"
         << (evidence.closed_cleanly ? "true" : "false") << "\n"
         << "}\n";
}

bool PumpUntil(CefRefPtr<asciiomium::browser::OsrClient> client,
               std::chrono::steady_clock::time_point deadline,
               const std::function<bool()>& predicate) {
  while (std::chrono::steady_clock::now() < deadline) {
    CefDoMessageLoopWork();
    if (predicate()) {
      return true;
    }
    if (client->load_error_code() != 0) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

int BrowserProcessMain(const Options& options, const CefMainArgs& main_args) {
  CefSettings settings;
  settings.windowless_rendering_enabled = true;
  settings.multi_threaded_message_loop = false;
  settings.external_message_pump = false;

  // Issue #2 intentionally established a direct libcef executable rather than
  // CEF 151's optional Windows bootstrap/client-DLL architecture. The official
  // CEF CMake integration therefore builds this diagnostic with USE_SANDBOX=OFF
  // and the matching runtime setting is required here. This is a documented
  // milestone exception, not the intended long-term security posture.
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_WARNING;

  if (!CefInitialize(main_args, settings, nullptr, nullptr)) {
    std::cerr << "CefInitialize failed with exit code " << CefGetExitCode() << '\n';
    return 3;
  }

  int result = 0;
  CaptureEvidence evidence;
  evidence.url = ResolveUrl(options);
  evidence.resize_requested = options.resize_width > 0;

  auto frame_store = std::make_shared<asciiomium::browser::SourceFrameStore>();
  CefRefPtr<asciiomium::browser::OsrClient> client(
      new asciiomium::browser::OsrClient(frame_store, options.width,
                                         options.height));

  CefWindowInfo window_info;
  window_info.SetAsWindowless(nullptr);

  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = options.frame_rate;
  // Opaque background makes alpha semantics explicit for the baseline. Pages
  // may still paint transparent subcontent, but the final view is expected to
  // be opaque and the captured alpha range is reported for verification.
  browser_settings.background_color = CefColorSetARGB(255, 16, 20, 26);

  if (!CefBrowserHost::CreateBrowser(window_info, client, evidence.url,
                                     browser_settings, nullptr, nullptr)) {
    std::cerr << "CefBrowserHost::CreateBrowser failed\n";
    client = nullptr;
    CefShutdown();
    return 4;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.timeout_ms);
  bool resized = false;
  std::uint64_t resize_generation = 0;
  const int target_width =
      evidence.resize_requested ? options.resize_width : options.width;
  const int target_height =
      evidence.resize_requested ? options.resize_height : options.height;

  const bool captured = PumpUntil(
      client, deadline,
      [&]() {
        const auto frame = frame_store->SnapshotView();
        if (frame.valid() && evidence.resize_requested && !resized) {
          resize_generation = frame.generation;
          client->Resize(options.resize_width, options.resize_height);
          resized = true;
          return false;
        }

        const bool correct_size = frame.valid() &&
                                  frame.width == target_width &&
                                  frame.height == target_height;
        const bool resize_fresh =
            !evidence.resize_requested || frame.generation > resize_generation;
        return correct_size && resize_fresh &&
               frame.paint_count >= static_cast<std::uint64_t>(options.min_paints) &&
               client->load_complete();
      });

  evidence.load_complete = client->load_complete();
  evidence.http_status = client->http_status_code();
  evidence.load_error = client->load_error_code();
  evidence.load_error_text = client->load_error_text();
  evidence.failed_url = client->failed_url();
  evidence.view = frame_store->SnapshotView();
  evidence.popup = frame_store->SnapshotPopup();
  evidence.view_capacity = frame_store->ViewStorageCapacityBytes();
  evidence.popup_capacity = frame_store->PopupStorageCapacityBytes();
  evidence.resize_observed =
      !evidence.resize_requested ||
      (evidence.view.valid() && evidence.view.width == target_width &&
       evidence.view.height == target_height &&
       evidence.view.generation > resize_generation);

  if (!captured || evidence.load_error != 0 || !evidence.view.valid()) {
    std::cerr << "CEF capture did not reach the requested state"
              << " load_error=" << evidence.load_error
              << " paints=" << evidence.view.paint_count
              << " generation=" << evidence.view.generation
              << " size=" << evidence.view.width << 'x' << evidence.view.height
              << '\n';
    result = 5;
  } else {
    try {
      WriteBmp32(options.output, evidence.view);
    } catch (const std::exception& error) {
      std::cerr << "Failed to write capture image: " << error.what() << '\n';
      result = 6;
    }
  }

  client->CloseBrowser();
  const auto close_deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
  while (!client->closed() && std::chrono::steady_clock::now() < close_deadline) {
    CefDoMessageLoopWork();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  evidence.closed_cleanly = client->closed();
  if (!evidence.closed_cleanly && result == 0) {
    std::cerr << "CEF browser did not close before shutdown deadline\n";
    result = 7;
  }

  client = nullptr;
  CefShutdown();

  try {
    WriteReport(options.report, options, evidence);
  } catch (const std::exception& error) {
    std::cerr << "Failed to write capture report: " << error.what() << '\n';
    if (result == 0) {
      result = 8;
    }
  }

  if (result == 0) {
    std::cout << "CEF OSR capture passed: " << evidence.view.width << 'x'
              << evidence.view.height << " BGRA, paints="
              << evidence.view.paint_count << ", generation="
              << evidence.view.generation << ", alpha="
              << static_cast<unsigned>(evidence.view.alpha_min) << ".."
              << static_cast<unsigned>(evidence.view.alpha_max)
              << ", popup_paints=" << evidence.popup.paint_count << '\n';
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  CefMainArgs main_args(GetModuleHandle(nullptr));

  // The same executable services Chromium renderer/GPU/utility subprocesses.
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
    std::cerr << "CEF capture failed: " << error.what() << '\n';
    return 1;
  }
}
