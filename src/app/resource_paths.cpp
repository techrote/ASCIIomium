#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "app/resource_paths.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace asciiomium {
std::filesystem::path ExecutableDirectory() {
  std::vector<wchar_t> buffer(32768);
  const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (count == 0 || count >= buffer.size()) throw std::runtime_error("cannot locate executable resources");
  return std::filesystem::path(std::wstring(buffer.data(), count)).parent_path();
}
std::filesystem::path BundledFixture(std::string_view filename) {
  if (filename.empty() || filename.find_first_of("/\\:") != std::string_view::npos || filename == "." || filename == "..")
    throw std::invalid_argument("invalid bundled fixture name");
  const auto result = ExecutableDirectory() / "fixtures" / "web" / std::string(filename);
  if (!std::filesystem::is_regular_file(result))
    throw std::runtime_error("bundled fixture missing beside executable; restore the complete distribution");
  return result;
}

std::filesystem::path DevelopmentSourceDirectory() {
  // Protect development checkouts without embedding a dependency on a build
  // machine's drive/path into portable browser startup.
  auto path = ExecutableDirectory();
  while (!path.empty()) {
    std::error_code error;
    if (std::filesystem::is_regular_file(path / "src/app/live_main.cpp", error) &&
        std::filesystem::is_regular_file(path / "CMakeLists.txt", error)) return path;
    const auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
  return {};
}
}  // namespace asciiomium
