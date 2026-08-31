// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timing_grid.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace {

using otgy::core::TimingGrid;
using otgy::core::TimingGridError;
using otgy::core::TimingGridLineKind;
using otgy::core::TimingGridResult;
using otgy::core::TimingParameters;

[[nodiscard]] const TimingGrid& grid_of(const TimingGridResult& result) {
  assert(result);
  assert(result.error() == TimingGridError::None);
  assert(result.grid() != nullptr);
  return *result.grid();
}

void assert_error(const TimingGridResult& result,
                  TimingGridError expected) {
  assert(!result);
  assert(result.grid() == nullptr);
  assert(result.error() == expected);
}

void test_default_four_measure_grid() {
  constexpr TimingParameters kTiming{100.0, 0.0};
  const auto result = otgy::core::generate_timing_grid(kTiming, 0.0, 9.6);
  const auto& lines = grid_of(result).lines();
  assert(lines.size() == 64U);

  std::size_t measure_count = 0U;
  std::size_t beat_count = 0U;
  std::size_t sixteenth_count = 0U;
  for (std::size_t index = 0U; index < lines.size(); ++index) {
    const auto& line = lines[index];
    assert(line.sixteenth_row == index);
    if (index > 0U) {
      assert(lines[index - 1U].seconds < line.seconds);
    }
    const auto expected_seconds =
        otgy::core::sixteenth_row_to_seconds(
            kTiming, static_cast<double>(index));
    assert(expected_seconds);
    assert(line.seconds == *expected_seconds.value());
    if (line.kind == TimingGridLineKind::Measure) {
      ++measure_count;
    } else if (line.kind == TimingGridLineKind::Beat) {
      ++beat_count;
    } else {
      ++sixteenth_count;
    }
  }
  assert(measure_count == 4U);
  assert(beat_count == 12U);
  assert(sixteenth_count == 48U);
  assert(lines.front().seconds == 0.0);
  assert(lines.back().sixteenth_row == 63U);
  assert(lines.back().seconds < 9.6);
}

void test_half_open_ranges_and_offset() {
  constexpr TimingParameters kDefault{100.0, 0.0};
  const auto aligned =
      otgy::core::generate_timing_grid(kDefault, 0.6, 1.2);
  const auto& aligned_lines = grid_of(aligned).lines();
  assert(aligned_lines.size() == 4U);
  assert(aligned_lines.front().sixteenth_row == 4U);
  assert(aligned_lines.front().seconds == 0.6);
  assert(aligned_lines.front().kind == TimingGridLineKind::Beat);
  assert(aligned_lines.back().sixteenth_row == 7U);

  const double after_beat = std::nextafter(0.6, 1.0);
  const auto after =
      otgy::core::generate_timing_grid(kDefault, after_beat, 1.2);
  const auto& after_lines = grid_of(after).lines();
  assert(after_lines.size() == 3U);
  assert(after_lines.front().sixteenth_row == 5U);

  constexpr double kDetectorOffset = 0.089977324263038547;
  constexpr TimingParameters kDetected{
      129.24970691676435, -kDetectorOffset};
  const auto detected = otgy::core::generate_timing_grid(
      kDetected, 0.0, kDetectorOffset);
  assert(grid_of(detected).lines().empty());
  const auto detected_with_beat = otgy::core::generate_timing_grid(
      kDetected, 0.0,
      std::nextafter(kDetectorOffset,
                     std::numeric_limits<double>::infinity()));
  const auto& detected_lines = grid_of(detected_with_beat).lines();
  assert(detected_lines.size() == 1U);
  assert(detected_lines[0].sixteenth_row == 0U);
  assert(detected_lines[0].seconds == kDetectorOffset);
  assert(detected_lines[0].kind == TimingGridLineKind::Measure);
}

void test_zero_bpm_and_empty_ranges() {
  for (const double bpm : {0.0, -0.0}) {
    const auto result = otgy::core::generate_timing_grid(
        {bpm, 0.125}, -100.0, 100.0);
    assert(grid_of(result).lines().empty());
  }

  assert(grid_of(otgy::core::generate_timing_grid(
                     {100.0, 0.0}, 1.0, 1.0))
             .lines()
             .empty());
  assert(grid_of(otgy::core::generate_timing_grid(
                     {100.0, -30.0}, -29.0, -28.0))
             .lines()
             .empty());
}

void test_errors_and_ownership() {
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();

  for (const auto range : {
           std::array<double, 2U>{kNan, 1.0},
           std::array<double, 2U>{0.0, kInfinity},
           std::array<double, 2U>{1.0, 0.0},
       }) {
    assert_error(otgy::core::generate_timing_grid(
                     {100.0, 0.0}, range[0], range[1]),
                 TimingGridError::InvalidRange);
  }
  assert_error(otgy::core::generate_timing_grid(
                   {-1.0, 0.0}, 0.0, 1.0),
               TimingGridError::InvalidTiming);
  assert_error(otgy::core::generate_timing_grid(
                   {100.0, 31.0}, 0.0, 1.0),
               TimingGridError::InvalidTiming);
  assert_error(otgy::core::generate_timing_grid(
                   {100'000.0, 0.0}, 0.0, 151.0),
               TimingGridError::RangeTooLarge);
  assert_error(otgy::core::generate_timing_grid(
                   {100.0, 0.0}, 0.0,
                   std::numeric_limits<double>::max()),
               TimingGridError::RangeTooLarge);
  assert_error(otgy::core::generate_timing_grid(
                   {std::numeric_limits<double>::denorm_min(), 0.0},
                   0.0, 1.0),
               TimingGridError::InvalidTiming);

  auto result = otgy::core::generate_timing_grid(
      {100.0, 0.0}, 0.0, 0.6);
  assert(result);
  const auto copy = *result.grid();
  const auto moved = std::move(result).take_grid();
  assert(copy.lines().size() == 4U);
  assert(moved.lines().size() == 4U);
  assert(copy.lines().data() != moved.lines().data());

  assert(otgy::core::timing_grid_error_message(TimingGridError::None)
             .empty());
  constexpr std::array<TimingGridError, 4U> kErrors{{
      TimingGridError::InvalidRange,
      TimingGridError::InvalidTiming,
      TimingGridError::RangeTooLarge,
      TimingGridError::AllocationFailure,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::timing_grid_error_message(error).empty());
  }
  assert(!otgy::core::timing_grid_error_message(
              static_cast<TimingGridError>(255))
              .empty());
}

}  // namespace

int main() {
  test_default_four_measure_grid();
  test_half_open_ranges_and_offset();
  test_zero_bpm_and_empty_ranges();
  test_errors_and_ownership();
  return 0;
}
