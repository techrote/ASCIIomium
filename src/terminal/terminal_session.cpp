#include "terminal/terminal_session.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#include "terminal/terminal_sequences.h"

namespace asciiomium::terminal {
namespace {

[[noreturn]] void ThrowLastError(const char* operation) {
  throw std::system_error(static_cast<int>(GetLastError()),
                          std::system_category(), operation);
}

bool IsUsableHandle(HANDLE handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

}  // namespace

std::atomic<TerminalSession*> TerminalSession::active_session_{nullptr};

TerminalSession::TerminalSession(TerminalSessionOptions options)
    : TerminalSession(options,
                      GetStdHandle(STD_INPUT_HANDLE),
                      GetStdHandle(STD_OUTPUT_HANDLE)) {}

TerminalSession::TerminalSession(TerminalSessionOptions options,
                                 HANDLE input_handle,
                                 HANDLE output_handle)
    : options_(options),
      input_handle_(input_handle),
      output_handle_(output_handle) {
  TerminalSession* expected = nullptr;
  if (!active_session_.compare_exchange_strong(expected, this)) {
    throw std::runtime_error("only one TerminalSession may be active per process");
  }

  try {
    Acquire();
  } catch (...) {
    Restore();
    throw;
  }
}

TerminalSession::~TerminalSession() {
  Restore();
}

void TerminalSession::Acquire() {
  if (!IsUsableHandle(input_handle_) || !IsUsableHandle(output_handle_)) {
    throw std::runtime_error("ASCIIomium requires attached console input/output handles");
  }

  if (!GetConsoleMode(input_handle_, &original_input_mode_)) {
    ThrowLastError("GetConsoleMode(input)");
  }
  if (!GetConsoleMode(output_handle_, &original_output_mode_)) {
    ThrowLastError("GetConsoleMode(output)");
  }

  original_input_code_page_ = GetConsoleCP();
  original_output_code_page_ = GetConsoleOutputCP();
  if (original_input_code_page_ == 0 || original_output_code_page_ == 0) {
    ThrowLastError("GetConsoleCP/GetConsoleOutputCP");
  }

  const DWORD output_mode =
      original_output_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (!SetConsoleMode(output_handle_, output_mode)) {
    ThrowLastError("SetConsoleMode(output VT)");
  }
  output_mode_changed_ = output_mode != original_output_mode_;

  if (options_.raw_keyboard_input && !options_.enable_vt_input) {
    throw std::invalid_argument(
        "raw keyboard input requires ENABLE_VIRTUAL_TERMINAL_INPUT");
  }

  if (options_.mouse_tracking != MouseTrackingMode::Disabled &&
      !options_.enable_vt_input) {
    throw std::invalid_argument(
        "mouse tracking requires ENABLE_VIRTUAL_TERMINAL_INPUT");
  }

  if (options_.enable_vt_input) {
    DWORD input_mode = original_input_mode_ | ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (options_.raw_keyboard_input) {
      // ReadFile should expose key bytes immediately. Processed input is
      // disabled deliberately so Ctrl+C reaches the browser as 0x03 instead
      // of being stolen by the console control handler. Ctrl+Alt+Q is the live
      // browser's documented application quit chord.
      input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                      ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT |
                      ENABLE_WINDOW_INPUT | ENABLE_QUICK_EDIT_MODE);
      input_mode |= ENABLE_EXTENDED_FLAGS;
    }
    if (!SetConsoleMode(input_handle_, input_mode)) {
      ThrowLastError("SetConsoleMode(input VT)");
    }
    input_mode_changed_ = input_mode != original_input_mode_;
  }

  if (original_output_code_page_ != CP_UTF8) {
    if (!SetConsoleOutputCP(CP_UTF8)) {
      ThrowLastError("SetConsoleOutputCP(CP_UTF8)");
    }
    output_code_page_changed_ = true;
  }
  if (original_input_code_page_ != CP_UTF8) {
    if (!SetConsoleCP(CP_UTF8)) {
      ThrowLastError("SetConsoleCP(CP_UTF8)");
    }
    input_code_page_changed_ = true;
  }

  if (!SetConsoleCtrlHandler(&TerminalSession::ControlHandler, TRUE)) {
    ThrowLastError("SetConsoleCtrlHandler(install)");
  }
  handler_installed_ = true;

  (void)RefreshGeometry();
  if (!geometry().valid()) {
    throw std::runtime_error("unable to determine terminal viewport geometry");
  }

  visual_state_acquired_ = true;
  Write(BuildAcquireSequence(options_.use_alternate_screen,
                             options_.hide_cursor));
  if (options_.mouse_tracking != MouseTrackingMode::Disabled) {
    Write(BuildMouseTrackingEnableSequence(options_.mouse_tracking));
    mouse_tracking_acquired_ = true;
  }
}

bool TerminalSession::RefreshGeometry() noexcept {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(output_handle_, &info)) {
    return false;
  }

  const int columns =
      std::max(1, static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1));
  const int rows =
      std::max(1, static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1));
  return geometry_tracker_.Update(columns, rows);
}

void TerminalSession::Write(std::string_view bytes) {
  while (!bytes.empty()) {
    const DWORD chunk_size = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(0x7fffffff)));
    DWORD written = 0;
    if (!WriteFile(output_handle_, bytes.data(), chunk_size, &written, nullptr)) {
      ThrowLastError("WriteFile(terminal)");
    }
    if (written == 0) {
      throw std::runtime_error("WriteFile(terminal) made no progress");
    }
    bytes.remove_prefix(written);
  }
}

std::string TerminalSession::ReadInputAvailable(std::size_t max_bytes) {
  if (!options_.raw_keyboard_input) {
    throw std::logic_error(
        "ReadInputAvailable requires raw_keyboard_input terminal mode");
  }
  if (max_bytes == 0) {
    return {};
  }
  max_bytes = std::min<std::size_t>(
      max_bytes, static_cast<std::size_t>(64 * 1024));

  const DWORD wait = WaitForSingleObject(input_handle_, 0);
  if (wait == WAIT_TIMEOUT) {
    return {};
  }
  if (wait != WAIT_OBJECT_0) {
    if (wait == WAIT_FAILED) {
      ThrowLastError("WaitForSingleObject(terminal input)");
    }
    throw std::runtime_error("unexpected terminal input wait result");
  }

  std::string bytes(max_bytes, '\0');
  DWORD read = 0;
  if (!ReadFile(input_handle_, bytes.data(),
                static_cast<DWORD>(bytes.size()), &read, nullptr)) {
    ThrowLastError("ReadFile(terminal input)");
  }
  bytes.resize(read);
  return bytes;
}

void TerminalSession::WriteBestEffort(std::string_view bytes) noexcept {
  while (!bytes.empty()) {
    const DWORD chunk_size = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(0x7fffffff)));
    DWORD written = 0;
    if (!WriteFile(output_handle_, bytes.data(), chunk_size, &written, nullptr) ||
        written == 0) {
      return;
    }
    bytes.remove_prefix(written);
  }
}

void TerminalSession::Restore() noexcept {
  if (restore_started_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  // VT cleanup must be emitted while VT processing and UTF-8 output are still
  // active. Each operation is best-effort so one failed cleanup step cannot
  // prevent restoring the remaining Win32 console state.
  if (visual_state_acquired_) {
    if (mouse_tracking_acquired_) {
      WriteBestEffort(BuildMouseTrackingDisableSequence(
          options_.mouse_tracking));
      mouse_tracking_acquired_ = false;
    }
    WriteBestEffort(kResetSgr);
    if (options_.hide_cursor) {
      WriteBestEffort(kShowCursor);
    }
    if (options_.use_alternate_screen) {
      WriteBestEffort(kLeaveAlternateScreen);
    }
    visual_state_acquired_ = false;
  }

  if (input_code_page_changed_) {
    SetConsoleCP(original_input_code_page_);
    input_code_page_changed_ = false;
  }
  if (output_code_page_changed_) {
    SetConsoleOutputCP(original_output_code_page_);
    output_code_page_changed_ = false;
  }
  if (input_mode_changed_) {
    SetConsoleMode(input_handle_, original_input_mode_);
    input_mode_changed_ = false;
  }
  if (output_mode_changed_) {
    SetConsoleMode(output_handle_, original_output_mode_);
    output_mode_changed_ = false;
  }

  if (handler_installed_) {
    SetConsoleCtrlHandler(&TerminalSession::ControlHandler, FALSE);
    handler_installed_ = false;
  }

  TerminalSession* expected = this;
  active_session_.compare_exchange_strong(expected, nullptr);
}

BOOL WINAPI TerminalSession::ControlHandler(DWORD control_type) {
  TerminalSession* session = active_session_.load(std::memory_order_acquire);
  if (session == nullptr) {
    return FALSE;
  }

  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      // Let the main loop leave normally so teardown is sequenced through RAII.
      session->stop_requested_.store(true, std::memory_order_release);
      return TRUE;

    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      // Windows may terminate the process shortly after this handler returns.
      // Restore synchronously as a best-effort fallback for these paths.
      session->stop_requested_.store(true, std::memory_order_release);
      session->Restore();
      return FALSE;

    default:
      return FALSE;
  }
}

}  // namespace asciiomium::terminal
