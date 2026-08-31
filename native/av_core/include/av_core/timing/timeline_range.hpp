// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/timing/timing_conversion.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class TimelineRangeError : std::uint8_t {
  None = 0,
  InvalidTiming = 1,
  InvalidAudioEnd = 2,
  RowOutOfRange = 3,
  ResultOutOfRange = 4,
};

[[nodiscard]] constexpr std::string_view timeline_range_error_message(
    TimelineRangeError error) noexcept {
  switch (error) {
    case TimelineRangeError::None:
      return {};
    case TimelineRangeError::InvalidTiming:
      return "timing parameters cannot be projected onto the timeline";
    case TimelineRangeError::InvalidAudioEnd:
      return "audio end must be a finite non-negative number of seconds";
    case TimelineRangeError::RowOutOfRange:
      return "timeline row exceeds the exact binary64 integer range";
    case TimelineRangeError::ResultOutOfRange:
      return "timeline range result is outside the finite range";
  }
  return "unknown timeline range error";
}

class TimelineRange final {
 public:
  TimelineRange(double begin_seconds, double end_seconds,
                double end_sixteenth_row,
                bool uses_zero_bpm_projection) noexcept;

  [[nodiscard]] constexpr double begin_sixteenth_row() const noexcept {
    return 0.0;
  }

  [[nodiscard]] double end_sixteenth_row() const noexcept {
    return end_sixteenth_row_;
  }

  [[nodiscard]] double begin_seconds() const noexcept {
    return begin_seconds_;
  }

  [[nodiscard]] double end_seconds() const noexcept {
    return end_seconds_;
  }

  [[nodiscard]] bool uses_zero_bpm_projection() const noexcept {
    return uses_zero_bpm_projection_;
  }

 private:
  double begin_seconds_;
  double end_seconds_;
  double end_sixteenth_row_;
  bool uses_zero_bpm_projection_;
};

class TimelineRangeResult final {
 public:
  explicit TimelineRangeResult(TimelineRange range) noexcept;
  explicit TimelineRangeResult(TimelineRangeError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const TimelineRange* range() const noexcept;
  [[nodiscard]] TimelineRangeError error() const noexcept;

 private:
  std::variant<TimelineRange, TimelineRangeError> storage_;
};

/// Computes the shared timeline extent in non-negative 1/16 chart rows.
///
/// The base extent is always row 64 (four 4/4 measures). Audio end is optional
/// and retains its exact input seconds if it is the latest contributor. Event
/// rows and SHOW-D range ends are scanned here so adapters never choose the
/// maximum independently. Zero BPM uses an internal 100 BPM projection only
/// for this range result; the authoritative TimingParameters are unchanged.
[[nodiscard]] TimelineRangeResult calculate_timeline_range(
    TimingParameters timing, std::optional<double> audio_end_seconds,
    const std::vector<std::uint64_t>& event_sixteenth_rows,
    const std::vector<std::uint64_t>& show_range_end_rows) noexcept;

}  // namespace otgy::core
