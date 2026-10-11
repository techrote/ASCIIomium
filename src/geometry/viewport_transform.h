#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace asciiomium::geometry {

inline constexpr int kMaxTerminalColumns = 4096;
inline constexpr int kMaxTerminalRows = 2048;
inline constexpr std::uint64_t kMaxTerminalCells = 2'000'000;
inline constexpr int kMaxBrowserDimension = 16384;
inline constexpr double kMinCellAspect = 0.05;
inline constexpr double kMaxCellAspect = 4.0;

enum class FitMode {
  Stretch,
  Contain,
};

struct RectD {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;

  [[nodiscard]] bool Contains(double px, double py) const noexcept;
  friend constexpr bool operator==(const RectD&, const RectD&) = default;
};

struct TerminalPoint {
  int column = 0;
  int row = 0;

  friend constexpr bool operator==(const TerminalPoint&,
                                   const TerminalPoint&) = default;
};

struct BrowserPoint {
  int x = 0;
  int y = 0;

  friend constexpr bool operator==(const BrowserPoint&,
                                   const BrowserPoint&) = default;
};

struct ViewportConfig {
  int terminal_columns = 0;
  int terminal_rows = 0;
  int reserved_rows = 0;
  int browser_width = 0;
  int browser_height = 0;
  double cell_aspect = 0.5;
  FitMode fit_mode = FitMode::Contain;

  friend constexpr bool operator==(const ViewportConfig&,
                                   const ViewportConfig&) = default;
};

// Immutable geometry authority shared by rendering and browser input.
//
// Coordinates:
// - terminal rows/columns are cell addressed;
// - logical sample rows are doubled for U+2580 half-block rendering;
// - content_sample_rect is expressed in that logical sample lattice;
// - browser coordinates are integer CEF view pixels;
// - mouse mapping uses the centre of a terminal cell and does not invent
//   sub-cell pointer precision.
class ViewportTransform {
 public:
  ViewportTransform(const ViewportTransform&) = default;
  ViewportTransform& operator=(const ViewportTransform&) = default;
  ViewportTransform(ViewportTransform&&) noexcept = default;
  ViewportTransform& operator=(ViewportTransform&&) noexcept = default;

  [[nodiscard]] static std::optional<ViewportTransform> TryCreate(
      const ViewportConfig& config) noexcept;
  [[nodiscard]] static ViewportTransform Create(const ViewportConfig& config);

  [[nodiscard]] const ViewportConfig& config() const noexcept { return config_; }
  [[nodiscard]] int terminal_columns() const noexcept {
    return config_.terminal_columns;
  }
  [[nodiscard]] int terminal_rows() const noexcept {
    return config_.terminal_rows;
  }
  [[nodiscard]] int reserved_rows() const noexcept {
    return config_.reserved_rows;
  }
  [[nodiscard]] int content_rows() const noexcept { return content_rows_; }
  [[nodiscard]] int sample_columns() const noexcept {
    return config_.terminal_columns;
  }
  [[nodiscard]] int sample_rows() const noexcept { return sample_rows_; }
  [[nodiscard]] int browser_width() const noexcept {
    return config_.browser_width;
  }
  [[nodiscard]] int browser_height() const noexcept {
    return config_.browser_height;
  }
  [[nodiscard]] double cell_aspect() const noexcept {
    return config_.cell_aspect;
  }
  [[nodiscard]] FitMode fit_mode() const noexcept { return config_.fit_mode; }

  [[nodiscard]] const RectD& content_sample_rect() const noexcept {
    return content_sample_rect_;
  }
  [[nodiscard]] const RectD& content_terminal_rect() const noexcept {
    return content_terminal_rect_;
  }

  [[nodiscard]] std::optional<BrowserPoint> TerminalCellToBrowser(
      int column, int row) const noexcept;
  [[nodiscard]] std::optional<TerminalPoint> BrowserPixelToTerminal(
      int x, int y) const noexcept;

  friend bool operator==(const ViewportTransform&,
                         const ViewportTransform&) = default;

 private:
  ViewportTransform() = default;

  ViewportConfig config_{};
  int content_rows_ = 0;
  int sample_rows_ = 0;
  RectD content_sample_rect_{};
  RectD content_terminal_rect_{};
};

struct ViewportSnapshot {
  std::shared_ptr<const ViewportTransform> transform;
  std::uint64_t generation = 0;

  [[nodiscard]] bool valid() const noexcept {
    return static_cast<bool>(transform);
  }
};

struct ViewportPublishResult {
  ViewportSnapshot snapshot;
  bool changed = false;
  bool terminal_changed = false;
  bool browser_changed = false;
  bool sampling_changed = false;
};

// Thread-safe publication point for immutable geometry. Future input code can
// snapshot the exact transform used by rendering without duplicating formulas.
class ViewportState {
 public:
  [[nodiscard]] ViewportPublishResult Publish(
      const ViewportTransform& transform);
  [[nodiscard]] ViewportSnapshot Snapshot() const;

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const ViewportTransform> transform_;
  std::uint64_t generation_ = 0;
};

[[nodiscard]] bool TryParseCellAspect(std::string_view text,
                                      double* value) noexcept;
[[nodiscard]] bool TryParseFitMode(std::string_view text,
                                   FitMode* mode) noexcept;
[[nodiscard]] std::string_view FitModeName(FitMode mode) noexcept;

}  // namespace asciiomium::geometry
