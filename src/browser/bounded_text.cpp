#include "browser/bounded_text.h"

#include <algorithm>
#include <cstdint>

namespace asciiomium::browser {

BoundedText BoundedUtf16Text(std::u16string_view input,
                            std::size_t maximum_bytes) {
  BoundedText result;
  result.text.reserve(std::min(input.size(), maximum_bytes));
  std::size_t offset = 0;
  while (offset < input.size()) {
    std::uint32_t scalar = input[offset];
    std::size_t units = 1;
    if (scalar >= 0xd800 && scalar <= 0xdbff) {
      if (offset + 1 < input.size() && input[offset + 1] >= 0xdc00 &&
          input[offset + 1] <= 0xdfff) {
        scalar = 0x10000 + ((scalar - 0xd800) << 10) +
                 (static_cast<std::uint32_t>(input[offset + 1]) - 0xdc00);
        units = 2;
      } else {
        scalar = 0xfffd;
      }
    } else if (scalar >= 0xdc00 && scalar <= 0xdfff) {
      scalar = 0xfffd;
    }

    const std::size_t bytes = scalar <= 0x7f ? 1 : scalar <= 0x7ff ? 2 :
                              scalar <= 0xffff ? 3 : 4;
    if (bytes > maximum_bytes - result.text.size()) break;
    char encoded[4];
    if (bytes == 1) {
      encoded[0] = static_cast<char>(scalar);
    } else if (bytes == 2) {
      encoded[0] = static_cast<char>(0xc0 | (scalar >> 6));
      encoded[1] = static_cast<char>(0x80 | (scalar & 0x3f));
    } else if (bytes == 3) {
      encoded[0] = static_cast<char>(0xe0 | (scalar >> 12));
      encoded[1] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
      encoded[2] = static_cast<char>(0x80 | (scalar & 0x3f));
    } else {
      encoded[0] = static_cast<char>(0xf0 | (scalar >> 18));
      encoded[1] = static_cast<char>(0x80 | ((scalar >> 12) & 0x3f));
      encoded[2] = static_cast<char>(0x80 | ((scalar >> 6) & 0x3f));
      encoded[3] = static_cast<char>(0x80 | (scalar & 0x3f));
    }
    result.text.append(encoded, bytes);
    offset += units;
  }
  result.truncated = offset != input.size();
  return result;
}

}  // namespace asciiomium::browser
