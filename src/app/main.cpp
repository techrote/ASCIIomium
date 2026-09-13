#include <iostream>
#include <string_view>

#include "app/build_info.h"

namespace {

void PrintHelp() {
  std::cout
      << "ASCIIomium bootstrap executable\n"
      << "\n"
      << "Usage:\n"
      << "  asciiomium --version   Show ASCIIomium, CEF, Chromium and build versions\n"
      << "  asciiomium --help      Show this help\n"
      << "\n"
      << "The browser/runtime loop is intentionally not started by issue #2.\n";
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

  if (argc != 1) {
    std::cerr << "Unknown arguments. Use --help.\n";
    return 1;
  }

  std::cout
      << "ASCIIomium bootstrap is installed. Browser initialisation is deferred "
         "to issue #8.\n"
      << "Run 'asciiomium --version' to verify the pinned CEF linkage.\n";
  return 0;
}
