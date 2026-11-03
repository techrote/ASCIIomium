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
inline thread_local std::ptrdiff_t remaining = -1;
inline thread_local std::size_t maximum_request = std::numeric_limits<std::size_t>::max();
inline thread_local std::size_t allocations = 0;
inline thread_local std::size_t largest_request = 0;
inline thread_local bool rejected = false;

class Scope {
 public:
  explicit Scope(std::ptrdiff_t allowed = -1,
      std::size_t maximum = std::numeric_limits<std::size_t>::max()) noexcept {
    remaining = allowed;
    maximum_request = maximum;
    allocations = largest_request = 0;
    rejected = false;
  }
  ~Scope() {
    remaining = -1;
    maximum_request = std::numeric_limits<std::size_t>::max();
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
}  // namespace allocation_test

ASCIIOMIUM_ALLOCATION_NOINLINE void* operator new(std::size_t size) {
  using namespace allocation_test;
  ++allocations;
  if (size > largest_request) largest_request = size;
  if (remaining == 0 || size > maximum_request) {
    rejected = true;
    throw std::bad_alloc();
  }
  if (remaining > 0) --remaining;
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
ASCIIOMIUM_ALLOCATION_NOINLINE void* operator new[](std::size_t size) { return ::operator new(size); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete(void* memory) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete[](void* memory) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
ASCIIOMIUM_ALLOCATION_NOINLINE void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#undef ASCIIOMIUM_ALLOCATION_NOINLINE
