#include "browser/popup_compositor.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace asciiomium::browser {
namespace {

[[nodiscard]] std::uint8_t PremultipliedOver(std::uint8_t source,
                                             std::uint8_t source_alpha,
                                             std::uint8_t destination) noexcept {
  const unsigned inverse = 255u - static_cast<unsigned>(source_alpha);
  const unsigned contribution =
      (static_cast<unsigned>(destination) * inverse + 127u) / 255u;
  return static_cast<std::uint8_t>(
      std::min(255u, static_cast<unsigned>(source) + contribution));
}

[[nodiscard]] std::uint8_t AlphaOver(std::uint8_t source_alpha,
                                     std::uint8_t destination_alpha) noexcept {
  const unsigned inverse = 255u - static_cast<unsigned>(source_alpha);
  const unsigned destination =
      (static_cast<unsigned>(destination_alpha) * inverse + 127u) / 255u;
  return static_cast<std::uint8_t>(
      std::min(255u, static_cast<unsigned>(source_alpha) + destination));
}

}  // namespace

DirtyRect FitPopupBoundsToView(DirtyRect requested,
                               int popup_width,
                               int popup_height,
                               int view_width,
                               int view_height) noexcept {
  if (popup_width <= 0 || popup_height <= 0 || view_width <= 0 ||
      view_height <= 0) {
    return DirtyRect{};
  }

  // Both dimensions are positive, so subtraction cannot overflow. Avoid
  // requested coordinate + dimension, which can exceed INT_MAX.
  const int x = std::clamp(requested.x, 0, std::max(0, view_width - popup_width));
  const int y = std::clamp(requested.y, 0, std::max(0, view_height - popup_height));

  return DirtyRect{x, y, popup_width, popup_height};
}

CompositedSourceFrame ComposeViewAndPopup(const SourceFrameSnapshot& view,
                                          const PopupFrameSnapshot& popup) {
  if (!view.valid()) {
    throw std::invalid_argument("popup compositor requires a valid view frame");
  }

  CompositedSourceFrame result;
  result.width = view.width;
  result.height = view.height;
  result.bgra = view.bgra;

  if (!popup.visible || !popup.has_pixels()) {
    return result;
  }

  const DirtyRect fitted = FitPopupBoundsToView(
      popup.bounds, popup.width, popup.height, view.width, view.height);
  if (fitted.width <= 0 || fitted.height <= 0) {
    return result;
  }

  const int copy_width = std::min(fitted.width, view.width - fitted.x);
  const int copy_height = std::min(fitted.height, view.height - fitted.y);
  if (copy_width <= 0 || copy_height <= 0) {
    return result;
  }

  for (int popup_y = 0; popup_y < copy_height; ++popup_y) {
    for (int popup_x = 0; popup_x < copy_width; ++popup_x) {
      const auto source_offset =
          (static_cast<std::size_t>(popup_y) *
               static_cast<std::size_t>(popup.width) +
           static_cast<std::size_t>(popup_x)) *
          4u;
      const auto destination_offset =
          (static_cast<std::size_t>(fitted.y + popup_y) *
               static_cast<std::size_t>(view.width) +
           static_cast<std::size_t>(fitted.x + popup_x)) *
          4u;

      const std::uint8_t alpha = popup.bgra[source_offset + 3];
      if (alpha == 0) {
        continue;
      }
      if (alpha == 255) {
        result.bgra[destination_offset + 0] = popup.bgra[source_offset + 0];
        result.bgra[destination_offset + 1] = popup.bgra[source_offset + 1];
        result.bgra[destination_offset + 2] = popup.bgra[source_offset + 2];
        result.bgra[destination_offset + 3] = 255;
        continue;
      }

      result.bgra[destination_offset + 0] = PremultipliedOver(
          popup.bgra[source_offset + 0], alpha,
          result.bgra[destination_offset + 0]);
      result.bgra[destination_offset + 1] = PremultipliedOver(
          popup.bgra[source_offset + 1], alpha,
          result.bgra[destination_offset + 1]);
      result.bgra[destination_offset + 2] = PremultipliedOver(
          popup.bgra[source_offset + 2], alpha,
          result.bgra[destination_offset + 2]);
      result.bgra[destination_offset + 3] = AlphaOver(
          alpha, result.bgra[destination_offset + 3]);
    }
  }

  result.popup_composited = true;
  result.popup_bounds = fitted;
  return result;
}

}  // namespace asciiomium::browser
