#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "browser/popup_compositor.h"
#include "browser/source_frame.h"
#include "runtime/frame_scheduler.h"
#include "runtime/timing_series.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestPacing(int fps) {
  using namespace std::chrono_literals;
  asciiomium::runtime::FrameScheduler scheduler(fps);
  auto now = asciiomium::runtime::FrameScheduler::TimePoint{};

  for (std::uint64_t millisecond = 0; millisecond < 1000; ++millisecond) {
    scheduler.ObserveSource(millisecond + 1);
    if (scheduler.Ready(now)) {
      scheduler.MarkRendered(now, millisecond + 1);
      scheduler.MarkEmitted();
    }
    now += 1ms;
  }

  const auto rendered = scheduler.stats().rendered_frames;
  Require(rendered >= static_cast<std::uint64_t>(fps - 1) &&
              rendered <= static_cast<std::uint64_t>(fps + 1),
          "one-second synthetic pacing stays close to configured FPS");
  Require(scheduler.stats().emitted_frames == rendered,
          "emitted count follows rendered count");
  Require(scheduler.stats().coalesced_source_generations > 0,
          "fast source updates are coalesced rather than queued");
}

void TestStaticAndForcedRefresh() {
  using namespace std::chrono_literals;
  asciiomium::runtime::FrameScheduler scheduler(20);
  auto now = asciiomium::runtime::FrameScheduler::TimePoint{};

  scheduler.ObserveSource(1);
  Require(scheduler.Ready(now), "first source generation is immediately ready");
  scheduler.MarkRendered(now, 1);
  scheduler.MarkEmitted();

  now += 500ms;
  Require(!scheduler.Ready(now),
          "static source does not schedule redundant terminal frames");

  scheduler.RequestRefresh();
  Require(scheduler.Ready(now), "explicit refresh can repaint a static source");
  scheduler.MarkRendered(now, 1);
  scheduler.MarkEmitted();
  Require(scheduler.stats().rendered_frames == 2,
          "forced refresh produces exactly one additional render");
  Require(scheduler.stats().coalesced_source_generations == 0,
          "forced refresh does not invent dropped source generations");
}

void TestGenerationCoalescing() {
  using namespace std::chrono_literals;
  asciiomium::runtime::FrameScheduler scheduler(10);
  auto now = asciiomium::runtime::FrameScheduler::TimePoint{};

  scheduler.ObserveSource(1);
  scheduler.MarkRendered(now, 1);
  scheduler.MarkEmitted();

  scheduler.ObserveSource(2);
  scheduler.ObserveSource(3);
  scheduler.ObserveSource(4);
  scheduler.ObserveSource(5);
  now += 100ms;
  Require(scheduler.Ready(now), "newest generation becomes ready at next slot");
  scheduler.MarkRendered(now, 5);
  scheduler.MarkEmitted();
  Require(scheduler.stats().coalesced_source_generations == 3,
          "intermediate generations are counted as coalesced");
  Require(scheduler.stats().last_rendered_source_generation == 5,
          "scheduler advances directly to newest rendered generation");
}

void TestPopupComposition() {
  using asciiomium::browser::ComposeViewAndPopup;
  using asciiomium::browser::DirtyRect;
  using asciiomium::browser::FitPopupBoundsToView;
  using asciiomium::browser::PopupFrameSnapshot;
  using asciiomium::browser::SourceFrameSnapshot;

  SourceFrameSnapshot view;
  view.width = 4;
  view.height = 2;
  view.bgra.assign(4u * 2u * 4u, 0);
  for (std::size_t i = 3; i < view.bgra.size(); i += 4) {
    view.bgra[i] = 255;
  }

  PopupFrameSnapshot popup;
  popup.visible = true;
  popup.bounds = DirtyRect{3, 0, 2, 1};
  popup.width = 2;
  popup.height = 1;
  popup.bgra = {
      0, 0, 255, 255,
      0, 128, 0, 128,
  };

  const auto fitted = FitPopupBoundsToView(popup.bounds, popup.width, popup.height,
                                           view.width, view.height);
  Require(fitted == DirtyRect{2, 0, 2, 1},
          "popup is repositioned to remain inside the view");

  const auto composed = ComposeViewAndPopup(view, popup);
  Require(composed.valid() && composed.popup_composited,
          "visible popup produces a valid composited frame");
  Require(composed.popup_bounds == fitted, "reported popup bounds are fitted bounds");

  const std::size_t opaque_offset = (0u * 4u + 2u) * 4u;
  Require(composed.bgra[opaque_offset + 0] == 0 &&
              composed.bgra[opaque_offset + 1] == 0 &&
              composed.bgra[opaque_offset + 2] == 255 &&
              composed.bgra[opaque_offset + 3] == 255,
          "opaque popup pixel overwrites destination BGRA");

  const std::size_t translucent_offset = (0u * 4u + 3u) * 4u;
  Require(composed.bgra[translucent_offset + 0] == 0 &&
              composed.bgra[translucent_offset + 1] == 128 &&
              composed.bgra[translucent_offset + 2] == 0 &&
              composed.bgra[translucent_offset + 3] == 255,
          "premultiplied popup alpha uses source-over composition");

  popup.visible = false;
  const auto hidden = ComposeViewAndPopup(view, popup);
  Require(!hidden.popup_composited && hidden.bgra == view.bgra,
          "hidden popup leaves base view unchanged");
}

void TestTimingSeries() {
  asciiomium::runtime::TimingSeries timing;
  for (int value = 1; value <= 100; ++value) {
    timing.AddMilliseconds(static_cast<double>(value));
  }
  Require(std::abs(timing.average_ms() - 50.5) < 0.0001,
          "timing average is cumulative and deterministic");
  Require(std::abs(timing.percentile_ms(0.95) - 96.0) < 0.0001,
          "timing p95 uses nearest-rank ceiling over retained samples");

  for (std::size_t i = 0; i < asciiomium::runtime::TimingSeries::kCapacity + 100;
       ++i) {
    timing.AddMilliseconds(1.0);
  }
  Require(timing.retained_count() == asciiomium::runtime::TimingSeries::kCapacity,
          "timing percentile history remains fixed-capacity");
}

}  // namespace

int main() {
  TestPacing(10);
  TestPacing(15);
  TestPacing(20);
  TestPacing(30);
  TestStaticAndForcedRefresh();
  TestGenerationCoalescing();
  TestPopupComposition();
  TestTimingSeries();

  if (failures != 0) {
    std::cerr << failures << " live-runtime assertion(s) failed\n";
    return 1;
  }

  std::cout << "live runtime tests passed\n";
  return 0;
}
