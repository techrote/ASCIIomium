#include <iostream>
#include <string>

#include "app/build_info.h"

namespace {

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto info = asciiomium::GetBuildInfo();

  if (info.asciiomium_version != "0.1.0") {
    return Fail("unexpected ASCIIomium version: " + info.asciiomium_version);
  }
  if (info.cef_compile_version !=
      "151.3.17+gf059e67+chromium-151.0.7922.138") {
    return Fail("unexpected pinned CEF headers: " + info.cef_compile_version);
  }
  if (info.cef_runtime_version != "151.3.17") {
    return Fail("unexpected CEF runtime: " + info.cef_runtime_version);
  }
  if (info.chromium_runtime_version != "151.0.7922.138") {
    return Fail("unexpected Chromium runtime: " + info.chromium_runtime_version);
  }
  if (info.architecture != "x64") {
    return Fail("unexpected architecture: " + info.architecture);
  }
  if (info.build_type != "Debug" && info.build_type != "Release") {
    return Fail("unexpected build type: " + info.build_type);
  }
  if (!info.cef_runtime_matches_headers) {
    return Fail("linked libcef.dll does not match the headers used at compile time");
  }

  std::cout << "PASS\n" << asciiomium::FormatBuildInfo(info) << '\n';
  return 0;
}
