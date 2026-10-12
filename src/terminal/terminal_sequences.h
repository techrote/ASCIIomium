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
inline constexpr char kEnableButtonEventMouse[] = "\x1b[?1002h";
inline constexpr char kDisableButtonEventMouse[] = "\x1b[?1002l";
inline constexpr char kEnableAnyEventMouse[] = "\x1b[?1003h";
inline constexpr char kDisableAnyEventMouse[] = "\x1b[?1003l";
inline constexpr char kEnableSgrMouse[] = "\x1b[?1006h";
inline constexpr char kDisableSgrMouse[] = "\x1b[?1006l";

enum class MouseTrackingMode {
  Disabled,
  ButtonEvent,
  AnyEvent,
};

std::string BuildAcquireSequence(bool use_alternate_screen, bool hide_cursor);
std::string BuildRestoreSequence(bool used_alternate_screen, bool cursor_hidden);
std::string BuildMouseTrackingEnableSequence(MouseTrackingMode mode);
std::string BuildMouseTrackingDisableSequence(MouseTrackingMode mode);
std::string BuildCursorPosition(int row, int column);
std::string BuildRgbForeground(unsigned char red,
                               unsigned char green,
                               unsigned char blue);
std::string BuildRgbBackground(unsigned char red,
                               unsigned char green,
                               unsigned char blue);
std::string BuildIndexedForeground(unsigned char index);
std::string BuildIndexedBackground(unsigned char index);

}  // namespace asciiomium::terminal
