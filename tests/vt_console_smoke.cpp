#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <vector>

#include "render/render_types.h"
#include "terminal/full_frame_emitter.h"
#include "terminal/terminal_session.h"

namespace {

int Fail(const char* message) {
  std::cerr << "FAIL: " << message << " (GetLastError=" << GetLastError()
            << ")\n";
  return 1;
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
  HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (allocated_console) FreeConsole();
    return Fail("open console handles");
  }

  int result = 0;
  {
    asciiomium::terminal::TerminalSessionOptions options;
    options.use_alternate_screen = true;
    options.hide_cursor = false;
    options.enable_vt_input = false;

    try {
      asciiomium::terminal::TerminalSession session(options, input, output);
      const auto geometry = session.geometry();
      if (!geometry.valid()) {
        result = Fail("valid terminal geometry");
      } else {
        std::vector<asciiomium::render::TerminalCell> cells;
        cells.reserve(static_cast<std::size_t>(geometry.columns) *
                      static_cast<std::size_t>(geometry.rows));

        for (int row = 0; row < geometry.rows; ++row) {
          for (int column = 0; column < geometry.columns; ++column) {
            const char32_t glyph =
                column == 0 ? static_cast<char32_t>(U'A' + (row % 26))
                            : U'\u2580';
            const auto foreground = asciiomium::render::TerminalColor::Indexed(
                static_cast<std::uint8_t>(9 + (row % 6)), {255, 255, 255});
            const auto background = asciiomium::render::TerminalColor::Rgb(
                {static_cast<std::uint8_t>((row * 13) & 0xFF),
                 static_cast<std::uint8_t>((row * 29) & 0xFF),
                 static_cast<std::uint8_t>((row * 47) & 0xFF)});
            cells.push_back({glyph, foreground, background});
          }
        }

        const asciiomium::render::TerminalFrame frame(
            geometry.columns, geometry.rows, std::move(cells));
        const auto emission =
            asciiomium::terminal::SerializeFullFrame(frame);
        session.Write(emission.bytes);

        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(output, &info)) {
          result = Fail("GetConsoleScreenBufferInfo after full frame");
        } else if (info.dwCursorPosition.X != 0 ||
                   info.dwCursorPosition.Y != 0) {
          std::cerr << "FAIL: full-frame emitter did not return cursor home: "
                    << info.dwCursorPosition.X << ',' << info.dwCursorPosition.Y
                    << '\n';
          result = 1;
        }

        wchar_t top_left = L'\0';
        DWORD characters_read = 0;
        if (result == 0 &&
            !ReadConsoleOutputCharacterW(output, &top_left, 1, COORD{0, 0},
                                         &characters_read)) {
          result = Fail("ReadConsoleOutputCharacterW");
        } else if (result == 0 &&
                   (characters_read != 1 || top_left != L'A')) {
          std::cerr << "FAIL: top row changed after writing bottom-right cell; "
                       "possible autowrap scroll\n";
          result = 1;
        }
      }
    } catch (const std::exception& error) {
      std::cerr << "FAIL: VT console smoke exception: " << error.what() << '\n';
      result = 1;
    }
  }

  CloseHandle(output);
  CloseHandle(input);
  if (allocated_console) {
    FreeConsole();
  }

  if (result == 0) {
    std::cout << "full-width VT frame did not scroll and cursor returned home\n";
  }
  return result;
}
