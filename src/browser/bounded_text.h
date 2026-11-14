#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace asciiomium::browser {

struct BoundedText {
  std::string text;
  bool truncated = false;
};

// Read only the input prefix that can fit. Output never splits a UTF-8 scalar;
// isolated UTF-16 surrogates become U+FFFD. No full input copy/conversion occurs.
[[nodiscard]] BoundedText BoundedUtf16Text(std::u16string_view input,
                                          std::size_t maximum_bytes);

}  // namespace asciiomium::browser
