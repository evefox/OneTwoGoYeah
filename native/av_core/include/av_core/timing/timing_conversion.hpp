// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

struct TimingParameters final {
  double bpm;
  double offset_seconds;
};

enum class TimingConversionError : std::uint8_t {
  None = 0,
  NonFiniteBpm = 1,
  BpmOutOfRange = 2,
  NonFiniteOffset = 3,
  OffsetOutOfRange = 4,
  NonFinitePosition = 5,
  NoGrid = 6,
  ResultOutOfRange = 7,
};

[[nodiscard]] constexpr std::string_view timing_conversion_error_message(
    TimingConversionError error) noexcept {
  switch (error) {
    case TimingConversionError::None:
      return {};
    case TimingConversionError::NonFiniteBpm:
      return "BPM must be finite";
    case TimingConversionError::BpmOutOfRange:
      return "BPM must be between 0 and 100000";
    case TimingConversionError::NonFiniteOffset:
      return "Offset must be finite";
    case TimingConversionError::OffsetOutOfRange:
      return "Offset must be between -30 and 30 seconds";
    case TimingConversionError::NonFinitePosition:
      return "timing position must be finite";
    case TimingConversionError::NoGrid:
      return "BPM is zero; no time grid is available";
    case TimingConversionError::ResultOutOfRange:
      return "timing conversion result is outside the finite range";
  }
  return "unknown timing conversion error";
}

class TimingConversionResult final {
 public:
  explicit TimingConversionResult(double value) noexcept;
  explicit TimingConversionResult(TimingConversionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const double* value() const noexcept;
  [[nodiscard]] TimingConversionError error() const noexcept;

 private:
  std::variant<double, TimingConversionError> storage_;
};

/// Converts audio seconds to a continuous zero-based chart beat position.
///
/// App Offset is the music start time relative to beat zero, so the mapping is
/// `(seconds + offset_seconds) / (60 / bpm)`.
[[nodiscard]] TimingConversionResult seconds_to_beat(
    TimingParameters timing, double seconds) noexcept;

/// Converts a continuous chart beat position to audio seconds using
/// `beat * (60 / bpm) - offset_seconds`.
[[nodiscard]] TimingConversionResult beat_to_seconds(
    TimingParameters timing, double beat) noexcept;

/// Converts audio seconds to continuous 1/16-row position (four rows/beat).
[[nodiscard]] TimingConversionResult seconds_to_sixteenth_row(
    TimingParameters timing, double seconds) noexcept;

/// Converts continuous 1/16-row position to audio seconds (four rows/beat).
[[nodiscard]] TimingConversionResult sixteenth_row_to_seconds(
    TimingParameters timing, double sixteenth_row) noexcept;

/// These two abstract chart-coordinate conversions remain available when BPM
/// is zero because they do not project the chart onto audio time.
[[nodiscard]] TimingConversionResult beat_to_sixteenth_row(
    double beat) noexcept;
[[nodiscard]] TimingConversionResult sixteenth_row_to_beat(
    double sixteenth_row) noexcept;

}  // namespace otgy::core
