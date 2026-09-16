#include <cstdint>
#include <iostream>
#include <vector>

#include "browser/source_frame.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using asciiomium::browser::DirtyRect;
  using asciiomium::browser::SourceFrameStore;

  SourceFrameStore store;
  const std::vector<std::uint8_t> first = {
      10, 20, 30, 255, 40, 50, 60, 128,
      70, 80, 90, 64, 100, 110, 120, 0};
  store.UpdateView(first.data(), 2, 2, {DirtyRect{0, 0, 1, 2}});

  auto snapshot = store.SnapshotView();
  Require(snapshot.valid(), "first view snapshot valid");
  Require(snapshot.width == 2 && snapshot.height == 2,
          "view dimensions preserved");
  Require(snapshot.bgra == first, "BGRA bytes copied without channel/origin transform");
  Require(snapshot.generation == 1 && snapshot.paint_count == 1,
          "first view generation and paint count");
  Require(snapshot.dirty_rects == std::vector<DirtyRect>{{0, 0, 1, 2}},
          "dirty rectangles preserved");
  Require(snapshot.rgb_byte_min == 10 && snapshot.rgb_byte_max == 120 &&
              snapshot.rgb_byte_span() == 110,
          "RGB byte range ignores alpha and measures visible variation");
  Require(snapshot.alpha_min == 0 && snapshot.alpha_max == 255,
          "alpha range derived from BGRA byte 3");

  std::vector<std::uint8_t> replacement(2 * 2 * 4, 0);
  for (std::size_t i = 0; i < replacement.size(); i += 4) {
    replacement[i + 0] = 3;
    replacement[i + 1] = 2;
    replacement[i + 2] = 1;
    replacement[i + 3] = 255;
  }
  store.UpdateView(replacement.data(), 2, 2, {DirtyRect{1, 1, 1, 1}});
  snapshot = store.SnapshotView();
  Require(snapshot.bgra == replacement, "newest view replaces older pixels");
  Require(snapshot.generation == 2 && snapshot.paint_count == 2,
          "view generation increments without queueing");
  Require(snapshot.rgb_byte_min == 1 && snapshot.rgb_byte_max == 3 &&
              snapshot.rgb_byte_span() == 2,
          "uniform-ish replacement has narrow RGB byte range");
  Require(snapshot.alpha_min == 255 && snapshot.alpha_max == 255,
          "opaque alpha range preserved");

  const auto warmed_capacity = store.ViewStorageCapacityBytes();
  for (int paint = 0; paint < 1000; ++paint) {
    replacement[0] = static_cast<std::uint8_t>(paint & 0xff);
    store.UpdateView(replacement.data(), 2, 2, {});
  }
  snapshot = store.SnapshotView();
  Require(snapshot.paint_count == 1002 && snapshot.generation == 1002,
          "repeated paints remain newest-frame generations");
  Require(snapshot.bgra.size() == 16, "repeated paints retain one fixed-size view buffer");
  Require(store.ViewStorageCapacityBytes() == warmed_capacity,
          "same-size paints do not grow view storage capacity");

  store.SetPopupVisible(true);
  store.SetPopupBounds(DirtyRect{5, 7, 2, 1});
  const std::vector<std::uint8_t> popup = {
      1, 2, 3, 255, 4, 5, 6, 255};
  store.UpdatePopup(popup.data(), 2, 1, {DirtyRect{0, 0, 2, 1}});
  auto popup_snapshot = store.SnapshotPopup();
  Require(popup_snapshot.visible, "popup visibility tracked separately");
  Require(popup_snapshot.bounds == DirtyRect{5, 7, 2, 1},
          "popup bounds tracked in view coordinates");
  Require(popup_snapshot.has_pixels() && popup_snapshot.bgra == popup,
          "popup BGRA pixels captured separately");
  Require(popup_snapshot.generation == 1 && popup_snapshot.paint_count == 1,
          "popup generation tracked");
  Require(store.SnapshotView().paint_count == 1002,
          "popup paint does not mutate view generation");

  store.SetPopupVisible(false);
  popup_snapshot = store.SnapshotPopup();
  Require(!popup_snapshot.visible, "popup hide tracked");
  Require(popup_snapshot.has_pixels(),
          "last popup pixels retained for diagnostic/composition handoff");

  if (failures != 0) {
    std::cerr << failures << " source-frame assertion(s) failed\n";
    return 1;
  }

  std::cout << "source-frame store tests passed\n";
  return 0;
}
