#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace asciiomium::terminal {

// Incremental, allocation-free conversion for the sole high-level console
// reader. A trailing high surrogate waits for the next read; it is never
// flushed by a timer. Invalid units become U+FFFD without consuming the next
// valid unit, which may be an ESC or another browser key.
class Utf16InputEncoder {
 public:
  // One carried invalid high surrogate plus one BMP unit is the largest
  // output of Append. A valid surrogate pair needs four bytes instead.
  static constexpr std::size_t kMaxUnitBytes = 6;

  [[nodiscard]] std::size_t Append(
      std::uint16_t unit, std::span<char, kMaxUnitBytes> output) noexcept {
    std::size_t size = 0;
    if (high_surrogate_ != 0) {
      const auto high = high_surrogate_;
      high_surrogate_ = 0;
      if (unit >= 0xdc00 && unit <= 0xdfff) {
        return EncodeScalar(0x10000u + ((high - 0xd800u) << 10) +
                                (unit - 0xdc00u), output.data());
      }
      size = EncodeScalar(0xfffd, output.data());
      // Reprocess this unit after replacing the unmatched high surrogate.
    }
    if (unit >= 0xd800 && unit <= 0xdbff) {
      high_surrogate_ = unit;
      return size;
    }
    const std::uint32_t scalar =
        unit >= 0xdc00 && unit <= 0xdfff ? 0xfffd : unit;
    return size + EncodeScalar(scalar, output.data() + size);
  }

 private:
  static std::size_t EncodeScalar(std::uint32_t scalar, char* output) noexcept {
    if (scalar <= 0x7f) {
      output[0] = static_cast<char>(scalar);
      return 1;
    }
    if (scalar <= 0x7ff) {
      output[0] = static_cast<char>(0xc0 | (scalar >> 6));
      output[1] = static_cast<char>(0x80 | (scalar & 0x3f));
      return 2;
    }
    if (scalar <= 0xffff) {
      output[0] = static_cast<char>(0xe0 | (scalar >> 12));
      output[1] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
      output[2] = static_cast<char>(0x80 | (scalar & 0x3f));
      return 3;
    }
    output[0] = static_cast<char>(0xf0 | (scalar >> 18));
    output[1] = static_cast<char>(0x80 | ((scalar >> 12) & 0x3f));
    output[2] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
    output[3] = static_cast<char>(0x80 | (scalar & 0x3f));
    return 4;
  }

  std::uint16_t high_surrogate_ = 0;
};

}  // namespace asciiomium::terminal
