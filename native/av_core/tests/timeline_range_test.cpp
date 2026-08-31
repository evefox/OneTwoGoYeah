// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timeline_range.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace {

using otgy::core::TimelineRange;
using otgy::core::TimelineRangeError;
using otgy::core::TimelineRangeResult;
using otgy::core::TimingParameters;

[[nodiscard]] TimelineRange range_of(
    const TimelineRangeResult& result) {
  assert(result);
  assert(result.error() == TimelineRangeError::None);
  assert(result.range() != nullptr);
  return *result.range();
}

void assert_error(const TimelineRangeResult& result,
                  TimelineRangeError expected) {
  assert(!result);
  assert(result.range() == nullptr);
  assert(result.error() == expected);
}

[[nodiscard]] std::uint64_t bits_of(double value) {
  std::uint64_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void test_empty_workspace_base_range() {
  const auto& range = range_of(otgy::core::calculate_timeline_range(
      {100.0, 0.0}, std::nullopt, {}, {}));
  assert(range.begin_sixteenth_row() == 0.0);
  assert(range.end_sixteenth_row() == 64.0);
  assert(range.begin_seconds() == 0.0);
  assert(range.end_seconds() == 9.6);
  assert(!range.uses_zero_bpm_projection());
}

void test_each_latest_contributor() {
  constexpr TimingParameters kTiming{100.0, 0.0};

  const auto& base = range_of(otgy::core::calculate_timeline_range(
      kTiming, 5.0, {1U, 63U}, {16U, 32U}));
  assert(base.end_sixteenth_row() == 64.0);
  assert(base.end_seconds() == 9.6);

  constexpr double kAudioEnd = 10.123456789012345;
  const auto& audio = range_of(otgy::core::calculate_timeline_range(
      kTiming, kAudioEnd, {1U}, {16U}));
  const auto audio_row =
      otgy::core::seconds_to_sixteenth_row(kTiming, kAudioEnd);
  assert(audio_row);
  assert(audio.end_sixteenth_row() == *audio_row.value());
  assert(bits_of(audio.end_seconds()) == bits_of(kAudioEnd));

  const auto& event = range_of(otgy::core::calculate_timeline_range(
      kTiming, 5.0, {12U, 100U, 80U}, {16U, 32U}));
  assert(event.end_sixteenth_row() == 100.0);
  assert(event.end_seconds() == 15.0);

  const auto& show = range_of(otgy::core::calculate_timeline_range(
      kTiming, 5.0, {12U, 100U}, {16U, 120U, 96U}));
  assert(show.end_sixteenth_row() == 120.0);
  assert(show.end_seconds() == 18.0);
}

void test_offset_and_zero_bpm_projection() {
  constexpr double kOffset = -0.089977324263038547;
  const auto& offset_range = range_of(
      otgy::core::calculate_timeline_range(
          {100.0, kOffset}, std::nullopt, {}, {}));
  assert(offset_range.begin_seconds() == -kOffset);
  assert(offset_range.end_seconds() == 9.6 - kOffset);
  assert(!offset_range.uses_zero_bpm_projection());

  for (const double bpm : {0.0, -0.0}) {
    const TimingParameters timing{bpm, kOffset};
    const auto bpm_bits = bits_of(timing.bpm);
    const auto offset_bits = bits_of(timing.offset_seconds);
    const auto& range = range_of(
        otgy::core::calculate_timeline_range(
            timing, std::nullopt, {}, {}));
    assert(range.begin_seconds() == -kOffset);
    assert(range.end_sixteenth_row() == 64.0);
    assert(range.end_seconds() == 9.6 - kOffset);
    assert(range.uses_zero_bpm_projection());
    assert(bits_of(timing.bpm) == bpm_bits);
    assert(bits_of(timing.offset_seconds) == offset_bits);
  }
}

void test_errors_and_messages() {
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();

  for (const double audio_end : {kNan, kInfinity, -0.001}) {
    assert_error(otgy::core::calculate_timeline_range(
                     {100.0, 0.0}, audio_end, {}, {}),
                 TimelineRangeError::InvalidAudioEnd);
  }
  assert_error(otgy::core::calculate_timeline_range(
                   {-1.0, 0.0}, std::nullopt, {}, {}),
               TimelineRangeError::InvalidTiming);
  assert_error(otgy::core::calculate_timeline_range(
                   {100.0, 31.0}, std::nullopt, {}, {}),
               TimelineRangeError::InvalidTiming);
  assert_error(otgy::core::calculate_timeline_range(
                   {100.0, 0.0}, std::nullopt,
                   {9'007'199'254'740'992ULL}, {}),
               TimelineRangeError::RowOutOfRange);
  assert_error(otgy::core::calculate_timeline_range(
                   {100.0, 0.0},
                   std::numeric_limits<double>::max(), {}, {}),
               TimelineRangeError::ResultOutOfRange);

  assert(otgy::core::timeline_range_error_message(
             TimelineRangeError::None)
             .empty());
  constexpr std::array<TimelineRangeError, 4U> kErrors{{
      TimelineRangeError::InvalidTiming,
      TimelineRangeError::InvalidAudioEnd,
      TimelineRangeError::RowOutOfRange,
      TimelineRangeError::ResultOutOfRange,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::timeline_range_error_message(error).empty());
  }
  assert(!otgy::core::timeline_range_error_message(
              static_cast<TimelineRangeError>(255))
              .empty());
}

}  // namespace

int main() {
  test_empty_workspace_base_range();
  test_each_latest_contributor();
  test_offset_and_zero_bpm_projection();
  test_errors_and_messages();
  return 0;
}
