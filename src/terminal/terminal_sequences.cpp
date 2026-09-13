#include "terminal/terminal_sequences.h"

#include <algorithm>
#include <sstream>

namespace asciiomium::terminal {

std::string BuildAcquireSequence(bool use_alternate_screen, bool hide_cursor) {
  std::string sequence;
  if (use_alternate_screen) {
    sequence += kEnterAlternateScreen;
    sequence += kClearScreen;
    sequence += kCursorHome;
  }
  sequence += kResetSgr;
  if (hide_cursor) {
    sequence += kHideCursor;
  }
  return sequence;
}

std::string BuildRestoreSequence(bool used_alternate_screen,
                                 bool cursor_hidden) {
  std::string sequence(kResetSgr);
  if (cursor_hidden) {
    sequence += kShowCursor;
  }
  if (used_alternate_screen) {
    sequence += kLeaveAlternateScreen;
  }
  return sequence;
}

std::string BuildCursorPosition(int row, int column) {
  row = std::max(row, 1);
  column = std::max(column, 1);
  std::ostringstream stream;
  stream << "\x1b[" << row << ';' << column << 'H';
  return stream.str();
}

std::string BuildRgbForeground(unsigned char red,
                               unsigned char green,
                               unsigned char blue) {
  std::ostringstream stream;
  stream << "\x1b[38;2;" << static_cast<int>(red) << ';'
         << static_cast<int>(green) << ';' << static_cast<int>(blue) << 'm';
  return stream.str();
}

std::string BuildRgbBackground(unsigned char red,
                               unsigned char green,
                               unsigned char blue) {
  std::ostringstream stream;
  stream << "\x1b[48;2;" << static_cast<int>(red) << ';'
         << static_cast<int>(green) << ';' << static_cast<int>(blue) << 'm';
  return stream.str();
}

}  // namespace asciiomium::terminal
