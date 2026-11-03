#include "terminal/terminal_session.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <system_error>

namespace asciiomium::terminal {
namespace {

class ExclusiveLock {
 public:
  explicit ExclusiveLock(SRWLOCK& lock) noexcept : lock_(&lock) {
    AcquireSRWLockExclusive(lock_);
  }
  ~ExclusiveLock() { Unlock(); }
  ExclusiveLock(const ExclusiveLock&) = delete;
  ExclusiveLock& operator=(const ExclusiveLock&) = delete;

  void Unlock() noexcept {
    if (lock_) {
      ReleaseSRWLockExclusive(lock_);
      lock_ = nullptr;
    }
  }

 private:
  SRWLOCK* lock_;
};

DWORD LastError(const TerminalConsoleApi& api) noexcept {
  const DWORD error = api.last_error(api.context);
  return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

[[noreturn]] void ThrowLastError(const TerminalConsoleApi& api,
                                 const char* operation) {
  throw std::system_error(static_cast<int>(LastError(api)),
                          std::system_category(), operation);
}

bool IsUsableHandle(HANDLE handle) {
  return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

}  // namespace

const TerminalConsoleApi& Win32TerminalConsoleApi() noexcept {
  static const TerminalConsoleApi api{
      nullptr,
      [](void*, HANDLE handle, DWORD* mode) noexcept {
        return GetConsoleMode(handle, mode);
      },
      [](void*) noexcept { return GetConsoleCP(); },
      [](void*) noexcept { return GetConsoleOutputCP(); },
      [](void*, HANDLE handle, DWORD mode) noexcept {
        return SetConsoleMode(handle, mode);
      },
      [](void*, UINT page) noexcept { return SetConsoleCP(page); },
      [](void*, UINT page) noexcept { return SetConsoleOutputCP(page); },
      [](void*, PHANDLER_ROUTINE handler, BOOL add) noexcept {
        return SetConsoleCtrlHandler(handler, add);
      },
      [](void*, HANDLE handle, CONSOLE_SCREEN_BUFFER_INFO* info) noexcept {
        return GetConsoleScreenBufferInfo(handle, info);
      },
      [](void*, HANDLE handle, const void* bytes, DWORD size,
         DWORD* written) noexcept {
        return WriteFile(handle, bytes, size, written, nullptr);
      },
      [](void*) noexcept { return GetLastError(); }};
  return api;
}

SRWLOCK TerminalSession::active_session_lock_ = SRWLOCK_INIT;
TerminalSession* TerminalSession::active_session_ = nullptr;

TerminalSession::TerminalSession(TerminalSessionOptions options)
    : TerminalSession(options,
                      GetStdHandle(STD_INPUT_HANDLE),
                      GetStdHandle(STD_OUTPUT_HANDLE)) {}

TerminalSession::TerminalSession(TerminalSessionOptions options,
                                 HANDLE input_handle,
                                 HANDLE output_handle)
    : TerminalSession(options, input_handle, output_handle,
                      Win32TerminalConsoleApi()) {}

TerminalSession::TerminalSession(TerminalSessionOptions options,
                                 HANDLE input_handle,
                                 HANDLE output_handle,
                                 const TerminalConsoleApi& console_api)
    : options_(options),
      input_handle_(input_handle),
      output_handle_(output_handle),
      console_api_(console_api) {
  // Publish while holding the state gate, so a dispatched callback cannot see
  // partially initialized ownership flags. The registry is released before
  // any Win32 call that can install or remove a handler.
  ExclusiveLock registry(active_session_lock_);
  if (active_session_ != nullptr) {
    throw std::runtime_error("only one TerminalSession may be active per process");
  }
  ExclusiveLock state(io_lock_);
  active_session_ = this;
  registry.Unlock();

  try {
    AcquireLocked();
  } catch (const std::exception& error) {
    state.Unlock();
    const auto restoration = Restore();
    throw TerminalSessionAcquisitionError(error.what(), restoration);
  } catch (...) {
    state.Unlock();
    (void)Restore();
    throw;
  }
}

TerminalSession::~TerminalSession() {
  (void)Restore();
}

void TerminalSession::AcquireLocked() {
  if (!IsUsableHandle(input_handle_) || !IsUsableHandle(output_handle_)) {
    throw std::runtime_error("ASCIIomium requires attached console input/output handles");
  }

  if (!console_api_.get_mode(console_api_.context, input_handle_,
                             &original_input_mode_)) {
    ThrowLastError(console_api_, "GetConsoleMode(input)");
  }
  if (!console_api_.get_mode(console_api_.context, output_handle_,
                             &original_output_mode_)) {
    ThrowLastError(console_api_, "GetConsoleMode(output)");
  }

  original_input_code_page_ =
      console_api_.get_input_code_page(console_api_.context);
  if (original_input_code_page_ == 0) {
    ThrowLastError(console_api_, "GetConsoleCP");
  }
  original_output_code_page_ =
      console_api_.get_output_code_page(console_api_.context);
  if (original_output_code_page_ == 0) {
    ThrowLastError(console_api_, "GetConsoleOutputCP");
  }

  const DWORD output_mode =
      original_output_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (!console_api_.set_mode(console_api_.context, output_handle_, output_mode)) {
    ThrowLastError(console_api_, "SetConsoleMode(output VT)");
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
      input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                      ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT |
                      ENABLE_WINDOW_INPUT | ENABLE_QUICK_EDIT_MODE);
      input_mode |= ENABLE_EXTENDED_FLAGS;
    }
    if (!console_api_.set_mode(console_api_.context, input_handle_, input_mode)) {
      ThrowLastError(console_api_, "SetConsoleMode(input VT)");
    }
    input_mode_changed_ = input_mode != original_input_mode_;
  }

  if (original_output_code_page_ != CP_UTF8) {
    if (!console_api_.set_output_code_page(console_api_.context, CP_UTF8)) {
      ThrowLastError(console_api_, "SetConsoleOutputCP(CP_UTF8)");
    }
    output_code_page_changed_ = true;
  }
  if (original_input_code_page_ != CP_UTF8) {
    if (!console_api_.set_input_code_page(console_api_.context, CP_UTF8)) {
      ThrowLastError(console_api_, "SetConsoleCP(CP_UTF8)");
    }
    input_code_page_changed_ = true;
  }

  if (!console_api_.set_control_handler(
          console_api_.context, &TerminalSession::ControlHandler, TRUE)) {
    ThrowLastError(console_api_, "SetConsoleCtrlHandler(install)");
  }
  handler_installed_ = true;

  (void)RefreshGeometry();
  if (!geometry().valid()) {
    throw std::runtime_error("unable to determine terminal viewport geometry");
  }

  visual_state_acquired_ = true;
  WriteLocked(BuildAcquireSequence(options_.use_alternate_screen,
                                   options_.hide_cursor));
  if (options_.mouse_tracking != MouseTrackingMode::Disabled) {
    // A partial enable write still requires its corresponding disable attempt.
    mouse_tracking_acquired_ = true;
    WriteLocked(BuildMouseTrackingEnableSequence(options_.mouse_tracking));
  }
}

bool TerminalSession::RefreshGeometry() noexcept {
  if (closing_.load(std::memory_order_acquire)) return false;
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!console_api_.get_buffer_info(console_api_.context, output_handle_, &info)) {
    return false;
  }
  const int columns =
      std::max(1, static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1));
  const int rows =
      std::max(1, static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1));
  return geometry_tracker_.Update(columns, rows);
}

void TerminalSession::Write(std::string_view bytes) {
  ExclusiveLock state(io_lock_);
  WriteLocked(bytes);
}

void TerminalSession::WriteLocked(std::string_view bytes) {
  if (closing_.load(std::memory_order_acquire)) {
    throw std::logic_error("terminal output is closed for restoration");
  }
  // Keep the entire payload under the state gate, including short writes.
  // An accepted write drains before cleanup; subsequent writes are rejected.
  while (!bytes.empty()) {
    const DWORD chunk_size = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(0x7fffffff)));
    DWORD written = 0;
    if (!console_api_.write(console_api_.context, output_handle_, bytes.data(),
                            chunk_size, &written)) {
      ThrowLastError(console_api_, "WriteFile(terminal)");
    }
    if (written == 0 || written > chunk_size) {
      throw std::runtime_error("WriteFile(terminal) made invalid progress");
    }
    bytes.remove_prefix(written);
  }
}

std::string TerminalSession::ReadInputAvailable(std::size_t max_bytes) {
  if (!options_.raw_keyboard_input) {
    throw std::logic_error(
        "ReadInputAvailable requires raw_keyboard_input terminal mode");
  }
  if (closing_.load(std::memory_order_acquire) || max_bytes == 0) return {};
  max_bytes = std::min<std::size_t>(
      max_bytes, static_cast<std::size_t>(64 * 1024));

  // Do not hold the output/cleanup gate across ReadFile: a signalled console
  // handle can contain non-character records, which ReadFile can discard.
  const DWORD wait = WaitForSingleObject(input_handle_, 0);
  if (wait == WAIT_TIMEOUT) return {};
  if (wait != WAIT_OBJECT_0) {
    if (wait == WAIT_FAILED) {
      throw std::system_error(static_cast<int>(GetLastError()),
                              std::system_category(),
                              "WaitForSingleObject(terminal input)");
    }
    throw std::runtime_error("unexpected terminal input wait result");
  }

  std::string bytes(max_bytes, '\0');
  DWORD read = 0;
  if (!ReadFile(input_handle_, bytes.data(),
                static_cast<DWORD>(bytes.size()), &read, nullptr)) {
    throw std::system_error(static_cast<int>(GetLastError()),
                            std::system_category(), "ReadFile(terminal input)");
  }
  bytes.resize(read);
  return bytes;
}

DWORD TerminalSession::WriteBestEffortLocked(std::string_view bytes) noexcept {
  while (!bytes.empty()) {
    const DWORD chunk_size = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(0x7fffffff)));
    DWORD written = 0;
    if (!console_api_.write(console_api_.context, output_handle_, bytes.data(),
                            chunk_size, &written)) {
      return LastError(console_api_);
    }
    if (written == 0 || written > chunk_size) return ERROR_WRITE_FAULT;
    bytes.remove_prefix(written);
  }
  return ERROR_SUCCESS;
}

void TerminalSession::RecordRestoreFailure(TerminalRestoreFailure step,
                                           DWORD error) noexcept {
  if (error == ERROR_SUCCESS) return;
  restore_result_.failed_steps |= static_cast<std::uint32_t>(step);
  if (restore_result_.first_error == ERROR_SUCCESS) {
    restore_result_.first_error = error;
  }
}

void TerminalSession::RestoreTerminalStateLocked() noexcept {
  if (terminal_cleanup_completed_) return;
  // No allocations and no exception-producing string builders on this path.
  // Failed steps remain recorded; later steps are always attempted.
  if (visual_state_acquired_) {
    if (mouse_tracking_acquired_) {
      RecordRestoreFailure(TerminalRestoreFailure::MouseTracking,
                           WriteBestEffortLocked(kDisableSgrMouse));
      RecordRestoreFailure(
          TerminalRestoreFailure::MouseTracking,
          WriteBestEffortLocked(
              options_.mouse_tracking == MouseTrackingMode::AnyEvent
                  ? kDisableAnyEventMouse : kDisableButtonEventMouse));
    }
    RecordRestoreFailure(TerminalRestoreFailure::Sgr,
                         WriteBestEffortLocked(kResetSgr));
    if (options_.hide_cursor) {
      RecordRestoreFailure(TerminalRestoreFailure::Cursor,
                           WriteBestEffortLocked(kShowCursor));
    }
    if (options_.use_alternate_screen) {
      RecordRestoreFailure(TerminalRestoreFailure::AlternateScreen,
                           WriteBestEffortLocked(kLeaveAlternateScreen));
    }
  }

  if (input_code_page_changed_ &&
      !console_api_.set_input_code_page(console_api_.context,
                                        original_input_code_page_)) {
    RecordRestoreFailure(TerminalRestoreFailure::InputCodePage,
                         LastError(console_api_));
  }
  if (output_code_page_changed_ &&
      !console_api_.set_output_code_page(console_api_.context,
                                         original_output_code_page_)) {
    RecordRestoreFailure(TerminalRestoreFailure::OutputCodePage,
                         LastError(console_api_));
  }
  if (input_mode_changed_ &&
      !console_api_.set_mode(console_api_.context, input_handle_,
                             original_input_mode_)) {
    RecordRestoreFailure(TerminalRestoreFailure::InputMode,
                         LastError(console_api_));
  }
  if (output_mode_changed_ &&
      !console_api_.set_mode(console_api_.context, output_handle_,
                             original_output_mode_)) {
    RecordRestoreFailure(TerminalRestoreFailure::OutputMode,
                         LastError(console_api_));
  }
  terminal_cleanup_completed_ = true;
}

TerminalRestoreResult TerminalSession::Restore() noexcept {
  closing_.store(true, std::memory_order_release);
  stop_requested_.store(true, std::memory_order_release);
  ExclusiveLock restoration(restore_lock_);
  if (restore_result_.completed) return restore_result_;

  {
    ExclusiveLock state(io_lock_);
    RestoreTerminalStateLocked();
  }

  // Do not assume handler removal drains callbacks. It must also not wait
  // while holding a gate needed by a callback that Windows already dispatched.
  if (handler_installed_) {
    if (!console_api_.set_control_handler(
            console_api_.context, &TerminalSession::ControlHandler, FALSE)) {
      RecordRestoreFailure(TerminalRestoreFailure::ControlHandler,
                           LastError(console_api_));
    }
    handler_installed_ = false;
  }
  {
    // A callback pins this object for all of its pointer use under this gate.
    // Keep the slot reserved until handler removal, to prevent a new session's
    // handler from being removed by the old session's teardown.
    ExclusiveLock registry(active_session_lock_);
    if (active_session_ == this) active_session_ = nullptr;
  }
  restore_result_.completed = true;
  return restore_result_;
}

BOOL WINAPI TerminalSession::ControlHandler(DWORD control_type) {
  ExclusiveLock registry(active_session_lock_);
  TerminalSession* session = active_session_;
  if (session == nullptr) return FALSE;

  switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      session->stop_requested_.store(true, std::memory_order_release);
      return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT: {
      session->stop_requested_.store(true, std::memory_order_release);
      session->closing_.store(true, std::memory_order_release);
      ExclusiveLock state(session->io_lock_);
      // Emergency cleanup never unregisters itself or waits for the main/CEF
      // loop. Windows can still terminate us before this best-effort work ends.
      session->RestoreTerminalStateLocked();
      return FALSE;
    }
    default:
      return FALSE;
  }
}

}  // namespace asciiomium::terminal
