#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "browser/profile_session.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

using asciiomium::browser::ProfileSession;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  try {
    std::filesystem::path private_root;
    {
      ProfileSession first({}, true);
      ProfileSession other({}, true);
      private_root = first.root();
      Require(first.root() != other.root(), "private roots must differ");
      std::ofstream(first.root() / "sentinel") << "benign test data";
      Require(std::filesystem::exists(private_root), "private root exists while owned");
      bool locked = false;
      try { ProfileSession collision(first.root(), false); }
      catch (const std::exception& e) { locked = std::string(e.what()).find("already in use") != std::string::npos; }
      Require(locked, "live ownership collision refused");
    }
    Require(!std::filesystem::exists(private_root), "private data removed on scope exit");
    {
      ProfileSession locked({}, true);
      const auto blocked_file = locked.root() / "held-open";
      const auto handle = CreateFileW(blocked_file.c_str(), GENERIC_READ | GENERIC_WRITE,
          0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      Require(handle != INVALID_HANDLE_VALUE, "create cleanup negative-control lock");
      const bool premature = locked.Finish(0);
      const int error = locked.cleanup_error_code();
      CloseHandle(handle);
      Require(!premature && error != 0, "cleanup cannot claim success with an open file");
      Require(locked.Finish() && !std::filesystem::exists(locked.root()), "cleanup succeeds after the blocking handle closes");
    }
    {
      ProfileSession transient({}, true);
      const auto handle = CreateFileW((transient.root() / "transient-handle").c_str(),
          GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
          FILE_ATTRIBUTE_NORMAL, nullptr);
      Require(handle != INVALID_HANDLE_VALUE, "create transient cleanup lock");
      std::jthread release([handle] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CloseHandle(handle);
      });
      Require(transient.Finish(1500), "bounded cleanup waits for transient handle release");
      Require(transient.cleanup_attempts() > 1 && transient.cleanup_error_code() == 0,
              "transient cleanup actually retries and clears its error");
      Require(!std::filesystem::exists(transient.root()), "retry removes private root");
    }
    // This temporary owner contains persistent profiles, keeping test cleanup local.
    ProfileSession test_root({}, true);
    const auto persistent = test_root.root() / "persistent";
    {
      ProfileSession first(persistent, false);
      std::ofstream(first.root() / "sentinel") << "keep";
    }
    {
      ProfileSession reopened(persistent / ".", false);
      Require(std::filesystem::exists(reopened.root() / "sentinel"), "persistent data preserved after lock release");
    }
    bool conflict = false;
    try { ProfileSession bad(persistent, true); } catch (const std::invalid_argument&) { conflict = true; }
    Require(conflict && std::filesystem::exists(persistent / "sentinel"), "conflicting flags cannot delete user data");
    const auto bad_path = test_root.root() / "not-a-directory";
    std::ofstream(bad_path) << "file";
    bool inaccessible = false;
    try { ProfileSession bad(bad_path, false); } catch (const std::exception&) { inaccessible = true; }
    Require(inaccessible, "non-directory profile must fail");
    bool in_source = false;
    try { ProfileSession bad(test_root.root() / "source" / "profile", false, test_root.root() / "source"); }
    catch (const std::exception&) { in_source = true; }
    Require(in_source, "profiles forbidden inside source tree");
    const auto download1 = asciiomium::browser::AllocateDownload(test_root.root() / "downloads");
    const auto download2 = asciiomium::browser::AllocateDownload(test_root.root() / "downloads");
    Require(download1 != download2 && download1.filename() == "download.bin", "downloads cannot overwrite or use remote paths");
    std::cout << "profile_session: pass (ownership, restart, isolation, cleanup, error paths, destinations)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
