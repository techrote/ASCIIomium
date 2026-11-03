#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "terminal/terminal_console_api.h"
#include "terminal/terminal_geometry.h"
#include "terminal/terminal_sequences.h"

namespace asciiomium::terminal {

struct TerminalSessionOptions {
  bool use_alternate_screen = true;
  bool hide_cursor = true;
  bool enable_vt_input = true;
  // Browser keyboard mode needs immediate byte-stream delivery instead of the
  // console's line editor. This is opt-in so existing diagnostics retain their
  // Ctrl+C console-control behaviour.
  bool raw_keyboard_input = false;
  MouseTrackingMode mouse_tracking = MouseTrackingMode::Disabled;
};

// Stable bits used in live evidence. Failure of one step never skips the rest.
enum class TerminalRestoreFailure : std::uint32_t {
  MouseTracking = 1u << 0,
  Sgr = 1u << 1,
  Cursor = 1u << 2,
  AlternateScreen = 1u << 3,
  InputCodePage = 1u << 4,
  OutputCodePage = 1u << 5,
  InputMode = 1u << 6,
  OutputMode = 1u << 7,
  ControlHandler = 1u << 8,
};

struct TerminalRestoreResult {
  bool completed = false;
  std::uint32_t failed_steps = 0;
  DWORD first_error = ERROR_SUCCESS;

  [[nodiscard]] bool succeeded() const noexcept {
    return completed && failed_steps == 0;
  }
};

// Constructor rollback has no surviving session object. Preserve its cleanup
// result so callers cannot mistake failed acquisition for a headless run.
class TerminalSessionAcquisitionError : public std::runtime_error {
 public:
  TerminalSessionAcquisitionError(const char* message,
                                 TerminalRestoreResult restoration)
      : std::runtime_error(message), restoration_(restoration) {}
  [[nodiscard]] TerminalRestoreResult restoration() const noexcept {
    return restoration_;
  }

 private:
  TerminalRestoreResult restoration_;
};

class TerminalSession {
 public:
  explicit TerminalSession(TerminalSessionOptions options = {});
  TerminalSession(TerminalSessionOptions options,
                  HANDLE input_handle,
                  HANDLE output_handle);
  TerminalSession(TerminalSessionOptions options,
                  HANDLE input_handle,
                  HANDLE output_handle,
                  const TerminalConsoleApi& console_api);
  ~TerminalSession();

  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;
  TerminalSession(TerminalSession&&) = delete;
  TerminalSession& operator=(TerminalSession&&) = delete;

  [[nodiscard]] bool RefreshGeometry() noexcept;
  [[nodiscard]] const TerminalGeometry& geometry() const noexcept {
    return geometry_tracker_.current();
  }

  void Write(std::string_view bytes);
  [[nodiscard]] std::string ReadInputAvailable(
      std::size_t max_bytes = 4096);
  // Waits for in-flight writes and callbacks. The first complete result is
  // retained, including failures; subsequent calls do not replay cleanup.
  [[nodiscard]] TerminalRestoreResult Restore() noexcept;

  [[nodiscard]] bool stop_requested() const noexcept {
    return stop_requested_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] DWORD original_input_mode() const noexcept {
    return original_input_mode_;
  }
  [[nodiscard]] DWORD original_output_mode() const noexcept {
    return original_output_mode_;
  }
  [[nodiscard]] UINT original_input_code_page() const noexcept {
    return original_input_code_page_;
  }
  [[nodiscard]] UINT original_output_code_page() const noexcept {
    return original_output_code_page_;
  }

 private:
  void AcquireLocked();
  void WriteLocked(std::string_view bytes);
  [[nodiscard]] DWORD WriteBestEffortLocked(std::string_view bytes) noexcept;
  void RestoreTerminalStateLocked() noexcept;
  void RecordRestoreFailure(TerminalRestoreFailure step, DWORD error) noexcept;
  static BOOL WINAPI ControlHandler(DWORD control_type);

  TerminalSessionOptions options_;
  HANDLE input_handle_ = INVALID_HANDLE_VALUE;
  HANDLE output_handle_ = INVALID_HANDLE_VALUE;
  const TerminalConsoleApi& console_api_;
  DWORD original_input_mode_ = 0;
  DWORD original_output_mode_ = 0;
  UINT original_input_code_page_ = 0;
  UINT original_output_code_page_ = 0;
  bool input_mode_changed_ = false;
  bool output_mode_changed_ = false;
  bool input_code_page_changed_ = false;
  bool output_code_page_changed_ = false;
  bool handler_installed_ = false;
  bool visual_state_acquired_ = false;
  bool mouse_tracking_acquired_ = false;
  GeometryTracker geometry_tracker_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool closing_{false};

  // Callback dispatch pins the target under active_session_lock_. State/output
  // uses io_lock_. Public Restore additionally serializes final unregistration
  // and result publication with restore_lock_. It never holds either of the
  // first two locks while unregistering the Win32 handler.
  SRWLOCK io_lock_ = SRWLOCK_INIT;
  SRWLOCK restore_lock_ = SRWLOCK_INIT;
  bool terminal_cleanup_completed_ = false;
  TerminalRestoreResult restore_result_;

  // The registry lock has process lifetime and no C++ destruction ordering.
  static SRWLOCK active_session_lock_;
  static TerminalSession* active_session_;
};

}  // namespace asciiomium::terminal
