#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <string_view>

#include "terminal/terminal_geometry.h"

namespace asciiomium::terminal {

struct TerminalSessionOptions {
  bool use_alternate_screen = true;
  bool hide_cursor = true;
  bool enable_vt_input = true;
};

class TerminalSession {
 public:
  explicit TerminalSession(TerminalSessionOptions options = {});
  TerminalSession(TerminalSessionOptions options,
                  HANDLE input_handle,
                  HANDLE output_handle);
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
  void Restore() noexcept;

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
  void Acquire();
  void WriteBestEffort(std::string_view bytes) noexcept;
  static BOOL WINAPI ControlHandler(DWORD control_type);

  TerminalSessionOptions options_;
  HANDLE input_handle_ = INVALID_HANDLE_VALUE;
  HANDLE output_handle_ = INVALID_HANDLE_VALUE;
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
  GeometryTracker geometry_tracker_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool restore_started_{false};

  static std::atomic<TerminalSession*> active_session_;
};

}  // namespace asciiomium::terminal
