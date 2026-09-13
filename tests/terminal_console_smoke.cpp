#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>

#include "terminal/terminal_session.h"

namespace {

int Fail(const char* message) {
  std::cerr << "FAIL: " << message << " (GetLastError=" << GetLastError()
            << ")\n";
  return 1;
}

bool ModesEqual(HANDLE handle, DWORD expected) {
  DWORD actual = 0;
  return GetConsoleMode(handle, &actual) && actual == expected;
}

}  // namespace

int main() {
  bool allocated_console = false;
  if (GetConsoleCP() == 0) {
    if (!AllocConsole()) {
      return Fail("AllocConsole");
    }
    allocated_console = true;
  }

  HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
  if (input == INVALID_HANDLE_VALUE) {
    if (allocated_console) {
      FreeConsole();
    }
    return Fail("CreateFileW(CONIN$)");
  }

  HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (output == INVALID_HANDLE_VALUE) {
    CloseHandle(input);
    if (allocated_console) {
      FreeConsole();
    }
    return Fail("CreateFileW(CONOUT$)");
  }

  DWORD original_input_mode = 0;
  DWORD original_output_mode = 0;
  if (!GetConsoleMode(input, &original_input_mode) ||
      !GetConsoleMode(output, &original_output_mode)) {
    CloseHandle(output);
    CloseHandle(input);
    if (allocated_console) {
      FreeConsole();
    }
    return Fail("GetConsoleMode baseline");
  }

  const UINT original_input_cp = GetConsoleCP();
  const UINT original_output_cp = GetConsoleOutputCP();
  if (original_input_cp == 0 || original_output_cp == 0) {
    CloseHandle(output);
    CloseHandle(input);
    if (allocated_console) {
      FreeConsole();
    }
    return Fail("console code page baseline");
  }

  for (int cycle = 0; cycle < 10; ++cycle) {
    asciiomium::terminal::TerminalSessionOptions options;
    options.use_alternate_screen = false;
    options.hide_cursor = false;
    options.enable_vt_input = true;

    {
      asciiomium::terminal::TerminalSession session(options, input, output);

      DWORD input_mode = 0;
      DWORD output_mode = 0;
      if (!GetConsoleMode(input, &input_mode) ||
          !GetConsoleMode(output, &output_mode)) {
        return Fail("GetConsoleMode active");
      }
      if ((input_mode & ENABLE_VIRTUAL_TERMINAL_INPUT) == 0) {
        std::cerr << "FAIL: VT input mode not enabled on cycle " << cycle << '\n';
        return 1;
      }
      if ((output_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        std::cerr << "FAIL: VT output mode not enabled on cycle " << cycle << '\n';
        return 1;
      }
      if (GetConsoleCP() != CP_UTF8 || GetConsoleOutputCP() != CP_UTF8) {
        std::cerr << "FAIL: UTF-8 code pages not enabled on cycle " << cycle
                  << '\n';
        return 1;
      }
      if (!session.geometry().valid()) {
        std::cerr << "FAIL: invalid console geometry on cycle " << cycle << '\n';
        return 1;
      }
      session.RefreshGeometry();

      // Exercise explicit cleanup and idempotence once; all other cycles rely
      // on the destructor so both supported ownership paths are covered.
      if (cycle == 0) {
        session.Restore();
        session.Restore();
      }
    }

    if (!ModesEqual(input, original_input_mode) ||
        !ModesEqual(output, original_output_mode)) {
      std::cerr << "FAIL: console modes not restored on cycle " << cycle << '\n';
      return 1;
    }
    if (GetConsoleCP() != original_input_cp ||
        GetConsoleOutputCP() != original_output_cp) {
      std::cerr << "FAIL: console code pages not restored on cycle " << cycle
                << '\n';
      return 1;
    }
  }

  CloseHandle(output);
  CloseHandle(input);
  if (allocated_console) {
    FreeConsole();
  }

  std::cout << "10 terminal acquire/restore cycles passed\n";
  return 0;
}
