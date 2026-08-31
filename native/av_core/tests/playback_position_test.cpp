// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_position.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace {

using otgy::core::PlaybackPhase;
using otgy::core::PlaybackPosition;
using otgy::core::PlaybackPositionError;
using otgy::core::PlaybackPositionResult;

[[nodiscard]] PlaybackPosition position_of(
    const PlaybackPositionResult& result) {
  assert(result);
  assert(result.error() == PlaybackPositionError::None);
  assert(result.position() != nullptr);
  return *result.position();
}

void assert_error(const PlaybackPositionResult& result,
                  PlaybackPositionError expected) {
  assert(!result);
  assert(result.position() == nullptr);
  assert(result.error() == expected);
}

[[nodiscard]] std::uint64_t bits_of(double value) {
  std::uint64_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void test_start_advance_and_natural_end() {
  constexpr double kCursor = 2.345678901234567;
  auto position = position_of(otgy::core::start_playback(10.0, kCursor));
  assert(position.phase() == PlaybackPhase::Playing);
  assert(bits_of(position.timeline_seconds()) == bits_of(kCursor));
  assert(position.audio_duration_seconds() == 10.0);

  position = position_of(otgy::core::advance_playback(position, 1.25));
  assert(position.phase() == PlaybackPhase::Playing);
  assert(position.timeline_seconds() == kCursor + 1.25);

  position = position_of(otgy::core::advance_playback(position, 100.0));
  assert(position.phase() == PlaybackPhase::Paused);
  assert(position.timeline_seconds() == 10.0);

  const auto unchanged =
      position_of(otgy::core::advance_playback(position, 1.0));
  assert(unchanged.phase() == PlaybackPhase::Paused);
  assert(unchanged.timeline_seconds() == 10.0);
}

void test_start_from_end_and_manual_pause() {
  auto restarted = position_of(otgy::core::start_playback(10.0, 10.0));
  assert(restarted.phase() == PlaybackPhase::Playing);
  assert(restarted.timeline_seconds() == 0.0);

  restarted = position_of(otgy::core::advance_playback(restarted, 0.125));
  const auto before_pause_bits = bits_of(restarted.timeline_seconds());
  const auto paused = position_of(otgy::core::pause_playback(restarted));
  assert(paused.phase() == PlaybackPhase::Paused);
  assert(bits_of(paused.timeline_seconds()) == before_pause_bits);
  assert(paused.audio_duration_seconds() == 10.0);
}

void test_negative_lead_in_and_zero_duration() {
  auto lead_in = position_of(otgy::core::start_playback(10.0, -0.5));
  assert(lead_in.phase() == PlaybackPhase::Playing);
  assert(lead_in.timeline_seconds() == -0.5);
  lead_in = position_of(otgy::core::advance_playback(lead_in, 0.25));
  assert(lead_in.timeline_seconds() == -0.25);
  lead_in = position_of(otgy::core::advance_playback(lead_in, 0.25));
  assert(lead_in.timeline_seconds() == 0.0);
  assert(lead_in.phase() == PlaybackPhase::Playing);

  const auto empty_at_end =
      position_of(otgy::core::start_playback(0.0, 0.0));
  assert(empty_at_end.phase() == PlaybackPhase::Paused);
  assert(empty_at_end.timeline_seconds() == 0.0);

  auto empty_lead_in =
      position_of(otgy::core::start_playback(0.0, -0.5));
  assert(empty_lead_in.phase() == PlaybackPhase::Playing);
  empty_lead_in = position_of(
      otgy::core::advance_playback(empty_lead_in, 0.5));
  assert(empty_lead_in.phase() == PlaybackPhase::Paused);
  assert(empty_lead_in.timeline_seconds() == 0.0);
}

void test_errors_and_messages() {
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();

  assert_error(otgy::core::start_playback(std::nullopt, 0.0),
               PlaybackPositionError::NoAudio);
  for (const double duration : {kNan, kInfinity, -0.001}) {
    assert_error(otgy::core::start_playback(duration, 0.0),
                 PlaybackPositionError::InvalidAudioDuration);
  }
  for (const double cursor : {kNan, kInfinity, 10.001}) {
    assert_error(otgy::core::start_playback(10.0, cursor),
                 PlaybackPositionError::InvalidPosition);
  }

  const PlaybackPosition valid{PlaybackPhase::Playing, 1.0, 10.0};
  for (const double elapsed : {kNan, kInfinity, -0.001}) {
    assert_error(otgy::core::advance_playback(valid, elapsed),
                 PlaybackPositionError::InvalidElapsedTime);
  }
  for (const PlaybackPosition invalid : {
           PlaybackPosition{static_cast<PlaybackPhase>(255), 1.0, 10.0},
           PlaybackPosition{PlaybackPhase::Playing, 11.0, 10.0},
           PlaybackPosition{PlaybackPhase::Playing, kNan, 10.0},
           PlaybackPosition{PlaybackPhase::Playing, 1.0, -1.0},
       }) {
    assert_error(otgy::core::advance_playback(invalid, 1.0),
                 PlaybackPositionError::InvalidState);
    assert_error(otgy::core::pause_playback(invalid),
                 PlaybackPositionError::InvalidState);
  }

  assert(otgy::core::playback_position_error_message(
             PlaybackPositionError::None)
             .empty());
  constexpr std::array<PlaybackPositionError, 5U> kErrors{{
      PlaybackPositionError::NoAudio,
      PlaybackPositionError::InvalidAudioDuration,
      PlaybackPositionError::InvalidPosition,
      PlaybackPositionError::InvalidElapsedTime,
      PlaybackPositionError::InvalidState,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::playback_position_error_message(error).empty());
  }
  assert(!otgy::core::playback_position_error_message(
              static_cast<PlaybackPositionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_start_advance_and_natural_end();
  test_start_from_end_and_manual_pause();
  test_negative_lead_in_and_zero_duration();
  test_errors_and_messages();
  return 0;
}
