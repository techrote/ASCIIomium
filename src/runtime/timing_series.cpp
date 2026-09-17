#include "runtime/timing_series.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace asciiomium::runtime {

void TimingSeries::AddMilliseconds(double milliseconds) noexcept {
  if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
    return;
  }
  samples_[next_] = milliseconds;
  next_ = (next_ + 1) % kCapacity;
  if (size_ < kCapacity) {
    ++size_;
  }
  ++count_;
  sum_ms_ += static_cast<long double>(milliseconds);
}

double TimingSeries::average_ms() const noexcept {
  if (count_ == 0) {
    return 0.0;
  }
  return static_cast<double>(sum_ms_ / static_cast<long double>(count_));
}

double TimingSeries::percentile_ms(double percentile) const {
  if (!std::isfinite(percentile) || percentile < 0.0 || percentile > 1.0) {
    throw std::invalid_argument("timing percentile must be in [0,1]");
  }
  if (size_ == 0) {
    return 0.0;
  }

  std::vector<double> ordered;
  ordered.reserve(size_);
  for (std::size_t i = 0; i < size_; ++i) {
    ordered.push_back(samples_[i]);
  }
  std::sort(ordered.begin(), ordered.end());

  const double scaled = percentile * static_cast<double>(ordered.size() - 1);
  const std::size_t index = static_cast<std::size_t>(std::ceil(scaled));
  return ordered[index];
}

}  // namespace asciiomium::runtime
