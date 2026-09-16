#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "render/halfblock_renderer.h"
#include "render/ppm_loader.h"

namespace {

using asciiomium::render::FitMode;
using asciiomium::render::ImageBuffer;
using asciiomium::render::ImageView;
using asciiomium::render::PixelFormat;
using asciiomium::render::RenderConfig;
using asciiomium::render::RenderHalfBlock;
using asciiomium::render::Rgb8;
using asciiomium::render::Rgba8;
using asciiomium::render::SamplingFilter;
using asciiomium::render::TerminalCell;

[[noreturn]] void Fail(const std::string& message) {
  throw std::runtime_error(message);
}

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    Fail(message);
  }
}

void ExpectColor(Rgb8 actual, Rgb8 expected, const std::string& label) {
  if (actual != expected) {
    Fail(label + " expected (" + std::to_string(expected.r) + "," +
         std::to_string(expected.g) + "," + std::to_string(expected.b) +
         ") got (" + std::to_string(actual.r) + "," +
         std::to_string(actual.g) + "," + std::to_string(actual.b) + ")");
  }
}

ImageBuffer MakeImage(int width,
                      int height,
                      std::initializer_list<Rgba8> pixels) {
  const auto expected = static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height);
  Expect(pixels.size() == expected, "MakeImage pixel count mismatch");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(expected * 4u);
  for (const auto pixel : pixels) {
    bytes.push_back(pixel.r);
    bytes.push_back(pixel.g);
    bytes.push_back(pixel.b);
    bytes.push_back(pixel.a);
  }
  return ImageBuffer(width, height, std::move(bytes));
}

void TestFixtureExactHalfBlockMapping() {
  const auto fixture = std::filesystem::path(ASCIIOMIUM_SOURCE_DIR) /
                       "fixtures" / "render" / "quad_2x4.ppm";
  const auto image = asciiomium::render::LoadPpmP3(fixture);

  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  const auto frame = RenderHalfBlock(image.view(), {2, 2}, config);

  Expect(frame.columns() == 2 && frame.rows() == 2, "fixture frame geometry");
  Expect(frame.size() == 4, "fixture frame cell count");

  const TerminalCell expected00{asciiomium::render::kUpperHalfBlock,
                                {255, 0, 0}, {0, 0, 255}};
  const TerminalCell expected10{asciiomium::render::kUpperHalfBlock,
                                {0, 255, 0}, {255, 255, 255}};
  const TerminalCell expected01{asciiomium::render::kUpperHalfBlock,
                                {0, 0, 0}, {255, 0, 255}};
  const TerminalCell expected11{asciiomium::render::kUpperHalfBlock,
                                {255, 255, 0}, {0, 255, 255}};
  Expect(frame.at(0, 0) == expected00, "fixture cell 0,0");
  Expect(frame.at(1, 0) == expected10, "fixture cell 1,0");
  Expect(frame.at(0, 1) == expected01, "fixture cell 0,1");
  Expect(frame.at(1, 1) == expected11, "fixture cell 1,1");
}

void TestBoxAverage() {
  const auto image = MakeImage(
      2, 2, {{255, 0, 0, 255}, {0, 255, 0, 255},
             {0, 0, 255, 255}, {255, 255, 255, 255}});
  RenderConfig config;
  config.filter = SamplingFilter::BoxAverage;
  const auto frame = RenderHalfBlock(image.view(), {1, 1}, config);
  ExpectColor(frame.at(0, 0).foreground, {128, 128, 0},
              "box average upper");
  ExpectColor(frame.at(0, 0).background, {128, 128, 255},
              "box average lower");
}

void TestAlphaCompositing() {
  const auto image = MakeImage(
      1, 2, {{255, 0, 0, 128}, {0, 255, 0, 0}});
  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  config.alpha_background = {0, 0, 255};
  const auto frame = RenderHalfBlock(image.view(), {1, 1}, config);
  ExpectColor(frame.at(0, 0).foreground, {128, 0, 127},
              "alpha upper");
  ExpectColor(frame.at(0, 0).background, {0, 0, 255},
              "alpha transparent lower");
}

void TestBgraView() {
  const std::vector<std::uint8_t> bgra = {
      0, 0, 255, 255,
      0, 255, 0, 255,
  };
  const ImageView view{bgra.data(), 1, 2, 4, PixelFormat::BGRA8};
  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  const auto frame = RenderHalfBlock(view, {1, 1}, config);
  ExpectColor(frame.at(0, 0).foreground, {255, 0, 0}, "BGRA upper");
  ExpectColor(frame.at(0, 0).background, {0, 255, 0}, "BGRA lower");
}

void TestOddSourceMapping() {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(3u * 3u * 4u);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      bytes.push_back(static_cast<std::uint8_t>(x + y * 10));
      bytes.push_back(0);
      bytes.push_back(0);
      bytes.push_back(255);
    }
  }
  const ImageBuffer image(3, 3, std::move(bytes));
  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  const auto frame = RenderHalfBlock(image.view(), {1, 1}, config);
  ExpectColor(frame.at(0, 0).foreground, {1, 0, 0},
              "odd mapping upper centre");
  ExpectColor(frame.at(0, 0).background, {21, 0, 0},
              "odd mapping lower centre");
}

void TestTinySourceUpscale() {
  const auto image = MakeImage(1, 1, {{7, 8, 9, 255}});
  RenderConfig config;
  config.filter = SamplingFilter::BoxAverage;
  const auto frame = RenderHalfBlock(image.view(), {3, 2}, config);
  Expect(frame.size() == 6, "tiny upscale size");
  for (const auto& cell : frame.cells()) {
    ExpectColor(cell.foreground, {7, 8, 9}, "tiny upscale foreground");
    ExpectColor(cell.background, {7, 8, 9}, "tiny upscale background");
  }
}

void TestContainCellAspectLetterbox() {
  const auto image = MakeImage(
      2, 1, {{255, 0, 0, 255}, {255, 0, 0, 255}});
  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  config.fit_mode = FitMode::Contain;
  config.cell_aspect = 1.0;
  config.alpha_background = {0, 0, 0};

  const auto frame = RenderHalfBlock(image.view(), {2, 2}, config);
  for (int column = 0; column < 2; ++column) {
    ExpectColor(frame.at(column, 0).foreground, {0, 0, 0},
                "contain top letterbox");
    ExpectColor(frame.at(column, 0).background, {255, 0, 0},
                "contain first content sample");
    ExpectColor(frame.at(column, 1).foreground, {255, 0, 0},
                "contain second content sample");
    ExpectColor(frame.at(column, 1).background, {0, 0, 0},
                "contain bottom letterbox");
  }
}

class FixedQuantizer final : public asciiomium::render::ColorQuantizer {
 public:
  Rgb8 Quantize(Rgb8) const noexcept override { return {1, 2, 3}; }
};

void TestQuantizerInterface() {
  const auto image = MakeImage(1, 1, {{200, 100, 50, 255}});
  FixedQuantizer quantizer;
  RenderConfig config;
  config.filter = SamplingFilter::Nearest;
  config.quantizer = &quantizer;
  const auto frame = RenderHalfBlock(image.view(), {1, 1}, config);
  ExpectColor(frame.at(0, 0).foreground, {1, 2, 3}, "quantized foreground");
  ExpectColor(frame.at(0, 0).background, {1, 2, 3}, "quantized background");
}

void TestHardBoundsAndDeterminism() {
  const auto image = MakeImage(
      2, 2, {{1, 2, 3, 255}, {4, 5, 6, 255},
             {7, 8, 9, 255}, {10, 11, 12, 255}});
  RenderConfig config;
  config.filter = SamplingFilter::BoxAverage;
  const auto first = RenderHalfBlock(image.view(), {2, 3}, config);
  const auto second = RenderHalfBlock(image.view(), {2, 3}, config);
  Expect(first == second, "renderer must be deterministic");
  Expect(first.size() == 6, "hard viewport cell count");
  Expect(first.try_cell(-1, 0) == nullptr, "negative column clipped");
  Expect(first.try_cell(2, 0) == nullptr, "right edge clipped");
  Expect(first.try_cell(0, 3) == nullptr, "bottom edge clipped");

  bool threw = false;
  try {
    (void)first.at(2, 0);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  Expect(threw, "out-of-bounds at() must throw");
}

}  // namespace

int main() {
  try {
    TestFixtureExactHalfBlockMapping();
    TestBoxAverage();
    TestAlphaCompositing();
    TestBgraView();
    TestOddSourceMapping();
    TestTinySourceUpscale();
    TestContainCellAspectLetterbox();
    TestQuantizerInterface();
    TestHardBoundsAndDeterminism();
  } catch (const std::exception& error) {
    std::cerr << "render golden test failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "renderer golden tests passed\n";
  return 0;
}
