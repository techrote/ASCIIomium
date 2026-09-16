#pragma once

#include "render/render_types.h"

namespace asciiomium::render {

inline constexpr char32_t kUpperHalfBlock = U'\u2580';

enum class SamplingFilter {
  Nearest,
  BoxAverage,
};

enum class FitMode {
  Stretch,
  Contain,
};

class ColorQuantizer {
 public:
  virtual ~ColorQuantizer() = default;
  [[nodiscard]] virtual Rgb8 Quantize(Rgb8 color) const noexcept = 0;
};

class IdentityQuantizer final : public ColorQuantizer {
 public:
  [[nodiscard]] Rgb8 Quantize(Rgb8 color) const noexcept override {
    return color;
  }
};

struct RenderTarget {
  int columns = 0;
  int rows = 0;
};

struct RenderConfig {
  SamplingFilter filter = SamplingFilter::BoxAverage;
  FitMode fit_mode = FitMode::Stretch;
  Rgb8 alpha_background{0, 0, 0};

  // Terminal cell width / cell height. It only changes geometry when an
  // aspect-preserving fit mode is selected; Stretch intentionally consumes
  // the complete source viewport.
  double cell_aspect = 0.5;

  // Null means identity/true-colour. Later palette issues can provide a
  // quantizer without changing the deterministic sampler/cell model.
  const ColorQuantizer* quantizer = nullptr;
};

[[nodiscard]] TerminalFrame RenderHalfBlock(ImageView source,
                                            RenderTarget target,
                                            const RenderConfig& config = {});

}  // namespace asciiomium::render
