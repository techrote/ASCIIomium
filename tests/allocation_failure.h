#pragma once

// Include once per test executable. The scope changes allocations only on the
// current thread; production code contains no allocator hook or test switch.
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

#if defined(_MSC_VER)
#define ASCIIOMIUM_ALLOCATION_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define ASCIIOMIUM_ALLOCATION_NOINLINE __attribute__((noinline))
#else
#define ASCIIOMIUM_ALLOCATION_NOINLINE
#endif

namespace allocation_test {
// Payload-failure tests may exempt tiny implementation bookkeeping (MSVC
// Debug iterator proxies allocate inside noexcept container constructors).
// Strict no-allocation tests retain the default zero threshold. The maximum
// request limit always applies, including to requests below this threshold.
inline constexpr std::size_t kPayloadMinimumBytes = 32;
inline thread_local bool active = false;
inline thread_local std::ptrdiff_t remaining = -1;
inline thread_local std::size_t minimum_failure_request = 0;
inline thread_local std::size_t maximum_request = std::numeric_limits<std::size_t>::max();
inline thread_local std::size_t allocations = 0;
inline thread_local std::size_t eligible_allocations = 0;
inline thread_local std::size_t largest_request = 0;
inline thread_local std::size_t rejected_request = 0;
inline thread_local bool rejected = false;

class Scope {
 public:
  explicit Scope(std::ptrdiff_t allowed = -1,
      std::size_t maximum = std::numeric_limits<std::size_t>::max(),
      std::size_t minimum = 0) noexcept {
    active = true;
    remaining = allowed;
    minimum_failure_request = minimum;
    maximum_request = maximum;
    allocations = eligible_allocations = largest_request = rejected_request = 0;
    rejected = false;
  }
  ~Scope() {
    active = false;
    remaining = -1;
    minimum_failure_request = 0;
    maximum_request = std::numeric_limits<std::size_t>::max();
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
}  // namespace allocation_test

ASCIIOMIUM_ALLOCATION_NOINLINE void* operator new(std::size_t size) {
  using namespace allocation_test;
  if (active) {
    ++allocations;
    if (size > largest_request) largest_request = size;
    const bool eligible = size >= minimum_failure_request;
    if (eligible) ++eligible_allocations;
    if (size > maximum_request || (eligible && remaining == 0)) {
      rejected = true;
      rejected_request = size;
      throw std::bad_alloc();
    }
    if (eligible && remaining > 0) --remaining;
  }
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
ASCIIOMIUM_ALLOCATION_NOINLINE void* operator new[](std::size_t size) { return ::operator new(size); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete(void* memory) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete[](void* memory) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#undef ASCIIOMIUM_ALLOCATION_NOINLINE
