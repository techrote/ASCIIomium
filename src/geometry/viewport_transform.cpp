#include "geometry/viewport_transform.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace asciiomium::geometry {
namespace {

[[nodiscard]] bool ValidConfig(const ViewportConfig& config) noexcept {
  if (config.terminal_columns <= 0 ||
      config.terminal_columns > kMaxTerminalColumns ||
      config.terminal_rows <= 0 ||
      config.terminal_rows > kMaxTerminalRows ||
      config.reserved_rows < 0 ||
      config.reserved_rows >= config.terminal_rows ||
      config.browser_width <= 0 ||
      config.browser_width > kMaxBrowserDimension ||
      config.browser_height <= 0 ||
      config.browser_height > kMaxBrowserDimension ||
      !std::isfinite(config.cell_aspect) ||
      config.cell_aspect < kMinCellAspect ||
      config.cell_aspect > kMaxCellAspect) {
    return false;
  }

  const auto cells =
      static_cast<std::uint64_t>(config.terminal_columns) *
      static_cast<std::uint64_t>(config.terminal_rows);
  return cells <= kMaxTerminalCells;
}

[[nodiscard]] RectD ComputeContentRect(const ViewportConfig& config,
                                       int content_rows) noexcept {
  const double sample_columns =
      static_cast<double>(config.terminal_columns);
  const double sample_rows = static_cast<double>(content_rows) * 2.0;

  if (config.fit_mode == FitMode::Stretch) {
    return RectD{0.0, 0.0, sample_columns, sample_rows};
  }

  const double browser_aspect =
      static_cast<double>(config.browser_width) /
      static_cast<double>(config.browser_height);

  // Each terminal cell contributes two vertical colour samples. If
  // cell_aspect = cell_width / cell_height, one logical sample therefore has
  // width/height = 2*cell_aspect.
  const double sample_physical_aspect = 2.0 * config.cell_aspect;
  const double desired_lattice_aspect =
      browser_aspect / sample_physical_aspect;
  const double target_lattice_aspect = sample_columns / sample_rows;

  RectD rect;
  if (target_lattice_aspect > desired_lattice_aspect) {
    rect.height = sample_rows;
    rect.width = desired_lattice_aspect * rect.height;
    rect.x = (sample_columns - rect.width) * 0.5;
  } else {
    rect.width = sample_columns;
    rect.height = rect.width / desired_lattice_aspect;
    rect.y = (sample_rows - rect.height) * 0.5;
  }
  return rect;
}

}  // namespace

bool RectD::Contains(double px, double py) const noexcept {
  return px >= x && py >= y && px < x + width && py < y + height;
}

std::optional<ViewportTransform> ViewportTransform::TryCreate(
    const ViewportConfig& config) noexcept {
  if (!ValidConfig(config)) {
    return std::nullopt;
  }

  ViewportTransform result;
  result.config_ = config;
  result.content_rows_ = config.terminal_rows - config.reserved_rows;
  result.sample_rows_ = result.content_rows_ * 2;
  result.content_sample_rect_ =
      ComputeContentRect(config, result.content_rows_);
  result.content_terminal_rect_ =
      RectD{result.content_sample_rect_.x,
            static_cast<double>(config.reserved_rows) +
                result.content_sample_rect_.y * 0.5,
            result.content_sample_rect_.width,
            result.content_sample_rect_.height * 0.5};
  return result;
}

ViewportTransform ViewportTransform::Create(const ViewportConfig& config) {
  auto transform = TryCreate(config);
  if (!transform.has_value()) {
    throw std::invalid_argument("invalid or unsafe viewport geometry");
  }
  return *transform;
}

std::optional<BrowserPoint> ViewportTransform::TerminalCellToBrowser(
    int column, int row) const noexcept {
  if (column < 0 || column >= config_.terminal_columns ||
      row < config_.reserved_rows || row >= config_.terminal_rows) {
    return std::nullopt;
  }

  const double sample_x = static_cast<double>(column) + 0.5;
  const double sample_y =
      static_cast<double>(row - config_.reserved_rows) * 2.0 + 1.0;
  if (!content_sample_rect_.Contains(sample_x, sample_y)) {
    return std::nullopt;
  }

  const double u =
      (sample_x - content_sample_rect_.x) / content_sample_rect_.width;
  const double v =
      (sample_y - content_sample_rect_.y) / content_sample_rect_.height;
  const int x = std::clamp(
      static_cast<int>(std::floor(u * config_.browser_width)),
      0, config_.browser_width - 1);
  const int y = std::clamp(
      static_cast<int>(std::floor(v * config_.browser_height)),
      0, config_.browser_height - 1);
  return BrowserPoint{x, y};
}

std::optional<TerminalPoint> ViewportTransform::BrowserPixelToTerminal(
    int x, int y) const noexcept {
  if (x < 0 || x >= config_.browser_width ||
      y < 0 || y >= config_.browser_height) {
    return std::nullopt;
  }

  const double u =
      (static_cast<double>(x) + 0.5) / config_.browser_width;
  const double v =
      (static_cast<double>(y) + 0.5) / config_.browser_height;
  const double sample_x =
      content_sample_rect_.x + u * content_sample_rect_.width;
  const double sample_y =
      content_sample_rect_.y + v * content_sample_rect_.height;

  const int column = std::clamp(
      static_cast<int>(std::floor(sample_x)),
      0, config_.terminal_columns - 1);
  const int content_row = std::clamp(
      static_cast<int>(std::floor(sample_y * 0.5)),
      0, content_rows_ - 1);
  return TerminalPoint{column, config_.reserved_rows + content_row};
}

ViewportPublishResult ViewportState::Publish(
    const ViewportTransform& transform) {
  std::lock_guard lock(mutex_);

  ViewportPublishResult result;
  const bool had_previous = static_cast<bool>(transform_);
  if (had_previous && *transform_ == transform) {
    result.snapshot = ViewportSnapshot{transform_, generation_};
    return result;
  }

  if (!had_previous) {
    result.terminal_changed = true;
    result.browser_changed = true;
    result.sampling_changed = true;
  } else {
    const auto& old = transform_->config();
    const auto& next = transform.config();
    result.terminal_changed =
        old.terminal_columns != next.terminal_columns ||
        old.terminal_rows != next.terminal_rows ||
        old.reserved_rows != next.reserved_rows;
    result.browser_changed =
        old.browser_width != next.browser_width ||
        old.browser_height != next.browser_height;
    result.sampling_changed =
        old.cell_aspect != next.cell_aspect ||
        old.fit_mode != next.fit_mode;
  }

  transform_ = std::make_shared<const ViewportTransform>(transform);
  ++generation_;
  result.changed = true;
  result.snapshot = ViewportSnapshot{transform_, generation_};
  return result;
}

ViewportSnapshot ViewportState::Snapshot() const {
  std::lock_guard lock(mutex_);
  return ViewportSnapshot{transform_, generation_};
}

bool TryParseCellAspect(std::string_view text, double* value) noexcept {
  if (value == nullptr || text.empty()) {
    return false;
  }

  double parsed = 0.0;
  const auto result = std::from_chars(
      text.data(), text.data() + text.size(), parsed, std::chars_format::general);
  if (result.ec != std::errc{} ||
      result.ptr != text.data() + text.size() ||
      !std::isfinite(parsed) ||
      parsed < kMinCellAspect ||
      parsed > kMaxCellAspect) {
    return false;
  }
  *value = parsed;
  return true;
}

bool TryParseFitMode(std::string_view text, FitMode* mode) noexcept {
  if (mode == nullptr) {
    return false;
  }
  if (text == "contain") {
    *mode = FitMode::Contain;
    return true;
  }
  if (text == "stretch") {
    *mode = FitMode::Stretch;
    return true;
  }
  return false;
}

std::string_view FitModeName(FitMode mode) noexcept {
  switch (mode) {
    case FitMode::Stretch:
      return "stretch";
    case FitMode::Contain:
      return "contain";
  }
  return "unknown";
}

}  // namespace asciiomium::geometry
