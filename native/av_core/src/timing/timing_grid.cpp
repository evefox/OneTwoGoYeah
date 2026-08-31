// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timing_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace otgy::core {
namespace {

constexpr double kMaximumExactInteger = 9'007'199'254'740'991.0;
constexpr std::uint64_t kSixteenthRowsPerMeasure = 16U;
constexpr std::uint64_t kSixteenthRowsPerBeat = 4U;

[[nodiscard]] TimingGridLineKind line_kind(
    std::uint64_t sixteenth_row) noexcept {
  if (sixteenth_row % kSixteenthRowsPerMeasure == 0U) {
    return TimingGridLineKind::Measure;
  }
  if (sixteenth_row % kSixteenthRowsPerBeat == 0U) {
    return TimingGridLineKind::Beat;
  }
  return TimingGridLineKind::Sixteenth;
}

[[nodiscard]] TimingGridResult empty_grid() noexcept {
  return TimingGridResult{TimingGrid{std::vector<TimingGridLine>{}}};
}

}  // namespace

TimingGrid::TimingGrid(std::vector<TimingGridLine> lines) noexcept
    : lines_(std::move(lines)) {}

TimingGridResult::TimingGridResult(TimingGrid grid) noexcept
    : storage_(std::move(grid)) {}

TimingGridResult::TimingGridResult(TimingGridError error) noexcept
    : storage_(error) {}

bool TimingGridResult::has_value() const noexcept {
  return std::holds_alternative<TimingGrid>(storage_);
}

const TimingGrid* TimingGridResult::grid() const noexcept {
  return std::get_if<TimingGrid>(&storage_);
}

TimingGrid* TimingGridResult::grid() noexcept {
  return std::get_if<TimingGrid>(&storage_);
}

TimingGridError TimingGridResult::error() const noexcept {
  if (const auto* error = std::get_if<TimingGridError>(&storage_)) {
    return *error;
  }
  return TimingGridError::None;
}

TimingGrid TimingGridResult::take_grid() && {
  return std::move(std::get<TimingGrid>(storage_));
}

TimingGridResult generate_timing_grid(
    TimingParameters timing, double begin_seconds,
    double end_seconds) noexcept {
  if (!std::isfinite(begin_seconds) || !std::isfinite(end_seconds) ||
      begin_seconds > end_seconds) {
    return TimingGridResult{TimingGridError::InvalidRange};
  }

  const auto beat_zero = beat_to_seconds(timing, 0.0);
  if (!beat_zero) {
    if (beat_zero.error() == TimingConversionError::NoGrid) {
      return empty_grid();
    }
    return TimingGridResult{TimingGridError::InvalidTiming};
  }

  const auto begin_position =
      seconds_to_sixteenth_row(timing, begin_seconds);
  if (!begin_position) {
    return TimingGridResult{
        begin_position.error() == TimingConversionError::ResultOutOfRange
            ? TimingGridError::RangeTooLarge
            : TimingGridError::InvalidTiming};
  }

  const auto end_position =
      seconds_to_sixteenth_row(timing, end_seconds);
  if (!end_position) {
    return TimingGridResult{
        end_position.error() == TimingConversionError::ResultOutOfRange
            ? TimingGridError::RangeTooLarge
            : TimingGridError::InvalidTiming};
  }
  if (begin_seconds == end_seconds || *end_position.value() <= 0.0) {
    return empty_grid();
  }

  const double nonnegative_begin =
      std::max(0.0, *begin_position.value());
  if (nonnegative_begin > kMaximumExactInteger ||
      *end_position.value() > kMaximumExactInteger) {
    return TimingGridResult{TimingGridError::RangeTooLarge};
  }

  const double first_row_value = std::floor(nonnegative_begin);
  const double end_row_ceiling = std::ceil(*end_position.value());
  const double estimated_count = end_row_ceiling - first_row_value;
  if (estimated_count >
      static_cast<double>(kMaximumTimingGridLineCount + 2U)) {
    return TimingGridResult{TimingGridError::RangeTooLarge};
  }

  try {
    std::vector<TimingGridLine> lines;
    if (estimated_count > 0.0) {
      lines.reserve(static_cast<std::size_t>(std::min(
          estimated_count,
          static_cast<double>(kMaximumTimingGridLineCount))));
    }

    std::uint64_t row = static_cast<std::uint64_t>(first_row_value);
    while (true) {
      const auto seconds = sixteenth_row_to_seconds(
          timing, static_cast<double>(row));
      if (!seconds) {
        return TimingGridResult{TimingGridError::InvalidTiming};
      }
      if (*seconds.value() >= end_seconds) {
        break;
      }
      if (*seconds.value() >= begin_seconds) {
        if (lines.size() == kMaximumTimingGridLineCount) {
          return TimingGridResult{TimingGridError::RangeTooLarge};
        }
        lines.push_back({row, *seconds.value(), line_kind(row)});
      }
      if (static_cast<double>(row) >= kMaximumExactInteger) {
        return TimingGridResult{TimingGridError::RangeTooLarge};
      }
      ++row;
    }

    return TimingGridResult{TimingGrid{std::move(lines)}};
  } catch (const std::length_error&) {
    return TimingGridResult{TimingGridError::RangeTooLarge};
  } catch (const std::bad_alloc&) {
    return TimingGridResult{TimingGridError::AllocationFailure};
  }
}

}  // namespace otgy::core
