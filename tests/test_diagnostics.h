#pragma once

// Test processes must fail with captured diagnostics instead of waiting for an
// unattended CRT dialog. These handlers never turn an assertion into success.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace test_diagnostics {

[[noreturn]] inline void Fail(const char* message) noexcept {
  std::fputs("FAIL: ", stderr);
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::_Exit(86);
}

inline void Stage(const char* message) noexcept {
  std::fputs("TEST STAGE: ", stderr);
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

#if defined(_MSC_VER) && defined(_DEBUG)
inline int __cdecl CrtReport(int type, char* message, int*) {
  if (type == _CRT_ASSERT || type == _CRT_ERROR)
    Fail(message ? message : "CRT assertion/error without a message");
  return 0;
}

inline int __cdecl CrtReportWide(int type, wchar_t* message, int*) {
  if (type == _CRT_ASSERT || type == _CRT_ERROR) {
    std::fputs("FAIL: CRT assertion/error: ", stderr);
    // stderr is byte-oriented after a stage marker. Preserve ASCII diagnostics
    // without allocating a conversion buffer or mixing wide and narrow I/O.
    if (message) {
      for (std::size_t index = 0; index < 8192 && message[index]; ++index) {
        const auto value = message[index];
        std::fputc(static_cast<unsigned int>(value) <= 0x7fu ? static_cast<int>(value) : '?', stderr);
      }
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::_Exit(86);
  }
  return 0;
}
#endif

inline void Configure() noexcept {
  std::set_terminate([] { Fail("unexpected std::terminate in test process"); });
#if defined(_MSC_VER)
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _CALL_REPORTFAULT);
#if defined(_DEBUG)
  for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
  if (_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, CrtReport) < 0 ||
      _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, CrtReportWide) < 0)
    Fail("cannot install noninteractive CRT report hooks");
#endif
#endif
}

}  // namespace test_diagnostics
