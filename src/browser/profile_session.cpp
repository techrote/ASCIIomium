#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "browser/profile_session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace asciiomium::browser {
namespace {
std::filesystem::path UniqueDirectory(const std::filesystem::path& parent,
                                      const std::wstring& prefix) {
  std::filesystem::create_directories(parent);
  static std::atomic<unsigned long long> sequence{0};
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = parent / (prefix + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(stamp) + L"-" + std::to_wstring(sequence++));
    if (std::filesystem::create_directory(path)) return path;
  }
  throw std::runtime_error("cannot allocate unique browser data directory");
}

bool Within(const std::filesystem::path& candidate,
            const std::filesystem::path& parent) {
  if (parent.empty()) return false;
  auto c = std::filesystem::weakly_canonical(candidate).native();
  auto p = std::filesystem::weakly_canonical(parent).native();
  for (auto& ch : c) ch = static_cast<wchar_t>(std::towlower(ch));
  for (auto& ch : p) ch = static_cast<wchar_t>(std::towlower(ch));
  if (c == p) return true;
  if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
  return c.starts_with(p);
}

std::filesystem::path ExecutableDirectory() {
  std::vector<wchar_t> path(32768);
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (count == 0 || count >= path.size()) throw std::runtime_error("cannot determine application directory");
  return std::filesystem::path(std::wstring(path.data(), count)).parent_path();
}
}  // namespace

ProfileSession::ProfileSession(const std::filesystem::path& requested,
                               bool private_mode,
                               const std::filesystem::path& source_directory)
    : private_(private_mode) {
  if (private_ && !requested.empty()) throw std::invalid_argument("--private and --profile cannot be combined");
  try {
    if (private_) {
      root_ = UniqueDirectory(std::filesystem::temp_directory_path() / L"ASCIIomium", L"private-");
    } else if (!requested.empty()) {
      root_ = std::filesystem::absolute(requested);
    } else {
      std::vector<wchar_t> local(32768);
      const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
      if (count == 0 || count >= local.size()) throw std::runtime_error("LOCALAPPDATA unavailable; choose --profile outside the application directory");
      root_ = std::filesystem::path(local.data()) / L"ASCIIomium" / L"Profiles" / L"Default";
    }
    root_ = std::filesystem::weakly_canonical(std::filesystem::absolute(root_));
    if (Within(root_, ExecutableDirectory()) || Within(root_, source_directory)) {
      throw std::runtime_error("profile must be outside the source and application directories");
    }
    std::filesystem::create_directories(root_);
    const auto lock_file = root_ / L".asciiomium.lock";
    const auto handle = CreateFileW(lock_file.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const auto error = GetLastError();
      if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
        throw std::runtime_error("profile is already in use; choose another --profile or --private");
      throw std::runtime_error("profile cannot be opened for writing (Windows error " + std::to_string(error) + ")");
    }
    lock_ = handle;
  } catch (...) {
    (void)Finish();
    throw;
  }
}

ProfileSession::~ProfileSession() { (void)Finish(); }

bool ProfileSession::Finish(int retry_budget_ms) noexcept {
  if (finished_) return true;
  if (cef_active_) return false;  // Never delete a root still owned by Chromium.
  if (lock_ != nullptr) { CloseHandle(static_cast<HANDLE>(lock_)); lock_ = nullptr; }
  if (private_ && !root_.empty()) {
    // Windows can release Chromium child-process file/mapping handles shortly
    // after CefShutdown returns. Retry only the owned disposable root, with a
    // fixed budget; never call cleanup before CEF has stopped or claim success
    // if an OS/file lock remains. This also tolerates transient scanner handles.
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, retry_budget_ms));
    while (true) {
      std::error_code error;
      ++cleanup_attempts_;
      std::filesystem::remove_all(root_, error);
      cleanup_error_ = error.value();
      if (!error) break;
      if (std::chrono::steady_clock::now() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  finished_ = true;
  return true;
}

std::filesystem::path AllocateDownload(const std::filesystem::path& directory) {
  if (directory.empty()) throw std::invalid_argument("downloads are disabled");
  const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute(directory));
  return UniqueDirectory(root, L"download-") / L"download.bin";
}
}  // namespace asciiomium::browser
