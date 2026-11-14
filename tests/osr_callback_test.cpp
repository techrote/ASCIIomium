#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "allocation_failure.h"
#include "test_diagnostics.h"
#include "browser/fixture_observation.h"
#include "browser/osr_client.h"

namespace {
using namespace asciiomium::browser;
int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void PaintBoundary() {
  std::array<std::uint8_t, 64> pixels{};
  pixels.fill(127);
  const CefRenderHandler::RectList damage{
      CefRect(0, 0, 2, 2), CefRect(2, 0, 2, 2),
      CefRect(0, 2, 2, 2), CefRect(2, 2, 2, 2)};
  // Conversion, pixel reserve, damage reserve: each allocation site must be
  // contained by the real callback, without publishing an initial frame. Each
  // payload is 64 bytes; small Debug iterator-proxy allocations are not faults.
  static_assert(4 * sizeof(DirtyRect) == 64);
  for (std::ptrdiff_t allowed = 0; allowed < 3; ++allowed) {
    auto store = std::make_shared<SourceFrameStore>();
    CefRefPtr<OsrClient> client(new OsrClient(store, 4, 4));
    {
      allocation_test::Scope allocation(allowed, 128, allocation_test::kPayloadMinimumBytes);
      client->OnPaint(nullptr, PET_VIEW, damage, pixels.data(), 4, 4);
    }
    const auto failure = client->callback_status();
    Require(allocation_test::rejected && allocation_test::rejected_request == 64 &&
            allocation_test::eligible_allocations == static_cast<std::size_t>(allowed + 1) &&
            failure.first_failure == CallbackFailure::PaintAllocation &&
            failure.paint_failures == 1 && store->SnapshotState().view_generation == 0 &&
            !store->SnapshotView().valid(),
            "real OnPaint contains conversion and frame allocation failure");
    client->OnPaint(nullptr, PET_VIEW, damage, pixels.data(), 4, 4);
    Require(client->callback_status().suppressed_paints == 1 &&
            store->SnapshotState().view_generation == 0,
            "real OnPaint cannot quietly recover a failed qualification run");
  }

  auto store = std::make_shared<SourceFrameStore>();
  CefRefPtr<OsrClient> client(new OsrClient(store, 4, 4));
  {
    allocation_test::Scope allocation(-1, 128, allocation_test::kPayloadMinimumBytes);
    client->OnPaint(nullptr, PET_VIEW, damage, pixels.data(), 4, 4);
  }
  Require(!allocation_test::rejected && allocation_test::eligible_allocations == 3 &&
          !client->callback_failed() && store->SnapshotView().valid(),
          "normal real callback publishes after exactly three eligible buffer allocations");
  CefRenderHandler::RectList excessive(kMaxSourceDirtyRects + 1, CefRect(0, 0, 1, 1));
  {
    allocation_test::Scope allocation(-1, 128);
    client->OnPaint(nullptr, PET_VIEW, excessive, pixels.data(), 4, 4);
  }
  Require(!allocation_test::rejected && !client->callback_failed() &&
          store->SnapshotView().dirty_rects == std::vector<DirtyRect>{{0, 0, 4, 4}},
          "excess CEF damage collapses before allocating its full list");
  client->OnPaint(nullptr, PET_POPUP, damage, nullptr, 4, 4);
  Require(client->callback_status().first_failure == CallbackFailure::PaintInvalidInput &&
          !store->SnapshotPopup().has_pixels(), "invalid popup buffer is rejected inside callback");
}

void DiagnosticBoundary() {
  const std::u16string huge_text(1024 * 1024, u'x');
  const CefString huge(huge_text);
  const CefString empty;
  const CefString fixture_message(std::string(kFixtureObservationPrefix) + "ignored");
  auto store = std::make_shared<SourceFrameStore>();
  CefRefPtr<OsrClient> client(new OsrClient(store, 4, 4));
  bool consumed = false;
  {
    allocation_test::Scope allocation(0);
    consumed = client->OnConsoleMessage(nullptr, LOGSEVERITY_INFO, huge, empty, 0);
  }
  Require(consumed && !allocation_test::rejected && !client->callback_failed(),
          "disabled fixture console consumes huge input without allocation");

  BrowserRuntimeOptions runtime;
  runtime.fixture_observations = true;
  runtime.fixture_url = "file:///trusted-fixture.html";
  CefRefPtr<OsrClient> fixture_client(new OsrClient(store, 4, 4, runtime));
  {
    allocation_test::Scope allocation(0);
    fixture_client->OnConsoleMessage(nullptr, LOGSEVERITY_INFO, huge, empty, 0);
    fixture_client->OnConsoleMessage(nullptr, LOGSEVERITY_INFO, fixture_message, empty, 0);
  }
  Require(!allocation_test::rejected && !fixture_client->callback_failed() &&
          fixture_client->LatestFixtureObservation().empty(),
          "oversized or untrusted fixture console is discarded before conversion");

  {
    allocation_test::Scope allocation(-1, 16384);
    client->OnTitleChange(nullptr, huge);
  }
  const auto status = client->Status();
  Require(!allocation_test::rejected && !client->callback_failed() &&
          status.title == std::string(4096, 'x') && status.title_truncated,
          "title callback converts a bounded prefix of large native input");
  CefRefPtr<OsrClient> failing(new OsrClient(store, 4, 4));
  {
    allocation_test::Scope allocation(0, 16384, allocation_test::kPayloadMinimumBytes);
    failing->OnTitleChange(nullptr, huge);
  }
  Require(allocation_test::rejected && allocation_test::rejected_request >= 4096 &&
          allocation_test::eligible_allocations == 1 &&
          failing->callback_status().first_failure == CallbackFailure::DiagnosticAllocation &&
          failing->callback_status().diagnostic_failures == 1 && failing->MainTitle().empty(),
          "diagnostic conversion allocation failure is contained and visible");

  // CEF permits IO resource callbacks after OnBeforeClose. The scalar observer
  // must survive client release and see a late allocation failure in shared
  // Notice(), rather than treating the close notification as final success.
  CefRefPtr<OsrClient> late(new OsrClient(store, 4, 4));
  const auto observer = late->callback_state();
  late->OnBeforeClose(nullptr);
  bool escaped = false;
  bool allocation_rejected = false;
  std::size_t rejected_size = 0;
  std::size_t eligible_attempts = 0;
  bool allow_execution = true;
  std::thread io_callback([&] {
    allocation_test::Scope allocation(0, 2048, allocation_test::kPayloadMinimumBytes);
    try { late->OnProtocolExecution(nullptr, nullptr, nullptr, allow_execution); }
    catch (...) { escaped = true; }
    allocation_rejected = allocation_test::rejected;
    rejected_size = allocation_test::rejected_request;
    eligible_attempts = allocation_test::eligible_allocations;
  });
  io_callback.join();
  late = nullptr;
  Require(!escaped && allocation_rejected && rejected_size >= allocation_test::kPayloadMinimumBytes &&
          eligible_attempts == 1 && !allow_execution &&
          observer->Snapshot().first_failure == CallbackFailure::DiagnosticAllocation &&
          observer->Snapshot().diagnostic_failures == 1,
          "late IO Notice failure is contained and retained after client release");
}
}  // namespace

int main() {
  test_diagnostics::Configure();
  test_diagnostics::Stage("OSR paint boundaries");
  PaintBoundary();
  test_diagnostics::Stage("OSR diagnostic boundaries");
  DiagnosticBoundary();
  if (failures != 0) return 1;
  std::cout << "OSR callback tests passed (direct callback boundary; no browser initialized)\n";
  return 0;
}
