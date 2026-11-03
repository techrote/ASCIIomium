#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace asciiomium::terminal {

// A narrow seam for deterministic console-ownership tests. The production
// table forwards directly to Win32. A supplied table and its context must
// outlive the session, including any control callback being drained by Restore.
struct TerminalConsoleApi {
  void* context;
  BOOL (*get_mode)(void*, HANDLE, DWORD*) noexcept;
  UINT (*get_input_code_page)(void*) noexcept;
  UINT (*get_output_code_page)(void*) noexcept;
  BOOL (*set_mode)(void*, HANDLE, DWORD) noexcept;
  BOOL (*set_input_code_page)(void*, UINT) noexcept;
  BOOL (*set_output_code_page)(void*, UINT) noexcept;
  BOOL (*set_control_handler)(void*, PHANDLER_ROUTINE, BOOL) noexcept;
  BOOL (*get_buffer_info)(void*, HANDLE, CONSOLE_SCREEN_BUFFER_INFO*) noexcept;
  BOOL (*write)(void*, HANDLE, const void*, DWORD, DWORD*) noexcept;
  DWORD (*last_error)(void*) noexcept;
  BOOL (*read)(void*, HANDLE, void*, DWORD, DWORD*) noexcept;
  BOOL (*cancel_synchronous_io)(void*, HANDLE) noexcept;
};

const TerminalConsoleApi& Win32TerminalConsoleApi() noexcept;

}  // namespace asciiomium::terminal
