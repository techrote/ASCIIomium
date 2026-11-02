#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "input/input_event.h"

namespace asciiomium::input {

// Incremental parser for the byte stream produced by Windows Terminal when
// ENABLE_VIRTUAL_TERMINAL_INPUT is active. Input reads may end at any byte
// boundary; partial UTF-8 and escape sequences remain buffered until more data
// arrives or FlushPending() resolves an ambiguous/incomplete prefix.
class VtInputParser {
 public:
  static constexpr std::size_t kMaxPendingBytes = 64;

  [[nodiscard]] std::vector<DecodedInputEvent> Feed(std::string_view bytes);
  [[nodiscard]] std::vector<DecodedInputEvent> FlushPending();

  [[nodiscard]] bool has_pending() const noexcept { return !pending_.empty(); }
  [[nodiscard]] std::size_t pending_size() const noexcept {
    return pending_.size();
  }

 private:
  std::vector<DecodedInputEvent> ParseAvailable(bool flush);
  std::string pending_;
};

[[nodiscard]] std::string EscapeBytes(std::string_view bytes);
[[nodiscard]] std::string DescribeKeyboardEvent(const KeyboardEvent& event);
[[nodiscard]] std::string DescribePointerEvent(const PointerEvent& event);

}  // namespace asciiomium::input
