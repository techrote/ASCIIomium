#include "app/build_info.h"
#include "asciiomium_build_identity.h"

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
  info.source_revision = ASCIIOMIUM_SOURCE_REVISION;
  info.source_tree = ASCIIOMIUM_SOURCE_TREE;
  info.source_identity_available = ASCIIOMIUM_SOURCE_IDENTITY_AVAILABLE != 0;
  info.source_dirty = ASCIIOMIUM_SOURCE_DIRTY != 0;
  info.compiler = ASCIIOMIUM_COMPILER;
  info.fixture_manifest_sha256 = ASCIIOMIUM_FIXTURE_MANIFEST_SHA256;
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
         << "Source revision: " << info.source_revision << '\n'
         << "Source tree: " << info.source_tree << '\n'
         << "Source identity: " << (info.source_identity_available ? "available" : "unavailable") << '\n'
         << "Source checkout dirty: " << (info.source_dirty ? "yes" : "no") << '\n'
         << "Compiler: " << info.compiler << '\n'
         << "Fixture manifest SHA-256: " << info.fixture_manifest_sha256 << '\n'
         << "CEF ABI/header match: "
         << (info.cef_runtime_matches_headers ? "yes" : "NO");
  return stream.str();
}

}  // namespace asciiomium
