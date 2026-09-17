#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace asciiomium::runtime {

// Fixed-memory timing history. Average is cumulative over the complete run;
// percentile is calculated over the most recent kCapacity samples so an
// indefinite browser session cannot grow diagnostics memory without bound.
class TimingSeries {
 public:
  static constexpr std::size_t kCapacity = 2048;

  void AddMilliseconds(double milliseconds) noexcept;

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
  [[nodiscard]] std::size_t retained_count() const noexcept { return size_; }
  [[nodiscard]] double average_ms() const noexcept;
  [[nodiscard]] double percentile_ms(double percentile) const;

 private:
  std::array<double, kCapacity> samples_{};
  std::size_t size_ = 0;
  std::size_t next_ = 0;
  std::uint64_t count_ = 0;
  long double sum_ms_ = 0.0L;
};

}  // namespace asciiomium::runtime
