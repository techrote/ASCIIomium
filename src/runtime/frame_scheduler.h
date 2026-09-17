#pragma once

#include <chrono>
#include <cstdint>

namespace asciiomium::runtime {

struct FrameSchedulerStats {
  std::uint64_t latest_source_generation = 0;
  std::uint64_t last_rendered_source_generation = 0;
  std::uint64_t rendered_frames = 0;
  std::uint64_t emitted_frames = 0;
  std::uint64_t coalesced_source_generations = 0;
  std::uint64_t forced_refreshes = 0;
};

// Main-loop frame pacing for an interactive newest-frame renderer.
//
// The scheduler stores only generation counters. It never owns browser frames
// and therefore cannot turn CEF callbacks into a growing FIFO. The caller polls
// SourceFrameStore metadata, observes the latest presentation generation and
// snapshots pixels only when Ready() is true.
class FrameScheduler {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit FrameScheduler(int max_fps);

  void ObserveSource(std::uint64_t generation) noexcept;
  void RequestRefresh() noexcept;

  [[nodiscard]] bool Ready(TimePoint now) const noexcept;
  [[nodiscard]] std::uint64_t latest_source_generation() const noexcept {
    return stats_.latest_source_generation;
  }
  [[nodiscard]] int max_fps() const noexcept { return max_fps_; }
  [[nodiscard]] std::chrono::nanoseconds frame_interval() const noexcept {
    return frame_interval_;
  }

  // Call after conversion has completed. The next slot is measured from the
  // completion time to avoid catch-up bursts after an expensive frame.
  void MarkRendered(TimePoint completed_at,
                    std::uint64_t rendered_source_generation) noexcept;
  void MarkEmitted() noexcept;

  [[nodiscard]] const FrameSchedulerStats& stats() const noexcept {
    return stats_;
  }

 private:
  int max_fps_ = 0;
  std::chrono::nanoseconds frame_interval_{};
  TimePoint next_allowed_render_{};
  bool has_rendered_ = false;
  bool forced_refresh_pending_ = false;
  FrameSchedulerStats stats_{};
};

}  // namespace asciiomium::runtime
