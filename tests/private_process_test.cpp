#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "browser/private_process.h"
#include "browser/profile_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace asciiomium::browser;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

class EnvironmentOverride {
 public:
  EnvironmentOverride(const wchar_t* name, const std::wstring& value) : name_(name) {
    wchar_t previous[32768]{};
    const auto size = GetEnvironmentVariableW(name, previous, 32768);
    Require(size < 32768, "test environment value is too large");
    present_ = size != 0;
    previous_ = previous;
    Require(SetEnvironmentVariableW(name, value.c_str()) != FALSE, "cannot set test environment");
  }
  ~EnvironmentOverride() { SetEnvironmentVariableW(name_.c_str(), present_ ? previous_.c_str() : nullptr); }
 private:
  std::wstring name_, previous_;
  bool present_ = false;
};

std::string ReadReport(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

void RequireRemovedRoot(const std::string& body) {
  const std::string prefix = "\"owned_root\":\"";
  const auto key = body.find(prefix);
  Require(key != std::string::npos, "worker root witness is missing");
  const auto begin = key + prefix.size();
  const auto end = body.find('"', begin);
  Require(end != std::string::npos, "worker root witness is malformed");
  const std::u8string path(reinterpret_cast<const char8_t*>(body.data() + begin), end - begin);
  Require(!std::filesystem::exists(std::filesystem::path(path)), "mapped worker root actually removed after process exit");
}

int Worker(int argc, char** argv) {
  const auto report = PrivateWorkerRoot() / kPrivateWorkerReportFilename;
  bool fail = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--script" && i + 1 < argc) {
      Require(std::string(argv[++i]) == "--report", "literal --report argument value was changed");
      continue;
    }
    if (std::string(argv[i]) == "--report" && i + 1 < argc) { ++i; continue; }
    if (std::string(argv[i]) == "--fail-worker") fail = true;
  }
  ProfileSession profile({}, true);
  Require(profile.cleanup_deferred(), "worker root must belong to supervisor");
  // Deliberately retain a file + writable mapping until process exit, matching
  // Chromium's process-lifetime BrowserMetrics mapping. CefShutdown-style
  // local object destruction cannot make this file removable on Windows.
  HANDLE file = CreateFileW((profile.root() / "process-lifetime.pma").c_str(),
      GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  Require(file != INVALID_HANDLE_VALUE, "cannot create process lifetime mapping test");
  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 4096, nullptr);
  Require(mapping != nullptr, "cannot create mapping");
  void* memory = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 4096);
  Require(memory != nullptr, "cannot map file");
  static_cast<char*>(memory)[0] = 'x';
  Require(profile.Finish() && std::filesystem::exists(profile.root()), "worker cannot delete supervisor-owned root");
  std::ofstream output(report);
  const auto root_utf8 = profile.root().generic_u8string();
  output << "{\"schema\":\"asciiomium-private-process-test-v1\",\"profile_cleanup\":false,"
         << "\"profile_cleanup_deferred\":true,\"profile_cleanup_attempts\":0,\"profile_cleanup_error\":0,"
         << "\"owned_root\":\"" << std::string(reinterpret_cast<const char*>(root_utf8.data()), root_utf8.size()) << "\"}\n";
  output.close();
  Require(static_cast<bool>(output), "cannot write private test report");
  return fail ? 23 : 0;
}

int main(int argc, char** argv) {
  try {
    InitializePrivateWorker();
    if (!PrivateWorkerRoot().empty()) return Worker(argc, argv);
    ProfileSession evidence({}, true);
    for (bool fail : {false, true}) {
      const auto report = evidence.root() / (fail ? "failure.json" : "success.json");
      std::vector<std::wstring> args{L"--private", L"--script", L"--report", L"--report", report.wstring()};
      if (fail) args.push_back(L"--fail-worker");
      const int result = RunPrivateProcess(report, args);
      Require(result == (fail ? 23 : 0), "supervisor must preserve worker exit status");
      const auto body = ReadReport(report);
      Require(body.find("\"profile_cleanup\":true") != std::string::npos, "supervisor must verify profile removal");
      Require(body.find("\"profile_cleanup_deferred\":false") != std::string::npos, "final report cannot retain pending cleanup");
      Require(body.find("\"process_tree_reaped\":true") != std::string::npos, "worker process tree must exit before cleanup");
      Require(body.find("\"forced_descendants\":false") != std::string::npos, "normal test cannot require forced descendants");
      RequireRemovedRoot(body);
    }
    {
      const auto unicode_temp = evidence.root() / L"private-\u6D4B\u8BD5-\U0001F9EA";
      std::filesystem::create_directory(unicode_temp);
      EnvironmentOverride temp(L"TEMP", unicode_temp.wstring()), tmp(L"TMP", unicode_temp.wstring());
      const auto report = evidence.root() / L"unicode-result.json";
      Require(RunPrivateProcess(report, {L"--private"}) == 0, "Unicode TEMP must support internal worker reports");
      RequireRemovedRoot(ReadReport(report));
    }
    // A report destination error must not replace the original worker failure.
    Require(RunPrivateProcess(evidence.root(), {L"--private", L"--fail-worker"}) == 23,
            "report finalization must preserve a failed worker exit status");
    std::cout << "private_process: pass (process-lifetime mapped file, owned cleanup, failure status, finalized evidence)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
