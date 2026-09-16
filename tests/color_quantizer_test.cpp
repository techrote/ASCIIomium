#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "render/color_quantizer.h"

namespace {

using asciiomium::render::ColorMode;
using asciiomium::render::ModeQuantizer;
using asciiomium::render::Rgb8;
using asciiomium::render::TerminalColor;
using asciiomium::render::TerminalColorKind;

[[noreturn]] void Fail(const std::string& message) {
  throw std::runtime_error(message);
}

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    Fail(message);
  }
}

void ExpectRgb(const TerminalColor& actual,
               Rgb8 expected,
               const std::string& label) {
  if (actual.rgb != expected) {
    Fail(label + " RGB mismatch");
  }
}

void TestModeParsing() {
  ColorMode mode = ColorMode::TrueColor;
  Expect(asciiomium::render::TryParseColorMode("true", &mode) &&
             mode == ColorMode::TrueColor,
         "parse true");
  Expect(asciiomium::render::TryParseColorMode("16", &mode) &&
             mode == ColorMode::Indexed16,
         "parse 16");
  Expect(asciiomium::render::TryParseColorMode("256", &mode) &&
             mode == ColorMode::Indexed256,
         "parse 256");
  Expect(asciiomium::render::TryParseColorMode("512", &mode) &&
             mode == ColorMode::Rgb512,
         "parse 512");
  Expect(asciiomium::render::TryParseColorMode("1024", &mode) &&
             mode == ColorMode::Rgb1024,
         "parse 1024");
  Expect(!asciiomium::render::TryParseColorMode("1025", &mode),
         "reject unsupported mode");
}

void TestTrueColorIdentity() {
  const ModeQuantizer quantizer(ColorMode::TrueColor);
  for (const Rgb8 input : std::array<Rgb8, 6>{
           Rgb8{0, 0, 0}, Rgb8{255, 255, 255}, Rgb8{255, 0, 0},
           Rgb8{12, 34, 56}, Rgb8{127, 128, 129}, Rgb8{200, 100, 50}}) {
    const auto output = quantizer.Quantize(input);
    Expect(output.kind == TerminalColorKind::Rgb, "true colour must be RGB");
    ExpectRgb(output, input, "true colour identity");
  }
}

void TestXterm16() {
  const ModeQuantizer quantizer(ColorMode::Indexed16);

  const auto black = quantizer.Quantize({0, 0, 0});
  Expect(black.is_indexed() && black.index == 0, "16 black index");
  ExpectRgb(black, {0, 0, 0}, "16 black RGB");

  const auto red = quantizer.Quantize({255, 0, 0});
  Expect(red.index == 9, "16 bright red index");
  ExpectRgb(red, {255, 0, 0}, "16 bright red RGB");

  const auto mid = quantizer.Quantize({95, 135, 175});
  Expect(mid.index == 8, "16 mid colour nearest index");
  ExpectRgb(mid, {128, 128, 128}, "16 mid colour canonical RGB");
}

void TestXterm256() {
  const ModeQuantizer quantizer(ColorMode::Indexed256);

  const auto black = quantizer.Quantize({0, 0, 0});
  Expect(black.index == 0, "256 black tie prefers lower base index");

  const auto white = quantizer.Quantize({255, 255, 255});
  Expect(white.index == 15, "256 white tie prefers lower base index");

  const auto cube = quantizer.Quantize({95, 135, 175});
  Expect(cube.index == 67, "256 exact cube index");
  ExpectRgb(cube, {95, 135, 175}, "256 exact cube RGB");

  const auto gray = quantizer.Quantize({8, 8, 8});
  Expect(gray.index == 232, "256 exact grayscale index");
  ExpectRgb(gray, {8, 8, 8}, "256 exact grayscale RGB");

  Expect(asciiomium::render::XtermPaletteColor(16) == Rgb8{0, 0, 0},
         "xterm cube start");
  Expect(asciiomium::render::XtermPaletteColor(231) == Rgb8{255, 255, 255},
         "xterm cube end");
  Expect(asciiomium::render::XtermPaletteColor(255) == Rgb8{238, 238, 238},
         "xterm grayscale end");
}

void TestFixedRgbCubes() {
  const ModeQuantizer q512(ColorMode::Rgb512);
  const ModeQuantizer q1024(ColorMode::Rgb1024);

  ExpectRgb(q512.Quantize({0, 0, 0}), {0, 0, 0}, "512 zero endpoint");
  ExpectRgb(q512.Quantize({255, 255, 255}), {255, 255, 255},
            "512 full endpoint");
  ExpectRgb(q512.Quantize({95, 135, 175}), {109, 146, 182},
            "512 3/3/3 reference");

  ExpectRgb(q1024.Quantize({0, 0, 0}), {0, 0, 0}, "1024 zero endpoint");
  ExpectRgb(q1024.Quantize({255, 255, 255}), {255, 255, 255},
            "1024 full endpoint");
  ExpectRgb(q1024.Quantize({95, 135, 175}), {109, 136, 182},
            "1024 3/4/3 reference");

  Expect(asciiomium::render::QuantizeChannel(18, 8) == 0,
         "8-level lower boundary");
  Expect(asciiomium::render::QuantizeChannel(19, 8) == 36,
         "8-level upper boundary");
  Expect(asciiomium::render::QuantizeChannel(8, 16) == 0,
         "16-level lower boundary");
  Expect(asciiomium::render::QuantizeChannel(9, 16) == 17,
         "16-level upper boundary");

  Expect(q512.Quantize({100, 120, 140}).kind == TerminalColorKind::Rgb,
         "512 is RGB, not an invented index");
  Expect(q1024.Quantize({100, 120, 140}).kind == TerminalColorKind::Rgb,
         "1024 is RGB, not an invented index");
}

std::uint64_t HashByte(std::uint64_t hash, std::uint8_t byte) {
  constexpr std::uint64_t kPrime = 1099511628211ull;
  hash ^= byte;
  return hash * kPrime;
}

std::uint64_t DeterministicVectorHash(ColorMode mode) {
  constexpr std::uint64_t kOffset = 14695981039346656037ull;
  std::uint64_t hash = kOffset;
  std::uint32_t state = 0x12345678u;
  const ModeQuantizer quantizer(mode);

  for (int i = 0; i < 1024; ++i) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    const Rgb8 input{static_cast<std::uint8_t>(state),
                     static_cast<std::uint8_t>(state >> 8u),
                     static_cast<std::uint8_t>(state >> 16u)};
    const auto output = quantizer.Quantize(input);
    hash = HashByte(hash, output.kind == TerminalColorKind::Indexed ? 1u : 0u);
    hash = HashByte(hash, output.is_indexed() ? output.index : 255u);
    hash = HashByte(hash, output.rgb.r);
    hash = HashByte(hash, output.rgb.g);
    hash = HashByte(hash, output.rgb.b);
  }
  return hash;
}

void TestDeterministicVectors() {
  Expect(DeterministicVectorHash(ColorMode::TrueColor) ==
             0x12c39ee37755044eull,
         "true-colour deterministic vector hash");
  Expect(DeterministicVectorHash(ColorMode::Indexed16) ==
             0x9ed239070f579509ull,
         "16-colour deterministic vector hash");
  Expect(DeterministicVectorHash(ColorMode::Indexed256) ==
             0x8abc6971735fe199ull,
         "256-colour deterministic vector hash");
  Expect(DeterministicVectorHash(ColorMode::Rgb512) ==
             0x134d5073db27146eull,
         "512-colour deterministic vector hash");
  Expect(DeterministicVectorHash(ColorMode::Rgb1024) ==
             0x5f0767e7d755be1eull,
         "1024-colour deterministic vector hash");
}

}  // namespace

int main() {
  try {
    TestModeParsing();
    TestTrueColorIdentity();
    TestXterm16();
    TestXterm256();
    TestFixedRgbCubes();
    TestDeterministicVectors();
  } catch (const std::exception& error) {
    std::cerr << "colour quantizer test failed: " << error.what() << '\n';
    return 1;
  }

  std::cout << "colour quantizer tests passed\n";
  return 0;
}
