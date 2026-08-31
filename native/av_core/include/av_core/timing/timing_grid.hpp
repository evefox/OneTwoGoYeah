// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/timing/timing_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class TimingGridLineKind : std::uint8_t {
  Measure = 0,
  Beat = 1,
  Sixteenth = 2,
};

struct TimingGridLine final {
  std::uint64_t sixteenth_row;
  double seconds;
  TimingGridLineKind kind;
};

enum class TimingGridError : std::uint8_t {
  None = 0,
  InvalidRange = 1,
  InvalidTiming = 2,
  RangeTooLarge = 3,
  AllocationFailure = 4,
};

[[nodiscard]] constexpr std::string_view timing_grid_error_message(
    TimingGridError error) noexcept {
  switch (error) {
    case TimingGridError::None:
      return {};
    case TimingGridError::InvalidRange:
      return "timing grid range must be finite and ordered";
    case TimingGridError::InvalidTiming:
      return "timing parameters cannot be projected onto the grid";
    case TimingGridError::RangeTooLarge:
      return "timing grid range contains too many lines";
    case TimingGridError::AllocationFailure:
      return "unable to allocate timing grid lines";
  }
  return "unknown timing grid error";
}

class TimingGrid final {
 public:
  explicit TimingGrid(std::vector<TimingGridLine> lines) noexcept;

  TimingGrid(const TimingGrid&) = default;
  TimingGrid& operator=(const TimingGrid&) = default;
  TimingGrid(TimingGrid&&) noexcept = default;
  TimingGrid& operator=(TimingGrid&&) noexcept = default;
  ~TimingGrid() = default;

  [[nodiscard]] const std::vector<TimingGridLine>& lines() const noexcept {
    return lines_;
  }

 private:
  std::vector<TimingGridLine> lines_;
};

class TimingGridResult final {
 public:
  explicit TimingGridResult(TimingGrid grid) noexcept;
  explicit TimingGridResult(TimingGridError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const TimingGrid* grid() const noexcept;
  [[nodiscard]] TimingGrid* grid() noexcept;
  [[nodiscard]] TimingGridError error() const noexcept;

  [[nodiscard]] TimingGrid take_grid() &&;

 private:
  std::variant<TimingGrid, TimingGridError> storage_;
};

inline constexpr std::size_t kMaximumTimingGridLineCount = 1'000'000U;

/// Generates the non-negative 4/4 chart grid in `[begin_seconds, end_seconds)`.
///
/// Every returned second value comes from sixteenth_row_to_seconds. A valid
/// zero BPM succeeds with an empty grid. The half-open range makes adjacent
/// requests composable without duplicate boundary lines.
[[nodiscard]] TimingGridResult generate_timing_grid(
    TimingParameters timing, double begin_seconds,
    double end_seconds) noexcept;

}  // namespace otgy::core
