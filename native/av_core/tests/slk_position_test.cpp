// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_position.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

namespace {

using otgy::core::SlkPosition;
using otgy::core::SlkPositionError;
using otgy::core::SlkPositionResult;

[[nodiscard]] SlkPosition position_of(const SlkPositionResult& result) {
  assert(result);
  assert(result.error() == SlkPositionError::None);
  assert(result.position() != nullptr);
  return *result.position();
}

void assert_error(const SlkPositionResult& result,
                  SlkPositionError expected) {
  assert(!result);
  assert(result.position() == nullptr);
  assert(result.error() == expected);
}

void assert_position(const SlkPosition& position, std::uint64_t measure,
                     std::uint64_t beat, std::uint64_t sixteenth,
                     std::uint64_t total) {
  assert(position.measure == measure);
  assert(position.beat == beat);
  assert(position.sixteenth == sixteenth);
  assert(position.total_sixteenth == total);
}

void test_all_within_measure_combinations() {
  for (std::uint64_t measure : {0U, 1U, 123'456U}) {
    for (std::uint64_t beat = 0U; beat < 4U; ++beat) {
      for (std::uint64_t sixteenth = 0U; sixteenth < 4U;
           ++sixteenth) {
        const std::uint64_t expected =
            measure * 16U + beat * 4U + sixteenth;
        const auto composed = position_of(
            otgy::core::compose_slk_position(
                measure, beat, sixteenth));
        assert_position(composed, measure, beat, sixteenth, expected);

        const auto decomposed = position_of(
            otgy::core::decompose_slk_position(expected));
        assert_position(decomposed, measure, beat, sixteenth, expected);

        const auto validated = position_of(
            otgy::core::validate_slk_position(
                measure, beat, sixteenth, expected));
        assert_position(validated, measure, beat, sixteenth, expected);
      }
    }
  }
}

void test_measure_and_uint64_boundaries() {
  assert_position(position_of(otgy::core::decompose_slk_position(15U)),
                  0U, 3U, 3U, 15U);
  assert_position(position_of(otgy::core::decompose_slk_position(16U)),
                  1U, 0U, 0U, 16U);

  constexpr auto kMaximum = std::numeric_limits<std::uint64_t>::max();
  const auto maximum =
      position_of(otgy::core::decompose_slk_position(kMaximum));
  assert_position(maximum, kMaximum / 16U, 3U, 3U, kMaximum);
  const auto recomposed = position_of(otgy::core::compose_slk_position(
      maximum.measure, maximum.beat, maximum.sixteenth));
  assert_position(recomposed, maximum.measure, 3U, 3U, kMaximum);

  assert_error(otgy::core::compose_slk_position(
                   kMaximum / 16U + 1U, 0U, 0U),
               SlkPositionError::ArithmeticOverflow);
}

void test_component_and_consistency_errors() {
  for (const std::uint64_t beat : {4U, 5U, 255U}) {
    assert_error(otgy::core::compose_slk_position(0U, beat, 0U),
                 SlkPositionError::BeatOutOfRange);
    assert_error(otgy::core::validate_slk_position(0U, beat, 0U, 0U),
                 SlkPositionError::BeatOutOfRange);
  }
  for (const std::uint64_t sixteenth : {4U, 5U, 255U}) {
    assert_error(
        otgy::core::compose_slk_position(0U, 0U, sixteenth),
        SlkPositionError::SixteenthOutOfRange);
    assert_error(
        otgy::core::validate_slk_position(0U, 0U, sixteenth, 0U),
        SlkPositionError::SixteenthOutOfRange);
  }

  assert_error(otgy::core::validate_slk_position(1U, 2U, 3U, 26U),
               SlkPositionError::InconsistentTotal);
  assert_error(otgy::core::validate_slk_position(1U, 2U, 3U, 28U),
               SlkPositionError::InconsistentTotal);
}

void test_error_messages() {
  assert(otgy::core::slk_position_error_message(SlkPositionError::None)
             .empty());
  constexpr std::array<SlkPositionError, 4U> kErrors{{
      SlkPositionError::BeatOutOfRange,
      SlkPositionError::SixteenthOutOfRange,
      SlkPositionError::ArithmeticOverflow,
      SlkPositionError::InconsistentTotal,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::slk_position_error_message(error).empty());
  }
  assert(!otgy::core::slk_position_error_message(
              static_cast<SlkPositionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_all_within_measure_combinations();
  test_measure_and_uint64_boundaries();
  test_component_and_consistency_errors();
  test_error_messages();
  return 0;
}
