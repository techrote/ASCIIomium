#pragma once

#include <string>

namespace asciiomium::terminal {

inline constexpr char kEnterAlternateScreen[] = "\x1b[?1049h";
inline constexpr char kLeaveAlternateScreen[] = "\x1b[?1049l";
inline constexpr char kHideCursor[] = "\x1b[?25l";
inline constexpr char kShowCursor[] = "\x1b[?25h";
inline constexpr char kResetSgr[] = "\x1b[0m";
inline constexpr char kClearScreen[] = "\x1b[2J";
inline constexpr char kCursorHome[] = "\x1b[H";

std::string BuildAcquireSequence(bool use_alternate_screen, bool hide_cursor);
std::string BuildRestoreSequence(bool used_alternate_screen, bool cursor_hidden);
std::string BuildCursorPosition(int row, int column);
std::string BuildRgbForeground(unsigned char red,
                               unsigned char green,
                               unsigned char blue);
std::string BuildRgbBackground(unsigned char red,
                               unsigned char green,
                               unsigned char blue);

}  // namespace asciiomium::terminal
