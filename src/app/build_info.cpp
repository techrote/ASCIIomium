#include "app/build_info.h"

#include <sstream>

#include "include/cef_version.h"
#include "include/cef_version_info.h"

namespace asciiomium {
namespace {

std::string JoinVersion(int major, int minor, int build, int patch) {
  std::ostringstream stream;
  stream << major << '.' << minor << '.' << build << '.' << patch;
  return stream.str();
}

std::string JoinCefVersion(int major, int minor, int patch) {
  std::ostringstream stream;
  stream << major << '.' << minor << '.' << patch;
  return stream.str();
}

bool RuntimeMatchesHeaders() {
  return cef_version_info(0) == CEF_VERSION_MAJOR &&
         cef_version_info(1) == CEF_VERSION_MINOR &&
         cef_version_info(2) == CEF_VERSION_PATCH &&
         cef_version_info(3) == CEF_COMMIT_NUMBER &&
         cef_version_info(4) == CHROME_VERSION_MAJOR &&
         cef_version_info(5) == CHROME_VERSION_MINOR &&
         cef_version_info(6) == CHROME_VERSION_BUILD &&
         cef_version_info(7) == CHROME_VERSION_PATCH;
}

}  // namespace

BuildInfo GetBuildInfo() {
  BuildInfo info;
  info.asciiomium_version = ASCIIOMIUM_VERSION;
  info.cef_compile_version = CEF_VERSION;
  info.cef_runtime_version = JoinCefVersion(
      cef_version_info(0), cef_version_info(1), cef_version_info(2));
  info.chromium_runtime_version = JoinVersion(
      cef_version_info(4), cef_version_info(5), cef_version_info(6),
      cef_version_info(7));
  info.build_type = ASCIIOMIUM_BUILD_TYPE;
  info.architecture = ASCIIOMIUM_ARCH;
  info.cef_runtime_matches_headers = RuntimeMatchesHeaders();
  return info;
}

std::string FormatBuildInfo(const BuildInfo& info) {
  std::ostringstream stream;
  stream << "ASCIIomium " << info.asciiomium_version << '\n'
         << "CEF compile: " << info.cef_compile_version << '\n'
         << "CEF runtime: " << info.cef_runtime_version << '\n'
         << "Chromium runtime: " << info.chromium_runtime_version << '\n'
         << "Build: " << info.build_type << '\n'
         << "Architecture: " << info.architecture << '\n'
         << "CEF ABI/header match: "
         << (info.cef_runtime_matches_headers ? "yes" : "NO");
  return stream.str();
}

}  // namespace asciiomium
