#pragma once

#include <filesystem>
#include <string>

namespace asciiomium::browser {

// Lifetime encloses CefInitialize/CefShutdown. The OS handle, not existence of
// the lock file, proves ownership; a crash cannot leave a stale logical lock.
class ProfileSession {
 public:
  ProfileSession(const std::filesystem::path& requested, bool private_mode,
                 const std::filesystem::path& source_directory = {});
  ~ProfileSession();
  ProfileSession(const ProfileSession&) = delete;
  ProfileSession& operator=(const ProfileSession&) = delete;
  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  [[nodiscard]] bool private_mode() const { return private_; }
  // Call after CefShutdown to observe cleanup errors. Destructor is fallback.
  [[nodiscard]] bool Finish(int retry_budget_ms = 2000) noexcept;
  [[nodiscard]] int cleanup_error_code() const noexcept { return cleanup_error_; }
  [[nodiscard]] unsigned cleanup_attempts() const noexcept { return cleanup_attempts_; }
  [[nodiscard]] bool cleanup_deferred() const noexcept { return worker_owned_; }
  void SetCefActive(bool active) noexcept { cef_active_ = active; }

 private:
  std::filesystem::path root_;
  void* lock_ = nullptr;
  bool private_ = false;
  bool finished_ = false;
  bool cef_active_ = false;
  bool worker_owned_ = false;
  int cleanup_error_ = 0;
  unsigned cleanup_attempts_ = 0;
};

// Creates a unique, application-selected destination. Remote suggested names
// never become paths; no overwrite, shell execution or OS download dialog.
[[nodiscard]] std::filesystem::path AllocateDownload(
    const std::filesystem::path& directory);

}  // namespace asciiomium::browser
