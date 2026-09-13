#pragma once

#include <string>

namespace asciiomium {

struct BuildInfo {
  std::string asciiomium_version;
  std::string cef_compile_version;
  std::string cef_runtime_version;
  std::string chromium_runtime_version;
  std::string build_type;
  std::string architecture;
  bool cef_runtime_matches_headers = false;
};

BuildInfo GetBuildInfo();
std::string FormatBuildInfo(const BuildInfo& info);

}  // namespace asciiomium
