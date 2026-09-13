#include <charconv>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

#include "app/build_info.h"
#include "terminal/terminal_diagnostics.h"
#include "terminal/terminal_session.h"

namespace {

struct CommandLineOptions {
  bool terminal_diagnostics = false;
  bool no_alt_screen = false;
  int duration_ms = 0;
};

void PrintHelp() {
  std::cout
      << "ASCIIomium bootstrap executable\n"
      << "\n"
      << "Usage:\n"
      << "  asciiomium --version\n"
      << "      Show ASCIIomium, CEF, Chromium and build versions.\n"
      << "\n"
      << "  asciiomium --terminal-diagnostics [--duration-ms N]\n"
      << "      Exercise VT output, UTF-8, geometry tracking, alternate-screen\n"
      << "      ownership and Ctrl+C restoration. Runs until Ctrl+C when no\n"
      << "      duration is supplied.\n"
      << "\n"
      << "  asciiomium --terminal-diagnostics --no-alt-screen\n"
      << "      Print one non-destructive diagnostic snapshot without entering\n"
      << "      the alternate screen.\n"
      << "\n"
      << "  asciiomium --help\n"
      << "      Show this help.\n"
      << "\n"
      << "CEF browser initialisation remains deferred to issue #8.\n";
}

bool ParsePositiveInt(std::string_view text, int* value) {
  if (text.empty()) {
    return false;
  }
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

bool ParseCommandLine(int argc, char** argv, CommandLineOptions* options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--terminal-diagnostics") {
      options->terminal_diagnostics = true;
      continue;
    }
    if (argument == "--no-alt-screen") {
      options->no_alt_screen = true;
      continue;
    }
    if (argument == "--duration-ms") {
      if (index + 1 >= argc ||
          !ParsePositiveInt(argv[++index], &options->duration_ms)) {
        return false;
      }
      continue;
    }
    return false;
  }

  if (options->no_alt_screen && !options->terminal_diagnostics) {
    return false;
  }
  if (options->duration_ms > 0 && !options->terminal_diagnostics) {
    return false;
  }
  return true;
}

int RunTerminalDiagnostics(const CommandLineOptions& options) {
  try {
    asciiomium::terminal::TerminalSessionOptions session_options;
    session_options.use_alternate_screen = !options.no_alt_screen;
    session_options.hide_cursor = !options.no_alt_screen;
    session_options.enable_vt_input = true;

    asciiomium::terminal::TerminalSession session(session_options);

    if (options.no_alt_screen) {
      session.Write(
          asciiomium::terminal::BuildDiagnosticSnapshot(session.geometry()));
      return 0;
    }

    const auto started = std::chrono::steady_clock::now();
    std::uint64_t frame_counter = 0;

    while (!session.stop_requested()) {
      (void)session.RefreshGeometry();
      session.Write(asciiomium::terminal::BuildDiagnosticFrame(
          session.geometry(), frame_counter++));

      if (options.duration_ms > 0 &&
          std::chrono::steady_clock::now() - started >=
              std::chrono::milliseconds(options.duration_ms)) {
        break;
      }

      // A short timed wait keeps resize detection responsive without a hot
      // polling loop. The future input subsystem will own stdin event parsing.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Terminal diagnostics failed: " << error.what() << '\n';
    return 3;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view argument(argv[1]);
    if (argument == "--version" || argument == "-V") {
      const auto info = asciiomium::GetBuildInfo();
      std::cout << asciiomium::FormatBuildInfo(info) << '\n';
      return info.cef_runtime_matches_headers ? 0 : 2;
    }
    if (argument == "--help" || argument == "-h") {
      PrintHelp();
      return 0;
    }
  }

  CommandLineOptions options;
  if (argc > 1) {
    if (!ParseCommandLine(argc, argv, &options)) {
      std::cerr << "Unknown or invalid arguments. Use --help.\n";
      return 1;
    }
    if (options.terminal_diagnostics) {
      return RunTerminalDiagnostics(options);
    }
  }

  std::cout
      << "ASCIIomium terminal bootstrap is installed. Browser initialisation "
         "is deferred to issue #8.\n"
      << "Run 'asciiomium --terminal-diagnostics' inside Windows Terminal to "
         "exercise terminal ownership.\n";
  return 0;
}
