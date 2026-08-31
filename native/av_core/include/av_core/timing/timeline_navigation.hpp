// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/timing/timeline_range.hpp"

#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class TimelineNavigationError : std::uint8_t {
  None = 0,
  InvalidRange = 1,
  InvalidPosition = 2,
  InvalidVisibleBeatCount = 3,
  ResultOutOfRange = 4,
};

[[nodiscard]] constexpr std::string_view timeline_navigation_error_message(
    TimelineNavigationError error) noexcept {
  switch (error) {
    case TimelineNavigationError::None:
      return {};
    case TimelineNavigationError::InvalidRange:
      return "timeline navigation range must be finite and ordered";
    case TimelineNavigationError::InvalidPosition:
      return "timeline position must be finite and inside the range";
    case TimelineNavigationError::InvalidVisibleBeatCount:
      return "visible beat count must be finite and at least one";
    case TimelineNavigationError::ResultOutOfRange:
      return "timeline navigation result is outside the finite range";
  }
  return "unknown timeline navigation error";
}

class TimelineNavigation final {
 public:
  TimelineNavigation(double to_begin_row, double page_up_row,
                     double page_down_row, double to_end_row,
                     bool can_to_begin, bool can_page_up,
                     bool can_page_down, bool can_to_end) noexcept;

  [[nodiscard]] double to_begin_row() const noexcept {
    return to_begin_row_;
  }
  [[nodiscard]] double page_up_row() const noexcept {
    return page_up_row_;
  }
  [[nodiscard]] double page_down_row() const noexcept {
    return page_down_row_;
  }
  [[nodiscard]] double to_end_row() const noexcept {
    return to_end_row_;
  }

  [[nodiscard]] bool can_to_begin() const noexcept {
    return can_to_begin_;
  }
  [[nodiscard]] bool can_page_up() const noexcept {
    return can_page_up_;
  }
  [[nodiscard]] bool can_page_down() const noexcept {
    return can_page_down_;
  }
  [[nodiscard]] bool can_to_end() const noexcept {
    return can_to_end_;
  }

 private:
  double to_begin_row_;
  double page_up_row_;
  double page_down_row_;
  double to_end_row_;
  bool can_to_begin_;
  bool can_page_up_;
  bool can_page_down_;
  bool can_to_end_;
};

class TimelineNavigationResult final {
 public:
  explicit TimelineNavigationResult(TimelineNavigation navigation) noexcept;
  explicit TimelineNavigationResult(TimelineNavigationError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const TimelineNavigation* navigation() const noexcept;
  [[nodiscard]] TimelineNavigationError error() const noexcept;

 private:
  std::variant<TimelineNavigation, TimelineNavigationError> storage_;
};

/// Computes all four discrete navigation targets in continuous 1/16 rows.
///
/// Page distance is `(visible_beats - 1) * 4`. Targets retain fractional rows
/// and are only clamped to TimelineRange boundaries; no snapping is applied.
[[nodiscard]] TimelineNavigationResult calculate_timeline_navigation(
    const TimelineRange& range, double current_sixteenth_row,
    double visible_beats) noexcept;

}  // namespace otgy::core
