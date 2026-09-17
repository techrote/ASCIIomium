#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace asciiomium::browser {

struct DirtyRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  friend constexpr bool operator==(const DirtyRect&, const DirtyRect&) = default;
};

struct SourceFrameSnapshot {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> bgra;
  std::vector<DirtyRect> dirty_rects;
  std::uint64_t generation = 0;
  std::uint64_t paint_count = 0;
  std::uint8_t rgb_byte_min = 255;
  std::uint8_t rgb_byte_max = 0;
  std::uint8_t alpha_min = 255;
  std::uint8_t alpha_max = 0;

  [[nodiscard]] bool valid() const noexcept {
    return width > 0 && height > 0 &&
           bgra.size() == static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u;
  }

  [[nodiscard]] unsigned rgb_byte_span() const noexcept {
    return static_cast<unsigned>(rgb_byte_max) -
           static_cast<unsigned>(rgb_byte_min);
  }
};

struct PopupFrameSnapshot {
  bool visible = false;
  DirtyRect bounds{};
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> bgra;
  std::vector<DirtyRect> dirty_rects;
  std::uint64_t generation = 0;
  std::uint64_t paint_count = 0;

  [[nodiscard]] bool has_pixels() const noexcept {
    return width > 0 && height > 0 &&
           bgra.size() == static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height) * 4u;
  }
};

// Cheap metadata snapshot for the live scheduler. Reading this structure does
// not copy either pixel buffer, so the main loop can poll for new presentation
// work without copying a full CEF framebuffer every iteration.
struct SourceFrameState {
  std::uint64_t presentation_generation = 0;
  std::uint64_t view_generation = 0;
  std::uint64_t view_paint_count = 0;
  std::uint64_t popup_generation = 0;
  std::uint64_t popup_paint_count = 0;
  bool popup_visible = false;
  DirtyRect popup_bounds{};
};

// Coherent full snapshot used only when the scheduler has decided to render.
// View and popup are copied under one lock so future CEF threading changes
// cannot produce a frame assembled from two different presentation states.
struct SourcePresentationSnapshot {
  SourceFrameSnapshot view;
  PopupFrameSnapshot popup;
  std::uint64_t presentation_generation = 0;
};

// Thread-safe, bounded newest-frame storage for CEF OSR callbacks.
//
// The store never queues historical paint buffers. A new PET_VIEW paint replaces
// the previous view image in-place (resizing only when dimensions change), and a
// PET_POPUP paint does the same for one separately tracked popup image. This
// preserves interactive newest-frame semantics without allowing paint callbacks
// to build an unbounded backlog.
class SourceFrameStore {
 public:
  void UpdateView(const void* bgra,
                  int width,
                  int height,
                  const std::vector<DirtyRect>& dirty_rects);

  void SetPopupVisible(bool visible);
  void SetPopupBounds(DirtyRect bounds);
  void UpdatePopup(const void* bgra,
                   int width,
                   int height,
                   const std::vector<DirtyRect>& dirty_rects);

  [[nodiscard]] SourceFrameSnapshot SnapshotView() const;
  [[nodiscard]] PopupFrameSnapshot SnapshotPopup() const;
  [[nodiscard]] SourceFrameState SnapshotState() const;
  [[nodiscard]] SourcePresentationSnapshot SnapshotPresentation() const;

  [[nodiscard]] std::size_t ViewStorageCapacityBytes() const;
  [[nodiscard]] std::size_t PopupStorageCapacityBytes() const;

 private:
  mutable std::mutex mutex_;
  SourceFrameSnapshot view_;
  PopupFrameSnapshot popup_;
  std::uint64_t presentation_generation_ = 0;
};

}  // namespace asciiomium::browser
