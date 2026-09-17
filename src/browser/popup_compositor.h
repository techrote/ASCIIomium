#pragma once

#include <cstdint>
#include <vector>

#include "browser/source_frame.h"

namespace asciiomium::browser {

struct CompositedSourceFrame {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> bgra;
  bool popup_composited = false;
  DirtyRect popup_bounds{};

  [[nodiscard]] bool valid() const noexcept {
    return width > 0 && height > 0 &&
           bgra.size() == static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u;
  }
};

// Match the established CEF OSR sample policy: keep popup widgets inside the
// visible browser view when possible, then clip any remaining oversized area.
[[nodiscard]] DirtyRect FitPopupBoundsToView(DirtyRect requested,
                                             int popup_width,
                                             int popup_height,
                                             int view_width,
                                             int view_height) noexcept;

// Copy the current PET_VIEW image and composite a visible PET_POPUP image over
// it. CEF/Chromium OSR popup texture values use premultiplied alpha, so source
// channels are combined using ONE, ONE_MINUS_SRC_ALPHA semantics. The function
// is deliberately browser-layer code; the generic terminal renderer continues
// to consume an ordinary BGRA image.
[[nodiscard]] CompositedSourceFrame ComposeViewAndPopup(
    const SourceFrameSnapshot& view,
    const PopupFrameSnapshot& popup);

}  // namespace asciiomium::browser
