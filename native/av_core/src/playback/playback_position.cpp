// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_position.hpp"

#include <cmath>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] PlaybackPositionError validate_position(
    const PlaybackPosition& position) noexcept {
  if (position.phase() != PlaybackPhase::Paused &&
      position.phase() != PlaybackPhase::Playing) {
    return PlaybackPositionError::InvalidState;
  }
  if (!std::isfinite(position.audio_duration_seconds()) ||
      position.audio_duration_seconds() < 0.0) {
    return PlaybackPositionError::InvalidState;
  }
  if (!std::isfinite(position.timeline_seconds()) ||
      position.timeline_seconds() > position.audio_duration_seconds()) {
    return PlaybackPositionError::InvalidState;
  }
  return PlaybackPositionError::None;
}

}  // namespace

PlaybackPosition::PlaybackPosition(PlaybackPhase phase,
                                   double timeline_seconds,
                                   double audio_duration_seconds) noexcept
    : phase_(phase),
      timeline_seconds_(timeline_seconds),
      audio_duration_seconds_(audio_duration_seconds) {}

PlaybackPositionResult::PlaybackPositionResult(
    PlaybackPosition position) noexcept
    : storage_(std::move(position)) {}

PlaybackPositionResult::PlaybackPositionResult(
    PlaybackPositionError error) noexcept
    : storage_(error) {}

bool PlaybackPositionResult::has_value() const noexcept {
  return std::holds_alternative<PlaybackPosition>(storage_);
}

const PlaybackPosition* PlaybackPositionResult::position() const noexcept {
  return std::get_if<PlaybackPosition>(&storage_);
}

PlaybackPositionError PlaybackPositionResult::error() const noexcept {
  if (const auto* error =
          std::get_if<PlaybackPositionError>(&storage_)) {
    return *error;
  }
  return PlaybackPositionError::None;
}

PlaybackPositionResult start_playback(
    std::optional<double> audio_duration_seconds,
    double cursor_timeline_seconds) noexcept {
  if (!audio_duration_seconds.has_value()) {
    return PlaybackPositionResult{PlaybackPositionError::NoAudio};
  }
  if (!std::isfinite(*audio_duration_seconds) ||
      *audio_duration_seconds < 0.0) {
    return PlaybackPositionResult{
        PlaybackPositionError::InvalidAudioDuration};
  }
  if (!std::isfinite(cursor_timeline_seconds) ||
      cursor_timeline_seconds > *audio_duration_seconds) {
    return PlaybackPositionResult{PlaybackPositionError::InvalidPosition};
  }

  if (cursor_timeline_seconds == *audio_duration_seconds) {
    const auto phase = *audio_duration_seconds == 0.0
                           ? PlaybackPhase::Paused
                           : PlaybackPhase::Playing;
    return PlaybackPositionResult{
        PlaybackPosition{phase, 0.0, *audio_duration_seconds}};
  }
  return PlaybackPositionResult{PlaybackPosition{
      PlaybackPhase::Playing, cursor_timeline_seconds,
      *audio_duration_seconds}};
}

PlaybackPositionResult advance_playback(
    PlaybackPosition position, double elapsed_seconds) noexcept {
  if (validate_position(position) != PlaybackPositionError::None) {
    return PlaybackPositionResult{PlaybackPositionError::InvalidState};
  }
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
    return PlaybackPositionResult{
        PlaybackPositionError::InvalidElapsedTime};
  }
  if (position.phase() == PlaybackPhase::Paused) {
    return PlaybackPositionResult{position};
  }

  const double remaining =
      position.audio_duration_seconds() - position.timeline_seconds();
  if (std::isfinite(remaining) && elapsed_seconds >= remaining) {
    return PlaybackPositionResult{PlaybackPosition{
        PlaybackPhase::Paused, position.audio_duration_seconds(),
        position.audio_duration_seconds()}};
  }

  const double advanced = position.timeline_seconds() + elapsed_seconds;
  if (!std::isfinite(advanced)) {
    return PlaybackPositionResult{PlaybackPositionError::InvalidElapsedTime};
  }
  return PlaybackPositionResult{PlaybackPosition{
      PlaybackPhase::Playing, advanced,
      position.audio_duration_seconds()}};
}

PlaybackPositionResult advance_playback_frames(
    PlaybackPosition start_position, std::uint64_t played_frames,
    std::uint32_t sample_rate) noexcept {
  if (sample_rate == 0U) {
    return PlaybackPositionResult{PlaybackPositionError::InvalidSampleRate};
  }
  const double elapsed_seconds =
      static_cast<double>(played_frames) / static_cast<double>(sample_rate);
  return advance_playback(start_position, elapsed_seconds);
}

PlaybackPositionResult pause_playback(PlaybackPosition position) noexcept {
  if (validate_position(position) != PlaybackPositionError::None) {
    return PlaybackPositionResult{PlaybackPositionError::InvalidState};
  }
  return PlaybackPositionResult{PlaybackPosition{
      PlaybackPhase::Paused, position.timeline_seconds(),
      position.audio_duration_seconds()}};
}

}  // namespace otgy::core
