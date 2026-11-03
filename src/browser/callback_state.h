#pragma once

#include <atomic>
#include <cstdint>
#include <new>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace asciiomium::browser {

enum class CallbackFailure : unsigned {
  None,
  PaintAllocation,
  PaintInvalidInput,
  PaintException,
  DiagnosticAllocation,
  DiagnosticException,
};

inline const char* CallbackFailureName(CallbackFailure failure) noexcept {
  switch (failure) {
    case CallbackFailure::None: return "none";
    case CallbackFailure::PaintAllocation: return "paint_allocation";
    case CallbackFailure::PaintInvalidInput: return "paint_invalid_input";
    case CallbackFailure::PaintException: return "paint_exception";
    case CallbackFailure::DiagnosticAllocation: return "diagnostic_allocation";
    case CallbackFailure::DiagnosticException: return "diagnostic_exception";
  }
  return "unknown";
}

struct CallbackStatus {
  CallbackFailure first_failure = CallbackFailure::None;
  std::uint64_t paint_failures = 0;
  std::uint64_t suppressed_paints = 0;
  std::uint64_t diagnostic_failures = 0;

  [[nodiscard]] bool failed() const noexcept {
    return first_failure != CallbackFailure::None;
  }
};

// The failure path uses only atomics: an allocation exception must not need
// another allocation to retain its cause. A paint failure is fatal to the run;
// the owner observes it after pumping CEF and follows ordinary close/cleanup.
// Concurrent snapshots are diagnostic observations, not transactional views of
// every counter; final reports retain an independent owner through CefShutdown
// and are collected after late IO callbacks have finished.
class CallbackState {
 public:
  [[nodiscard]] bool failed() const noexcept {
    return first_failure_.load(std::memory_order_acquire) != CallbackFailure::None;
  }

  [[nodiscard]] CallbackStatus Snapshot() const noexcept {
    return {first_failure_.load(std::memory_order_acquire),
            paint_failures_.load(std::memory_order_relaxed),
            suppressed_paints_.load(std::memory_order_relaxed),
            diagnostic_failures_.load(std::memory_order_relaxed)};
  }

  template <class Publish>
  void Paint(Publish&& publish) noexcept {
    if (failed()) {
      suppressed_paints_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    try {
      std::forward<Publish>(publish)();
    } catch (const std::bad_alloc&) {
      PaintFailure(CallbackFailure::PaintAllocation);
    } catch (const std::invalid_argument&) {
      PaintFailure(CallbackFailure::PaintInvalidInput);
    } catch (const std::overflow_error&) {
      PaintFailure(CallbackFailure::PaintInvalidInput);
    } catch (const std::length_error&) {
      PaintFailure(CallbackFailure::PaintInvalidInput);
    } catch (...) {
      PaintFailure(CallbackFailure::PaintException);
    }
  }

  template <class Update>
  bool Diagnostic(Update&& update) noexcept {
    if (failed()) return false;
    try {
      std::forward<Update>(update)();
      return true;
    } catch (const std::bad_alloc&) {
      DiagnosticFailure(CallbackFailure::DiagnosticAllocation);
    } catch (...) {
      DiagnosticFailure(CallbackFailure::DiagnosticException);
    }
    return false;
  }

 private:
  void Latch(CallbackFailure failure) noexcept {
    auto expected = CallbackFailure::None;
    first_failure_.compare_exchange_strong(expected, failure,
        std::memory_order_release, std::memory_order_relaxed);
  }

  void PaintFailure(CallbackFailure failure) noexcept {
    paint_failures_.fetch_add(1, std::memory_order_relaxed);
    Latch(failure);
  }

  void DiagnosticFailure(CallbackFailure failure) noexcept {
    diagnostic_failures_.fetch_add(1, std::memory_order_relaxed);
    Latch(failure);
  }

  std::atomic<CallbackFailure> first_failure_{CallbackFailure::None};
  std::atomic<std::uint64_t> paint_failures_{0};
  std::atomic<std::uint64_t> suppressed_paints_{0};
  std::atomic<std::uint64_t> diagnostic_failures_{0};
};

inline void WriteCallbackStatusJson(std::ostream& out, const CallbackStatus& status) {
  out << "{\"failed\":" << (status.failed() ? "true" : "false")
      << ",\"first_failure\":\"" << CallbackFailureName(status.first_failure)
      << "\",\"paint_failures\":" << status.paint_failures
      << ",\"suppressed_paints\":" << status.suppressed_paints
      << ",\"diagnostic_failures\":" << status.diagnostic_failures << '}';
}

}  // namespace asciiomium::browser
