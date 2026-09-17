#include "browser/source_frame.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace asciiomium::browser {
namespace {

std::size_t CheckedByteCount(int width, int height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("source frame dimensions must be positive");
  }
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  if (w > static_cast<std::size_t>(-1) / h / 4u) {
    throw std::overflow_error("source frame byte count overflow");
  }
  return w * h * 4u;
}

void CopyPixels(std::vector<std::uint8_t>* destination,
                const void* source,
                std::size_t byte_count) {
  if (source == nullptr) {
    throw std::invalid_argument("source frame buffer must not be null");
  }
  destination->resize(byte_count);
  std::memcpy(destination->data(), source, byte_count);
}

}  // namespace

void SourceFrameStore::UpdateView(
    const void* bgra,
    int width,
    int height,
    const std::vector<DirtyRect>& dirty_rects) {
  const std::size_t byte_count = CheckedByteCount(width, height);

  std::lock_guard lock(mutex_);
  CopyPixels(&view_.bgra, bgra, byte_count);
  view_.width = width;
  view_.height = height;
  view_.dirty_rects = dirty_rects;
  ++view_.generation;
  ++view_.paint_count;
  ++presentation_generation_;

  std::uint8_t rgb_min = 255;
  std::uint8_t rgb_max = 0;
  std::uint8_t alpha_min = 255;
  std::uint8_t alpha_max = 0;
  for (std::size_t offset = 0; offset < byte_count; offset += 4) {
    rgb_min = std::min(rgb_min, view_.bgra[offset + 0]);
    rgb_min = std::min(rgb_min, view_.bgra[offset + 1]);
    rgb_min = std::min(rgb_min, view_.bgra[offset + 2]);
    rgb_max = std::max(rgb_max, view_.bgra[offset + 0]);
    rgb_max = std::max(rgb_max, view_.bgra[offset + 1]);
    rgb_max = std::max(rgb_max, view_.bgra[offset + 2]);
    alpha_min = std::min(alpha_min, view_.bgra[offset + 3]);
    alpha_max = std::max(alpha_max, view_.bgra[offset + 3]);
  }
  view_.rgb_byte_min = rgb_min;
  view_.rgb_byte_max = rgb_max;
  view_.alpha_min = alpha_min;
  view_.alpha_max = alpha_max;
}

void SourceFrameStore::SetPopupVisible(bool visible) {
  std::lock_guard lock(mutex_);
  if (popup_.visible == visible) {
    return;
  }
  popup_.visible = visible;
  if (!visible) {
    popup_.dirty_rects.clear();
  }
  ++presentation_generation_;
}

void SourceFrameStore::SetPopupBounds(DirtyRect bounds) {
  std::lock_guard lock(mutex_);
  if (popup_.bounds == bounds) {
    return;
  }
  popup_.bounds = bounds;
  // Bounds are presentation state only while the popup is visible. CEF may
  // update bookkeeping for a hidden popup; that must not wake an otherwise
  // static terminal frame loop.
  if (popup_.visible) {
    ++presentation_generation_;
  }
}

void SourceFrameStore::UpdatePopup(
    const void* bgra,
    int width,
    int height,
    const std::vector<DirtyRect>& dirty_rects) {
  const std::size_t byte_count = CheckedByteCount(width, height);

  std::lock_guard lock(mutex_);
  CopyPixels(&popup_.bgra, bgra, byte_count);
  popup_.width = width;
  popup_.height = height;
  popup_.dirty_rects = dirty_rects;
  ++popup_.generation;
  ++popup_.paint_count;
  ++presentation_generation_;
}

SourceFrameSnapshot SourceFrameStore::SnapshotView() const {
  std::lock_guard lock(mutex_);
  return view_;
}

PopupFrameSnapshot SourceFrameStore::SnapshotPopup() const {
  std::lock_guard lock(mutex_);
  return popup_;
}

SourceFrameState SourceFrameStore::SnapshotState() const {
  std::lock_guard lock(mutex_);
  return SourceFrameState{
      presentation_generation_,
      view_.generation,
      view_.paint_count,
      popup_.generation,
      popup_.paint_count,
      popup_.visible,
      popup_.bounds,
  };
}

SourcePresentationSnapshot SourceFrameStore::SnapshotPresentation() const {
  std::lock_guard lock(mutex_);
  return SourcePresentationSnapshot{view_, popup_, presentation_generation_};
}

std::size_t SourceFrameStore::ViewStorageCapacityBytes() const {
  std::lock_guard lock(mutex_);
  return view_.bgra.capacity();
}

std::size_t SourceFrameStore::PopupStorageCapacityBytes() const {
  std::lock_guard lock(mutex_);
  return popup_.bgra.capacity();
}

}  // namespace asciiomium::browser
