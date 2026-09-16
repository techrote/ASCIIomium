#include "render/halfblock_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace asciiomium::render {
namespace {

struct RectD {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
};

[[nodiscard]] Rgba8 ReadPixel(ImageView image, int x, int y) noexcept {
  const auto* pixel = image.data +
                      static_cast<std::size_t>(y) * image.stride_bytes +
                      static_cast<std::size_t>(x) * 4u;
  if (image.format == PixelFormat::BGRA8) {
    return Rgba8{pixel[2], pixel[1], pixel[0], pixel[3]};
  }
  return Rgba8{pixel[0], pixel[1], pixel[2], pixel[3]};
}

[[nodiscard]] std::uint8_t CompositeChannel(std::uint8_t source,
                                             std::uint8_t alpha,
                                             std::uint8_t background) noexcept {
  const unsigned a = alpha;
  const unsigned inverse = 255u - a;
  const unsigned value = static_cast<unsigned>(source) * a +
                         static_cast<unsigned>(background) * inverse;
  return static_cast<std::uint8_t>((value + 127u) / 255u);
}

[[nodiscard]] Rgb8 Composite(Rgba8 source, Rgb8 background) noexcept {
  if (source.a == 255) {
    return Rgb8{source.r, source.g, source.b};
  }
  if (source.a == 0) {
    return background;
  }
  return Rgb8{CompositeChannel(source.r, source.a, background.r),
              CompositeChannel(source.g, source.a, background.g),
              CompositeChannel(source.b, source.a, background.b)};
}

[[nodiscard]] RectD ComputeContentRect(ImageView source,
                                       RenderTarget target,
                                       const RenderConfig& config) {
  const double sample_columns = static_cast<double>(target.columns);
  const double sample_rows = static_cast<double>(target.rows) * 2.0;

  if (config.fit_mode == FitMode::Stretch) {
    return RectD{0.0, 0.0, sample_columns, sample_rows};
  }

  const double source_aspect =
      static_cast<double>(source.width) / static_cast<double>(source.height);
  const double sample_physical_aspect = 2.0 * config.cell_aspect;
  const double desired_lattice_aspect = source_aspect / sample_physical_aspect;
  const double target_lattice_aspect = sample_columns / sample_rows;

  RectD rect;
  if (target_lattice_aspect > desired_lattice_aspect) {
    rect.height = sample_rows;
    rect.width = desired_lattice_aspect * rect.height;
    rect.x = (sample_columns - rect.width) * 0.5;
    rect.y = 0.0;
  } else {
    rect.width = sample_columns;
    rect.height = rect.width / desired_lattice_aspect;
    rect.x = 0.0;
    rect.y = (sample_rows - rect.height) * 0.5;
  }
  return rect;
}

[[nodiscard]] bool Contains(const RectD& rect, double x, double y) noexcept {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.width &&
         y < rect.y + rect.height;
}

[[nodiscard]] Rgb8 NearestSample(ImageView source,
                                 const RectD& content,
                                 int sample_x,
                                 int sample_y,
                                 Rgb8 background) noexcept {
  const double x = static_cast<double>(sample_x) + 0.5;
  const double y = static_cast<double>(sample_y) + 0.5;
  if (!Contains(content, x, y)) {
    return background;
  }

  const double u = (x - content.x) / content.width;
  const double v = (y - content.y) / content.height;
  const int source_x = std::clamp(
      static_cast<int>(std::floor(u * static_cast<double>(source.width))),
      0, source.width - 1);
  const int source_y = std::clamp(
      static_cast<int>(std::floor(v * static_cast<double>(source.height))),
      0, source.height - 1);
  return Composite(ReadPixel(source, source_x, source_y), background);
}

[[nodiscard]] Rgb8 BoxSample(ImageView source,
                             const RectD& content,
                             int sample_x,
                             int sample_y,
                             Rgb8 background) noexcept {
  const double output_x0 = static_cast<double>(sample_x);
  const double output_y0 = static_cast<double>(sample_y);
  const double output_x1 = output_x0 + 1.0;
  const double output_y1 = output_y0 + 1.0;

  const double ix0 = std::max(output_x0, content.x);
  const double iy0 = std::max(output_y0, content.y);
  const double ix1 = std::min(output_x1, content.x + content.width);
  const double iy1 = std::min(output_y1, content.y + content.height);
  if (ix1 <= ix0 || iy1 <= iy0) {
    return background;
  }

  const double image_fraction =
      std::clamp((ix1 - ix0) * (iy1 - iy0), 0.0, 1.0);

  const double sx0 =
      ((ix0 - content.x) / content.width) * static_cast<double>(source.width);
  const double sy0 =
      ((iy0 - content.y) / content.height) * static_cast<double>(source.height);
  const double sx1 =
      ((ix1 - content.x) / content.width) * static_cast<double>(source.width);
  const double sy1 =
      ((iy1 - content.y) / content.height) * static_cast<double>(source.height);

  const int first_x = std::clamp(static_cast<int>(std::floor(sx0)), 0,
                                 source.width - 1);
  const int last_x = std::clamp(static_cast<int>(std::ceil(sx1)) - 1, 0,
                                source.width - 1);
  const int first_y = std::clamp(static_cast<int>(std::floor(sy0)), 0,
                                 source.height - 1);
  const int last_y = std::clamp(static_cast<int>(std::ceil(sy1)) - 1, 0,
                                source.height - 1);

  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  double total_weight = 0.0;

  for (int y = first_y; y <= last_y; ++y) {
    const double overlap_y =
        std::max(0.0, std::min(sy1, static_cast<double>(y + 1)) -
                          std::max(sy0, static_cast<double>(y)));
    for (int x = first_x; x <= last_x; ++x) {
      const double overlap_x =
          std::max(0.0, std::min(sx1, static_cast<double>(x + 1)) -
                            std::max(sx0, static_cast<double>(x)));
      const double weight = overlap_x * overlap_y;
      if (weight <= 0.0) {
        continue;
      }

      const Rgb8 pixel = Composite(ReadPixel(source, x, y), background);
      red += static_cast<double>(pixel.r) * weight;
      green += static_cast<double>(pixel.g) * weight;
      blue += static_cast<double>(pixel.b) * weight;
      total_weight += weight;
    }
  }

  if (total_weight <= 0.0) {
    return background;
  }

  const double image_red = red / total_weight;
  const double image_green = green / total_weight;
  const double image_blue = blue / total_weight;
  const double background_fraction = 1.0 - image_fraction;

  const auto channel = [image_fraction, background_fraction](double image,
                                                              std::uint8_t bg) {
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(image * image_fraction +
                    static_cast<double>(bg) * background_fraction),
        0l, 255l));
  };

  return Rgb8{channel(image_red, background.r),
              channel(image_green, background.g),
              channel(image_blue, background.b)};
}

[[nodiscard]] Rgb8 Sample(ImageView source,
                          const RectD& content,
                          int sample_x,
                          int sample_y,
                          const RenderConfig& config) noexcept {
  if (config.filter == SamplingFilter::Nearest) {
    return NearestSample(source, content, sample_x, sample_y,
                         config.alpha_background);
  }
  return BoxSample(source, content, sample_x, sample_y,
                   config.alpha_background);
}

[[nodiscard]] std::size_t CheckedCellCount(RenderTarget target) {
  const auto columns = static_cast<std::size_t>(target.columns);
  const auto rows = static_cast<std::size_t>(target.rows);
  if (rows != 0 && columns > std::numeric_limits<std::size_t>::max() / rows) {
    throw std::overflow_error("terminal target dimensions overflow cell count");
  }
  return columns * rows;
}

}  // namespace

TerminalFrame RenderHalfBlock(ImageView source,
                              RenderTarget target,
                              const RenderConfig& config) {
  if (!source.valid()) {
    throw std::invalid_argument("RenderHalfBlock requires a valid image view");
  }
  if (target.columns <= 0 || target.rows <= 0) {
    throw std::invalid_argument("RenderHalfBlock target dimensions must be positive");
  }
  if (!std::isfinite(config.cell_aspect) || config.cell_aspect <= 0.0) {
    throw std::invalid_argument("RenderHalfBlock cell_aspect must be finite and positive");
  }

  const RectD content = ComputeContentRect(source, target, config);
  static const IdentityQuantizer kIdentity;
  const ColorQuantizer& quantizer =
      config.quantizer == nullptr ? static_cast<const ColorQuantizer&>(kIdentity)
                                  : *config.quantizer;

  std::vector<TerminalCell> cells;
  cells.reserve(CheckedCellCount(target));

  for (int row = 0; row < target.rows; ++row) {
    const int upper_sample_y = row * 2;
    const int lower_sample_y = upper_sample_y + 1;
    for (int column = 0; column < target.columns; ++column) {
      const Rgb8 upper = quantizer.Quantize(
          Sample(source, content, column, upper_sample_y, config));
      const Rgb8 lower = quantizer.Quantize(
          Sample(source, content, column, lower_sample_y, config));
      cells.push_back(TerminalCell{kUpperHalfBlock, upper, lower});
    }
  }

  return TerminalFrame(target.columns, target.rows, std::move(cells));
}

}  // namespace asciiomium::render
