#pragma once

#include "geometry/viewport_transform.h"
#include "render/color_quantizer.h"
#include "render/render_types.h"

namespace asciiomium::render {

inline constexpr char32_t kUpperHalfBlock = U'\u2580';

enum class SamplingFilter {
  Nearest,
  BoxAverage,
};

// Compatibility alias: geometry owns fit policy so rendering and later input
// consume the same definition rather than inventing parallel viewport models.
using FitMode = asciiomium::geometry::FitMode;

struct RenderTarget {
  int columns = 0;
  int rows = 0;
};

struct RenderConfig {
  SamplingFilter filter = SamplingFilter::BoxAverage;
  FitMode fit_mode = FitMode::Stretch;
  Rgb8 alpha_background{0, 0, 0};

  // Terminal cell width / cell height. The convenience RenderTarget overload
  // uses this to construct a ViewportTransform. The authoritative-transform
  // overload uses geometry already frozen in the supplied transform.
  double cell_aspect = 0.5;

  // Null means identity/true-colour. Indexed modes keep their palette index in
  // TerminalColor while 512/1024 remain quantised RGB values.
  const ColorQuantizer* quantizer = nullptr;
};

// Convenience/reference path used by offline tools and older tests. Geometry is
// still computed by the shared ViewportTransform implementation.
[[nodiscard]] TerminalFrame RenderHalfBlock(ImageView source,
                                            RenderTarget target,
                                            const RenderConfig& config = {});

// Authoritative live/input-aligned path. The transform fixes terminal geometry,
// browser viewport, fit policy and cell aspect for the whole frame.
[[nodiscard]] TerminalFrame RenderHalfBlock(
    ImageView source,
    const asciiomium::geometry::ViewportTransform& transform,
    const RenderConfig& config = {});

}  // namespace asciiomium::render
