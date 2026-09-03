// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timing_conversion.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

using otgy::core::TimingConversionError;
using otgy::core::TimingConversionResult;
using otgy::core::TimingParameters;

[[nodiscard]] double value_of(const TimingConversionResult& result) {
  assert(result);
  assert(result.error() == TimingConversionError::None);
  assert(result.value() != nullptr);
  return *result.value();
}

void assert_error(const TimingConversionResult& result,
                  TimingConversionError expected) {
  assert(!result);
  assert(result.value() == nullptr);
  assert(result.error() == expected);
}

[[nodiscard]] std::uint64_t bits_of(double value) {
  std::uint64_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void test_default_timing_and_chart_coordinates() {
  constexpr TimingParameters kTiming{100.0, 0.0};
  assert(value_of(otgy::core::beat_to_seconds(kTiming, 16.0)) == 9.6);
  assert(value_of(otgy::core::seconds_to_beat(kTiming, 9.6)) == 16.0);
  assert(value_of(
             otgy::core::sixteenth_row_to_seconds(kTiming, 64.0)) ==
         9.6);
  assert(value_of(
             otgy::core::seconds_to_sixteenth_row(kTiming, 9.6)) ==
         64.0);

  assert(value_of(otgy::core::beat_to_sixteenth_row(3.25)) == 13.0);
  assert(value_of(otgy::core::sixteenth_row_to_beat(13.0)) == 3.25);
  assert(value_of(otgy::core::beat_to_sixteenth_row(-0.25)) == -1.0);
  assert(value_of(otgy::core::sixteenth_row_to_beat(-1.0)) == -0.25);
}

void test_app_offset_sign_and_unrounded_values() {
  constexpr double kBpm = 129.24970691676435;
  constexpr double kDetectorOffset = 0.089977324263038547;
  constexpr TimingParameters kTiming{kBpm, -kDetectorOffset};

  assert(value_of(
             otgy::core::beat_to_seconds(kTiming, 0.0)) ==
         kDetectorOffset);
  const double beat_zero =
      value_of(otgy::core::seconds_to_beat(kTiming, kDetectorOffset));
  assert(beat_zero == 0.0);
  assert(!std::signbit(beat_zero));

  const auto bpm_bits = bits_of(kTiming.bpm);
  const auto offset_bits = bits_of(kTiming.offset_seconds);
  const double exact = value_of(
      otgy::core::sixteenth_row_to_seconds(kTiming, 1'000'000.0));
  const double display_rounded = value_of(
      otgy::core::sixteenth_row_to_seconds(
          TimingParameters{129.250, -0.090}, 1'000'000.0));
  assert(exact != display_rounded);
  assert(bits_of(kTiming.bpm) == bpm_bits);
  assert(bits_of(kTiming.offset_seconds) == offset_bits);
}

void test_offset_beat_adjustment_uses_the_original_baseline() {
  constexpr double kBpm = 127.8878685355244;
  constexpr double kOffset = -0.43331065759637188;
  constexpr TimingParameters kBaseline{kBpm, kOffset};
  const auto bpm_bits = bits_of(kBaseline.bpm);
  const auto offset_bits = bits_of(kBaseline.offset_seconds);

  const double two_beats =
      value_of(otgy::core::adjust_offset_by_beats(kBaseline, -2.0));
  const double seconds_per_beat = 60.0 / kBpm;
  const double adjustment_seconds = seconds_per_beat * -2.0;
  assert(two_beats == kOffset + adjustment_seconds);

  const double one_beat =
      value_of(otgy::core::adjust_offset_by_beats(kBaseline, -1.0));
  const double accumulated = value_of(otgy::core::adjust_offset_by_beats(
      {kBpm, one_beat}, -1.0));
  assert(bits_of(two_beats) != bits_of(accumulated));
  assert(bits_of(kBaseline.bpm) == bpm_bits);
  assert(bits_of(kBaseline.offset_seconds) == offset_bits);

  assert(value_of(otgy::core::adjust_offset_by_beats(
             {100.0, 29.8}, 1.0)) == 30.0);
  assert(value_of(otgy::core::adjust_offset_by_beats(
             {100.0, -29.8}, -1.0)) == -30.0);
  assert_error(otgy::core::adjust_offset_by_beats({0.0, 0.0}, 1.0),
               TimingConversionError::NoGrid);
  assert_error(otgy::core::adjust_offset_by_beats(
                   kBaseline, std::numeric_limits<double>::infinity()),
               TimingConversionError::NonFinitePosition);
}

void test_zero_bpm_has_no_time_grid() {
  for (const double bpm : {0.0, -0.0}) {
    const TimingParameters timing{bpm, 0.125};
    assert_error(otgy::core::seconds_to_beat(timing, 0.0),
                 TimingConversionError::NoGrid);
    assert_error(otgy::core::beat_to_seconds(timing, 0.0),
                 TimingConversionError::NoGrid);
    assert_error(otgy::core::seconds_to_sixteenth_row(timing, 0.0),
                 TimingConversionError::NoGrid);
    assert_error(otgy::core::sixteenth_row_to_seconds(timing, 0.0),
                 TimingConversionError::NoGrid);
  }

  assert(value_of(otgy::core::beat_to_sixteenth_row(16.0)) == 64.0);
  assert(value_of(otgy::core::sixteenth_row_to_beat(64.0)) == 16.0);
}

void test_parameter_and_position_errors() {
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();
  constexpr TimingParameters kValid{100.0, 0.0};

  assert(value_of(otgy::core::beat_to_seconds(
             {100'000.0, 30.0}, 0.0)) == -30.0);
  assert(value_of(otgy::core::beat_to_seconds(
             {100'000.0, -30.0}, 0.0)) == 30.0);

  for (const double bpm : {kNan, kInfinity, -kInfinity}) {
    assert_error(otgy::core::seconds_to_beat({bpm, 0.0}, 0.0),
                 TimingConversionError::NonFiniteBpm);
  }
  for (const double bpm : {-0.001, 100'000.001}) {
    assert_error(otgy::core::seconds_to_beat({bpm, 0.0}, 0.0),
                 TimingConversionError::BpmOutOfRange);
  }
  for (const double offset : {kNan, kInfinity, -kInfinity}) {
    assert_error(otgy::core::seconds_to_beat({100.0, offset}, 0.0),
                 TimingConversionError::NonFiniteOffset);
  }
  for (const double offset : {
           std::nextafter(-30.0, -kInfinity),
           std::nextafter(30.0, kInfinity),
       }) {
    assert_error(otgy::core::seconds_to_beat({100.0, offset}, 0.0),
                 TimingConversionError::OffsetOutOfRange);
  }

  for (const double position : {kNan, kInfinity, -kInfinity}) {
    assert_error(otgy::core::seconds_to_beat(kValid, position),
                 TimingConversionError::NonFinitePosition);
    assert_error(otgy::core::beat_to_seconds(kValid, position),
                 TimingConversionError::NonFinitePosition);
    assert_error(otgy::core::seconds_to_sixteenth_row(kValid, position),
                 TimingConversionError::NonFinitePosition);
    assert_error(otgy::core::sixteenth_row_to_seconds(kValid, position),
                 TimingConversionError::NonFinitePosition);
    assert_error(otgy::core::beat_to_sixteenth_row(position),
                 TimingConversionError::NonFinitePosition);
    assert_error(otgy::core::sixteenth_row_to_beat(position),
                 TimingConversionError::NonFinitePosition);
  }
}

void test_finite_overflow_errors() {
  constexpr double kMaximum = std::numeric_limits<double>::max();
  constexpr double kMinimumPositive =
      std::numeric_limits<double>::denorm_min();

  assert_error(otgy::core::seconds_to_beat({100'000.0, 0.0}, kMaximum),
               TimingConversionError::ResultOutOfRange);
  assert_error(
      otgy::core::seconds_to_beat({kMinimumPositive, 0.0}, 0.0),
      TimingConversionError::ResultOutOfRange);
  assert_error(otgy::core::beat_to_seconds({1.0, 0.0}, kMaximum),
               TimingConversionError::ResultOutOfRange);
  assert_error(
      otgy::core::sixteenth_row_to_seconds(
          {kMinimumPositive, 0.0}, kMaximum),
      TimingConversionError::ResultOutOfRange);
  assert_error(otgy::core::beat_to_sixteenth_row(kMaximum),
               TimingConversionError::ResultOutOfRange);
}

void test_error_messages_and_result_state() {
  const TimingConversionResult success{42.0};
  assert(success);
  assert(success.error() == TimingConversionError::None);
  assert(value_of(success) == 42.0);

  assert(otgy::core::timing_conversion_error_message(
             TimingConversionError::None)
             .empty());
  constexpr std::array<TimingConversionError, 7U> kErrors{{
      TimingConversionError::NonFiniteBpm,
      TimingConversionError::BpmOutOfRange,
      TimingConversionError::NonFiniteOffset,
      TimingConversionError::OffsetOutOfRange,
      TimingConversionError::NonFinitePosition,
      TimingConversionError::NoGrid,
      TimingConversionError::ResultOutOfRange,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::timing_conversion_error_message(error).empty());
  }
  assert(!otgy::core::timing_conversion_error_message(
              static_cast<TimingConversionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_default_timing_and_chart_coordinates();
  test_app_offset_sign_and_unrounded_values();
  test_offset_beat_adjustment_uses_the_original_baseline();
  test_zero_bpm_has_no_time_grid();
  test_parameter_and_position_errors();
  test_finite_overflow_errors();
  test_error_messages_and_result_state();
  return 0;
}
