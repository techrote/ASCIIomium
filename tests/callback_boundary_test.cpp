#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "allocation_failure.h"
#include "browser/bounded_text.h"
#include "browser/browser_commands.h"
#include "browser/callback_state.h"
#include "browser/source_frame.h"

namespace {
using namespace asciiomium::browser;
int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool SameView(const SourceFrameSnapshot& a, const SourceFrameSnapshot& b) {
  return a.width == b.width && a.height == b.height && a.bgra == b.bgra &&
         a.dirty_rects == b.dirty_rects && a.generation == b.generation &&
         a.paint_count == b.paint_count && a.rgb_byte_min == b.rgb_byte_min &&
         a.rgb_byte_max == b.rgb_byte_max && a.alpha_min == b.alpha_min &&
         a.alpha_max == b.alpha_max;
}

bool SamePopup(const PopupFrameSnapshot& a, const PopupFrameSnapshot& b) {
  return a.width == b.width && a.height == b.height && a.bgra == b.bgra &&
         a.dirty_rects == b.dirty_rects && a.generation == b.generation &&
         a.paint_count == b.paint_count && a.visible == b.visible && a.bounds == b.bounds;
}

void FramePublication() {
  const std::vector<std::uint8_t> old_pixels(16, 1), new_pixels(32, 200);
  const std::vector<DirtyRect> old_damage{{0, 0, 2, 2}};
  const std::vector<DirtyRect> new_damage{{0, 0, 1, 4}, {0, 4, 1, 4}};
  for (const bool popup : {false, true}) {
    for (const std::ptrdiff_t allowed : {0, 1}) {
      SourceFrameStore store;
      store.UpdateView(old_pixels.data(), 2, 2, old_damage);
      store.SetPopupVisible(true);
      store.SetPopupBounds({1, 2, 2, 2});
      store.UpdatePopup(old_pixels.data(), 2, 2, old_damage);
      const auto view = store.SnapshotView();
      const auto pop = store.SnapshotPopup();
      const auto generation = store.SnapshotState().presentation_generation;
      bool threw = false;
      {
        allocation_test::Scope allocation(allowed);
        try {
          if (popup) store.UpdatePopup(new_pixels.data(), 1, 8, new_damage);
          else store.UpdateView(new_pixels.data(), 1, 8, new_damage);
        } catch (const std::bad_alloc&) { threw = true; }
      }
      Require(threw, "both pixel-reserve and damage-reserve failures exercised");
      Require(SameView(store.SnapshotView(), view), "failed update preserves complete view");
      Require(SamePopup(store.SnapshotPopup(), pop), "failed update preserves complete popup");
      Require(store.SnapshotState().presentation_generation == generation,
              "failed allocation does not publish a generation");
      if (popup) store.UpdatePopup(new_pixels.data(), 1, 8, new_damage);
      else store.UpdateView(new_pixels.data(), 1, 8, new_damage);
      Require(store.SnapshotState().presentation_generation == generation + 1,
              "next successful update publishes exactly once");
      Require(popup ? store.SnapshotPopup().bgra == new_pixels :
                      store.SnapshotView().bgra == new_pixels,
              "recovered update publishes actual new pixels");
    }
  }

  // This is the original regression shape: no pixel allocation is needed, but
  // dimensions change and the larger damage list fails to allocate.
  SourceFrameStore store;
  store.UpdateView(old_pixels.data(), 2, 2, old_damage);
  const auto before = store.SnapshotView();
  bool threw = false;
  {
    allocation_test::Scope allocation(0);
    try { store.UpdateView(new_pixels.data(), 1, 4, new_damage); }
    catch (const std::bad_alloc&) { threw = true; }
  }
  Require(threw && SameView(store.SnapshotView(), before),
          "same-byte-count failed damage growth cannot change dimensions or pixels");
  {
    allocation_test::Scope allocation(0);
    store.UpdateView(old_pixels.data(), 2, 2, old_damage);
  }
  Require(!allocation_test::rejected, "warmed same-size updates allocate nothing");
  const auto valid = store.SnapshotView();
  for (int invalid = 0; invalid < 3; ++invalid) {
    bool rejected = false;
    try {
      store.UpdateView(invalid == 0 ? nullptr : old_pixels.data(),
                       invalid == 1 ? 0 : 2, invalid == 2 ? -1 : 2, old_damage);
    } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected && SameView(store.SnapshotView(), valid),
            "invalid pointer/dimensions leave committed state intact");
  }

  std::vector<DirtyRect> damage(kMaxSourceDirtyRects, DirtyRect{0, 0, 1, 1});
  store.UpdateView(old_pixels.data(), 2, 2, damage);
  Require(store.SnapshotView().dirty_rects == damage, "damage list at bound preserved");
  damage.push_back({1, 1, 1, 1});
  store.UpdateView(old_pixels.data(), 2, 2, damage);
  store.UpdatePopup(old_pixels.data(), 2, 2, damage);
  const std::vector<DirtyRect> whole{{0, 0, 2, 2}};
  Require(store.SnapshotView().dirty_rects == whole &&
          store.SnapshotPopup().dirty_rects == whole &&
          store.SnapshotView().bgra == old_pixels && store.SnapshotPopup().bgra == old_pixels,
          "excess view/popup damage collapses while preserving complete pixels");
}

void CallbackFailures() {
  CallbackState state;
  bool published = false;
  state.Paint([&] { published = true; });
  Require(published && !state.failed(), "successful paint has no failure");
  {
    allocation_test::Scope allocation(0);
    state.Paint([] { std::vector<int> impossible(32); (void)impossible; });
  }
  auto status = state.Snapshot();
  Require(status.first_failure == CallbackFailure::PaintAllocation &&
          status.paint_failures == 1 && status.suppressed_paints == 0,
          "allocation failure contained and retained without allocating");
  published = false;
  state.Paint([&] { published = true; });
  Require(!published && state.Snapshot().suppressed_paints == 1,
          "fatal latch suppresses later frame publication");
  Require(!state.Diagnostic([&] { published = true; }) && !published,
          "failed run rejects later diagnostic mutations");

  CallbackState invalid;
  SourceFrameStore store;
  invalid.Paint([&] { store.UpdateView(nullptr, 1, 1, {}); });
  Require(invalid.Snapshot().first_failure == CallbackFailure::PaintInvalidInput &&
          store.SnapshotState().view_generation == 0,
          "invalid paint rejected without a generation");
  CallbackState unexpected;
  unexpected.Paint([] { throw 42; });
  Require(unexpected.Snapshot().first_failure == CallbackFailure::PaintException,
          "unknown exception cannot escape paint boundary");

  CallbackState diagnostic;
  bool updated = true;
  {
    allocation_test::Scope allocation(0);
    updated = diagnostic.Diagnostic([] { std::vector<int> impossible(32); (void)impossible; });
  }
  Require(!updated && diagnostic.Snapshot().first_failure == CallbackFailure::DiagnosticAllocation &&
          diagnostic.Snapshot().diagnostic_failures == 1,
          "diagnostic allocation failure remains observable");
  diagnostic.Paint([] {});
  Require(diagnostic.Snapshot().suppressed_paints == 1,
          "diagnostic failure also stops accepting paints");
  std::ostringstream json;
  WriteCallbackStatusJson(json, diagnostic.Snapshot());
  Require(json.str() == "{\"failed\":true,\"first_failure\":\"diagnostic_allocation\",\"paint_failures\":0,\"suppressed_paints\":1,\"diagnostic_failures\":1}",
          "callback evidence is a complete scalar-only JSON object");
}

void BoundedDiagnostics() {
  const std::u16string huge(1024 * 1024, u'x');
  BoundedText bounded;
  {
    allocation_test::Scope allocation(-1, 16384);
    bounded = BoundedUtf16Text(huge, 4096);
  }
  Require(!allocation_test::rejected && bounded.text == std::string(4096, 'x') && bounded.truncated,
          "large diagnostic converts only its bounded prefix");
  Require(BoundedUtf16Text(u"", 0).text.empty() && !BoundedUtf16Text(u"", 0).truncated &&
          BoundedUtf16Text(u"x", 0).truncated, "zero output budget is well-defined");

  const std::u16string text = u"A\u00e9\u4e2d\U0001f680Z";
  const std::string complete = "A\xc3\xa9\xe4\xb8\xad\xf0\x9f\x9a\x80" "Z";
  const std::array<std::size_t, 12> expected_sizes{0, 1, 1, 3, 3, 3, 6, 6, 6, 6, 10, 11};
  for (std::size_t limit = 0; limit < expected_sizes.size(); ++limit) {
    const auto converted = BoundedUtf16Text(text, limit);
    Require(converted.text == complete.substr(0, expected_sizes[limit]) &&
            converted.truncated == (expected_sizes[limit] != complete.size()),
            "byte limits do not split two/three/four-byte UTF-8 scalars");
  }
  const std::u16string malformed{char16_t{0xd800}, u'X', char16_t{0xdc00}};
  Require(BoundedUtf16Text(malformed, 32).text == "\xef\xbf\xbd" "X\xef\xbf\xbd",
          "isolated UTF-16 surrogates become replacement scalars");

  std::u16string url = u"https://example.test/";
  url.append(8192 - url.size(), u'a');
  Require(IsAllowedNavigationUtf16(url), "complete URL at byte bound accepted");
  url += u'?';
  Require(!IsAllowedNavigationUtf16(url), "overlong native URL cannot validate a clipped prefix");
  url = u"https://example.test/";
  url.append(3000, u'\u4e2d');
  Require(!IsAllowedNavigationUtf16(url), "UTF-8 byte overflow rejected despite fitting native length");
  Require(!IsAllowedNavigationUtf16(u"javascript:alert(1)") &&
          !IsAllowedNavigationUtf16(u"https://example.test/\n") &&
          IsAllowedNavigationUtf16(u"https://example.test/\u4e2d"),
          "bounded conversion retains navigation policy");
  std::u16string credential_url = u"https://user:secret";
  credential_url.append(8192, u'x');
  credential_url += u"@example.test/path";
  const auto clipped = BoundedUtf16Text(credential_url, 8192);
  Require(clipped.truncated &&
          DiagnosticOrigin(clipped.text, !clipped.truncated) == "[redacted]",
          "truncated authority cannot expose a credential prefix as an origin");
}
}  // namespace

int main() {
  FramePublication();
  CallbackFailures();
  BoundedDiagnostics();
  if (failures != 0) return 1;
  std::cout << "callback boundary tests passed\n";
  return 0;
}
