#include "render/ppm_loader.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asciiomium::render {
namespace {

class TokenReader {
 public:
  explicit TokenReader(std::string text) : text_(std::move(text)) {}

  [[nodiscard]] std::string_view Next() {
    SkipTrivia();
    if (position_ >= text_.size()) {
      return {};
    }
    const std::size_t start = position_;
    while (position_ < text_.size() &&
           !std::isspace(static_cast<unsigned char>(text_[position_])) &&
           text_[position_] != '#') {
      ++position_;
    }
    return std::string_view(text_).substr(start, position_ - start);
  }

 private:
  void SkipTrivia() {
    for (;;) {
      while (position_ < text_.size() &&
             std::isspace(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
      if (position_ >= text_.size() || text_[position_] != '#') {
        return;
      }
      while (position_ < text_.size() && text_[position_] != '\n') {
        ++position_;
      }
    }
  }

  std::string text_;
  std::size_t position_ = 0;
};

[[nodiscard]] int ParseInt(std::string_view token, const char* field) {
  if (token.empty()) {
    throw std::runtime_error(std::string("PPM missing ") + field);
  }
  int value = 0;
  const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
    throw std::runtime_error(std::string("PPM invalid integer for ") + field);
  }
  return value;
}

[[nodiscard]] std::uint8_t ScaleSample(int value, int maximum) {
  if (value < 0 || value > maximum) {
    throw std::runtime_error("PPM sample is outside declared range");
  }
  const auto scaled =
      (static_cast<long long>(value) * 255ll + maximum / 2) / maximum;
  return static_cast<std::uint8_t>(scaled);
}

}  // namespace

ImageBuffer LoadPpmP3(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open PPM fixture: " + path.string());
  }

  std::string text((std::istreambuf_iterator<char>(stream)),
                   std::istreambuf_iterator<char>());
  TokenReader tokens(std::move(text));

  if (tokens.Next() != "P3") {
    throw std::runtime_error("PPM fixture must use ASCII P3 format");
  }

  const int width = ParseInt(tokens.Next(), "width");
  const int height = ParseInt(tokens.Next(), "height");
  const int maximum = ParseInt(tokens.Next(), "max value");
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("PPM dimensions must be positive");
  }
  if (maximum <= 0 || maximum > 65535) {
    throw std::runtime_error("PPM max value must be in 1..65535");
  }

  const auto pixel_count = static_cast<std::size_t>(width) *
                           static_cast<std::size_t>(height);
  if (pixel_count > std::numeric_limits<std::size_t>::max() / 4u) {
    throw std::overflow_error("PPM dimensions overflow image storage");
  }

  std::vector<std::uint8_t> rgba;
  rgba.reserve(pixel_count * 4u);
  for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const int red = ParseInt(tokens.Next(), "red sample");
    const int green = ParseInt(tokens.Next(), "green sample");
    const int blue = ParseInt(tokens.Next(), "blue sample");
    rgba.push_back(ScaleSample(red, maximum));
    rgba.push_back(ScaleSample(green, maximum));
    rgba.push_back(ScaleSample(blue, maximum));
    rgba.push_back(255);
  }

  if (!tokens.Next().empty()) {
    throw std::runtime_error("PPM fixture contains trailing sample data");
  }

  return ImageBuffer(width, height, std::move(rgba));
}

}  // namespace asciiomium::render
