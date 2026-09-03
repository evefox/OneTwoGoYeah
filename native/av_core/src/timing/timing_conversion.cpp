// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timing_conversion.hpp"

#include <algorithm>
#include <cmath>

namespace otgy::core {
namespace {

constexpr double kMaximumBpm = 100'000.0;
constexpr double kMaximumOffsetSeconds = 30.0;
constexpr double kSixteenthRowsPerBeat = 4.0;
constexpr double kSecondsPerMinute = 60.0;

[[nodiscard]] TimingConversionError validate_parameters(
    TimingParameters timing) noexcept {
  if (!std::isfinite(timing.bpm)) {
    return TimingConversionError::NonFiniteBpm;
  }
  if (timing.bpm < 0.0 || timing.bpm > kMaximumBpm) {
    return TimingConversionError::BpmOutOfRange;
  }
  if (!std::isfinite(timing.offset_seconds)) {
    return TimingConversionError::NonFiniteOffset;
  }
  if (timing.offset_seconds < -kMaximumOffsetSeconds ||
      timing.offset_seconds > kMaximumOffsetSeconds) {
    return TimingConversionError::OffsetOutOfRange;
  }
  return TimingConversionError::None;
}

[[nodiscard]] TimingConversionResult finite_result(double value) noexcept {
  if (!std::isfinite(value)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  return TimingConversionResult{value};
}

[[nodiscard]] TimingConversionError validate_time_projection(
    TimingParameters timing, double position) noexcept {
  const auto parameter_error = validate_parameters(timing);
  if (parameter_error != TimingConversionError::None) {
    return parameter_error;
  }
  if (!std::isfinite(position)) {
    return TimingConversionError::NonFinitePosition;
  }
  if (timing.bpm == 0.0) {
    return TimingConversionError::NoGrid;
  }
  return TimingConversionError::None;
}

}  // namespace

TimingConversionResult::TimingConversionResult(double value) noexcept
    : storage_(value) {}

TimingConversionResult::TimingConversionResult(
    TimingConversionError error) noexcept
    : storage_(error) {}

bool TimingConversionResult::has_value() const noexcept {
  return std::holds_alternative<double>(storage_);
}

const double* TimingConversionResult::value() const noexcept {
  return std::get_if<double>(&storage_);
}

TimingConversionError TimingConversionResult::error() const noexcept {
  if (const auto* error =
          std::get_if<TimingConversionError>(&storage_)) {
    return *error;
  }
  return TimingConversionError::None;
}

TimingConversionResult seconds_to_beat(
    TimingParameters timing, double seconds) noexcept {
  const auto error = validate_time_projection(timing, seconds);
  if (error != TimingConversionError::None) {
    return TimingConversionResult{error};
  }
  const double seconds_per_beat = kSecondsPerMinute / timing.bpm;
  if (!std::isfinite(seconds_per_beat)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  return finite_result((seconds + timing.offset_seconds) /
                       seconds_per_beat);
}

TimingConversionResult beat_to_seconds(
    TimingParameters timing, double beat) noexcept {
  const auto error = validate_time_projection(timing, beat);
  if (error != TimingConversionError::None) {
    return TimingConversionResult{error};
  }
  const double seconds_per_beat = kSecondsPerMinute / timing.bpm;
  if (!std::isfinite(seconds_per_beat)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  const double beat_seconds = beat * seconds_per_beat;
  return finite_result(beat_seconds - timing.offset_seconds);
}

TimingConversionResult seconds_to_sixteenth_row(
    TimingParameters timing, double seconds) noexcept {
  const auto beat_result = seconds_to_beat(timing, seconds);
  if (!beat_result) {
    return TimingConversionResult{beat_result.error()};
  }
  return finite_result(*beat_result.value() * kSixteenthRowsPerBeat);
}

TimingConversionResult sixteenth_row_to_seconds(
    TimingParameters timing, double sixteenth_row) noexcept {
  const auto error = validate_time_projection(timing, sixteenth_row);
  if (error != TimingConversionError::None) {
    return TimingConversionResult{error};
  }
  const double beat = sixteenth_row / kSixteenthRowsPerBeat;
  const double seconds_per_beat = kSecondsPerMinute / timing.bpm;
  if (!std::isfinite(seconds_per_beat)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  const double beat_seconds = beat * seconds_per_beat;
  return finite_result(beat_seconds - timing.offset_seconds);
}

TimingConversionResult beat_to_sixteenth_row(double beat) noexcept {
  if (!std::isfinite(beat)) {
    return TimingConversionResult{
        TimingConversionError::NonFinitePosition};
  }
  return finite_result(beat * kSixteenthRowsPerBeat);
}

TimingConversionResult sixteenth_row_to_beat(
    double sixteenth_row) noexcept {
  if (!std::isfinite(sixteenth_row)) {
    return TimingConversionResult{
        TimingConversionError::NonFinitePosition};
  }
  return TimingConversionResult{sixteenth_row / kSixteenthRowsPerBeat};
}

TimingConversionResult adjust_offset_by_beats(
    TimingParameters baseline, double cumulative_beats) noexcept {
  const auto error = validate_time_projection(baseline, cumulative_beats);
  if (error != TimingConversionError::None) {
    return TimingConversionResult{error};
  }
  const double seconds_per_beat = kSecondsPerMinute / baseline.bpm;
  if (!std::isfinite(seconds_per_beat)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  const double adjustment_seconds = seconds_per_beat * cumulative_beats;
  if (!std::isfinite(adjustment_seconds)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  const double adjusted_offset = baseline.offset_seconds + adjustment_seconds;
  if (!std::isfinite(adjusted_offset)) {
    return TimingConversionResult{
        TimingConversionError::ResultOutOfRange};
  }
  return TimingConversionResult{std::clamp(
      adjusted_offset, -kMaximumOffsetSeconds, kMaximumOffsetSeconds)};
}

}  // namespace otgy::core
