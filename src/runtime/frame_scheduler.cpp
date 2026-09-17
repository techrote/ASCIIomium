#include "runtime/frame_scheduler.h"

#include <stdexcept>

namespace asciiomium::runtime {

FrameScheduler::FrameScheduler(int max_fps) : max_fps_(max_fps) {
  if (max_fps_ <= 0 || max_fps_ > 240) {
    throw std::invalid_argument("frame scheduler max_fps must be in 1..240");
  }
  frame_interval_ = std::chrono::nanoseconds(1'000'000'000LL / max_fps_);
}

void FrameScheduler::ObserveSource(std::uint64_t generation) noexcept {
  if (generation > stats_.latest_source_generation) {
    stats_.latest_source_generation = generation;
  }
}

void FrameScheduler::RequestRefresh() noexcept {
  if (!forced_refresh_pending_) {
    forced_refresh_pending_ = true;
    ++stats_.forced_refreshes;
  }
}

bool FrameScheduler::Ready(TimePoint now) const noexcept {
  const bool has_new_source = stats_.latest_source_generation >
                              stats_.last_rendered_source_generation;
  if (!has_new_source && !forced_refresh_pending_) {
    return false;
  }
  return !has_rendered_ || now >= next_allowed_render_;
}

void FrameScheduler::MarkRendered(
    TimePoint completed_at,
    std::uint64_t rendered_source_generation) noexcept {
  if (rendered_source_generation > stats_.last_rendered_source_generation) {
    const std::uint64_t previous = stats_.last_rendered_source_generation;
    if (rendered_source_generation > previous + 1) {
      stats_.coalesced_source_generations +=
          rendered_source_generation - previous - 1;
    }
    stats_.last_rendered_source_generation = rendered_source_generation;
  }

  ++stats_.rendered_frames;
  forced_refresh_pending_ = false;
  has_rendered_ = true;
  next_allowed_render_ = completed_at + frame_interval_;
}

void FrameScheduler::MarkEmitted() noexcept {
  ++stats_.emitted_frames;
}

}  // namespace asciiomium::runtime
