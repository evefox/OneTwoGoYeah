// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timeline_range.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace otgy::core {
namespace {

constexpr double kProjectionBpm = 100.0;
constexpr double kBaseEndSixteenthRow = 64.0;
constexpr std::uint64_t kMaximumExactIntegerRow =
    9'007'199'254'740'991ULL;

struct Extent final {
  double row;
  double seconds;
};

[[nodiscard]] TimelineRangeError conversion_error(
    TimingConversionError error) noexcept {
  return error == TimingConversionError::ResultOutOfRange
             ? TimelineRangeError::ResultOutOfRange
             : TimelineRangeError::InvalidTiming;
}

[[nodiscard]] std::optional<std::uint64_t> maximum_row(
    const std::vector<std::uint64_t>& rows) noexcept {
  if (rows.empty()) {
    return std::nullopt;
  }
  return *std::max_element(rows.begin(), rows.end());
}

}  // namespace

TimelineRange::TimelineRange(double begin_seconds, double end_seconds,
                             double end_sixteenth_row,
                             bool uses_zero_bpm_projection) noexcept
    : begin_seconds_(begin_seconds),
      end_seconds_(end_seconds),
      end_sixteenth_row_(end_sixteenth_row),
      uses_zero_bpm_projection_(uses_zero_bpm_projection) {}

TimelineRangeResult::TimelineRangeResult(TimelineRange range) noexcept
    : storage_(std::move(range)) {}

TimelineRangeResult::TimelineRangeResult(TimelineRangeError error) noexcept
    : storage_(error) {}

bool TimelineRangeResult::has_value() const noexcept {
  return std::holds_alternative<TimelineRange>(storage_);
}

const TimelineRange* TimelineRangeResult::range() const noexcept {
  return std::get_if<TimelineRange>(&storage_);
}

TimelineRangeError TimelineRangeResult::error() const noexcept {
  if (const auto* error = std::get_if<TimelineRangeError>(&storage_)) {
    return *error;
  }
  return TimelineRangeError::None;
}

TimelineRangeResult calculate_timeline_range(
    TimingParameters timing, std::optional<double> audio_end_seconds,
    const std::vector<std::uint64_t>& event_sixteenth_rows,
    const std::vector<std::uint64_t>& show_range_end_rows) noexcept {
  return calculate_timeline_range_with_end(
      timing, audio_end_seconds, std::nullopt, event_sixteenth_rows,
      show_range_end_rows);
}

TimelineRangeResult calculate_timeline_range_with_end(
    TimingParameters timing, std::optional<double> audio_end_seconds,
    std::optional<std::uint64_t> custom_end_sixteenth_row,
    const std::vector<std::uint64_t>& event_sixteenth_rows,
    const std::vector<std::uint64_t>& show_range_end_rows) noexcept {
  if (audio_end_seconds.has_value() &&
      (!std::isfinite(*audio_end_seconds) || *audio_end_seconds < 0.0)) {
    return TimelineRangeResult{TimelineRangeError::InvalidAudioEnd};
  }

  bool uses_zero_bpm_projection = false;
  const auto authoritative_origin = beat_to_seconds(timing, 0.0);
  if (!authoritative_origin) {
    if (authoritative_origin.error() != TimingConversionError::NoGrid) {
      return TimelineRangeResult{
          conversion_error(authoritative_origin.error())};
    }
    timing.bpm = kProjectionBpm;
    uses_zero_bpm_projection = true;
  }

  const auto begin_seconds = sixteenth_row_to_seconds(timing, 0.0);
  if (!begin_seconds) {
    return TimelineRangeResult{conversion_error(begin_seconds.error())};
  }
  const auto base_end_seconds =
      sixteenth_row_to_seconds(timing, kBaseEndSixteenthRow);
  if (!base_end_seconds) {
    return TimelineRangeResult{
        conversion_error(base_end_seconds.error())};
  }
  Extent extent{kBaseEndSixteenthRow, *base_end_seconds.value()};

  if (audio_end_seconds.has_value()) {
    const auto audio_end_row =
        seconds_to_sixteenth_row(timing, *audio_end_seconds);
    if (!audio_end_row) {
      return TimelineRangeResult{conversion_error(audio_end_row.error())};
    }
    if (*audio_end_row.value() > extent.row) {
      extent = {*audio_end_row.value(), *audio_end_seconds};
    }
  }

  if (custom_end_sixteenth_row.has_value()) {
    if (*custom_end_sixteenth_row > kMaximumExactIntegerRow) {
      return TimelineRangeResult{TimelineRangeError::RowOutOfRange};
    }
    const double row_value =
        static_cast<double>(*custom_end_sixteenth_row);
    if (row_value > extent.row) {
      const auto seconds = sixteenth_row_to_seconds(timing, row_value);
      if (!seconds) {
        return TimelineRangeResult{conversion_error(seconds.error())};
      }
      extent = {row_value, *seconds.value()};
    }
  }

  for (const auto row : {
           maximum_row(event_sixteenth_rows),
           maximum_row(show_range_end_rows),
       }) {
    if (!row.has_value()) {
      continue;
    }
    if (*row > kMaximumExactIntegerRow) {
      return TimelineRangeResult{TimelineRangeError::RowOutOfRange};
    }
    const double row_value = static_cast<double>(*row);
    if (row_value <= extent.row) {
      continue;
    }
    const auto seconds = sixteenth_row_to_seconds(timing, row_value);
    if (!seconds) {
      return TimelineRangeResult{conversion_error(seconds.error())};
    }
    extent = {row_value, *seconds.value()};
  }

  return TimelineRangeResult{TimelineRange{
      *begin_seconds.value(), extent.seconds, extent.row,
      uses_zero_bpm_projection}};
}

}  // namespace otgy::core
