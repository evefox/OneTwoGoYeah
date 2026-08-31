// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_position.hpp"

#include <limits>

namespace otgy::core {
namespace {

constexpr std::uint64_t kBeatsPerMeasure = 4U;
constexpr std::uint64_t kSixteenthsPerBeat = 4U;
constexpr std::uint64_t kSixteenthsPerMeasure =
    kBeatsPerMeasure * kSixteenthsPerBeat;

}  // namespace

SlkPositionResult::SlkPositionResult(SlkPosition position) noexcept
    : storage_(position) {}

SlkPositionResult::SlkPositionResult(SlkPositionError error) noexcept
    : storage_(error) {}

bool SlkPositionResult::has_value() const noexcept {
  return std::holds_alternative<SlkPosition>(storage_);
}

const SlkPosition* SlkPositionResult::position() const noexcept {
  return std::get_if<SlkPosition>(&storage_);
}

SlkPositionError SlkPositionResult::error() const noexcept {
  if (const auto* error = std::get_if<SlkPositionError>(&storage_)) {
    return *error;
  }
  return SlkPositionError::None;
}

SlkPositionResult compose_slk_position(
    std::uint64_t measure, std::uint64_t beat,
    std::uint64_t sixteenth) noexcept {
  if (beat >= kBeatsPerMeasure) {
    return SlkPositionResult{SlkPositionError::BeatOutOfRange};
  }
  if (sixteenth >= kSixteenthsPerBeat) {
    return SlkPositionResult{SlkPositionError::SixteenthOutOfRange};
  }

  const std::uint64_t within_measure =
      beat * kSixteenthsPerBeat + sixteenth;
  constexpr auto kMaximum = std::numeric_limits<std::uint64_t>::max();
  if (measure > (kMaximum - within_measure) /
                    kSixteenthsPerMeasure) {
    return SlkPositionResult{SlkPositionError::ArithmeticOverflow};
  }
  const std::uint64_t total =
      measure * kSixteenthsPerMeasure + within_measure;
  return SlkPositionResult{
      SlkPosition{measure, beat, sixteenth, total}};
}

SlkPositionResult decompose_slk_position(
    std::uint64_t total_sixteenth) noexcept {
  const std::uint64_t within_measure =
      total_sixteenth % kSixteenthsPerMeasure;
  return SlkPositionResult{SlkPosition{
      total_sixteenth / kSixteenthsPerMeasure,
      within_measure / kSixteenthsPerBeat,
      within_measure % kSixteenthsPerBeat,
      total_sixteenth}};
}

SlkPositionResult validate_slk_position(
    std::uint64_t measure, std::uint64_t beat, std::uint64_t sixteenth,
    std::uint64_t total_sixteenth) noexcept {
  const auto composed = compose_slk_position(measure, beat, sixteenth);
  if (!composed) {
    return SlkPositionResult{composed.error()};
  }
  if (composed.position()->total_sixteenth != total_sixteenth) {
    return SlkPositionResult{SlkPositionError::InconsistentTotal};
  }
  return composed;
}

}  // namespace otgy::core
