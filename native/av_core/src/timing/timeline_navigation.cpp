// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timeline_navigation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace otgy::core {
namespace {

constexpr double kSixteenthRowsPerBeat = 4.0;

}  // namespace

TimelineNavigation::TimelineNavigation(
    double to_begin_row, double page_up_row, double page_down_row,
    double to_end_row, bool can_to_begin, bool can_page_up,
    bool can_page_down, bool can_to_end) noexcept
    : to_begin_row_(to_begin_row),
      page_up_row_(page_up_row),
      page_down_row_(page_down_row),
      to_end_row_(to_end_row),
      can_to_begin_(can_to_begin),
      can_page_up_(can_page_up),
      can_page_down_(can_page_down),
      can_to_end_(can_to_end) {}

TimelineNavigationResult::TimelineNavigationResult(
    TimelineNavigation navigation) noexcept
    : storage_(std::move(navigation)) {}

TimelineNavigationResult::TimelineNavigationResult(
    TimelineNavigationError error) noexcept
    : storage_(error) {}

bool TimelineNavigationResult::has_value() const noexcept {
  return std::holds_alternative<TimelineNavigation>(storage_);
}

const TimelineNavigation* TimelineNavigationResult::navigation()
    const noexcept {
  return std::get_if<TimelineNavigation>(&storage_);
}

TimelineNavigationError TimelineNavigationResult::error() const noexcept {
  if (const auto* error =
          std::get_if<TimelineNavigationError>(&storage_)) {
    return *error;
  }
  return TimelineNavigationError::None;
}

TimelineNavigationResult calculate_timeline_navigation(
    const TimelineRange& range, double current_sixteenth_row,
    double visible_beats) noexcept {
  const double begin_row = range.begin_sixteenth_row();
  const double end_row = range.end_sixteenth_row();
  if (!std::isfinite(begin_row) || !std::isfinite(end_row) ||
      begin_row > end_row) {
    return TimelineNavigationResult{
        TimelineNavigationError::InvalidRange};
  }
  if (!std::isfinite(current_sixteenth_row) ||
      current_sixteenth_row < begin_row ||
      current_sixteenth_row > end_row) {
    return TimelineNavigationResult{
        TimelineNavigationError::InvalidPosition};
  }
  if (!std::isfinite(visible_beats) || visible_beats < 1.0) {
    return TimelineNavigationResult{
        TimelineNavigationError::InvalidVisibleBeatCount};
  }

  const double page_beats = visible_beats - 1.0;
  const double page_rows = page_beats * kSixteenthRowsPerBeat;
  const double raw_page_up = current_sixteenth_row - page_rows;
  const double raw_page_down = current_sixteenth_row + page_rows;
  if (!std::isfinite(page_rows) || !std::isfinite(raw_page_up) ||
      !std::isfinite(raw_page_down)) {
    return TimelineNavigationResult{
        TimelineNavigationError::ResultOutOfRange};
  }

  const double page_up = std::max(begin_row, raw_page_up);
  const double page_down = std::min(end_row, raw_page_down);
  return TimelineNavigationResult{TimelineNavigation{
      begin_row, page_up, page_down, end_row,
      current_sixteenth_row > begin_row, page_up < current_sixteenth_row,
      page_down > current_sixteenth_row, current_sixteenth_row < end_row}};
}

}  // namespace otgy::core
