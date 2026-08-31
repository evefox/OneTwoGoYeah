// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class PlaybackPhase : std::uint8_t {
  Paused = 0,
  Playing = 1,
};

class PlaybackPosition final {
 public:
  PlaybackPosition(PlaybackPhase phase, double timeline_seconds,
                   double audio_duration_seconds) noexcept;

  [[nodiscard]] PlaybackPhase phase() const noexcept {
    return phase_;
  }
  [[nodiscard]] double timeline_seconds() const noexcept {
    return timeline_seconds_;
  }
  [[nodiscard]] double audio_duration_seconds() const noexcept {
    return audio_duration_seconds_;
  }

 private:
  PlaybackPhase phase_;
  double timeline_seconds_;
  double audio_duration_seconds_;
};

enum class PlaybackPositionError : std::uint8_t {
  None = 0,
  NoAudio = 1,
  InvalidAudioDuration = 2,
  InvalidPosition = 3,
  InvalidElapsedTime = 4,
  InvalidState = 5,
  InvalidSampleRate = 6,
};

[[nodiscard]] constexpr std::string_view playback_position_error_message(
    PlaybackPositionError error) noexcept {
  switch (error) {
    case PlaybackPositionError::None:
      return {};
    case PlaybackPositionError::NoAudio:
      return "audio must be loaded before playback";
    case PlaybackPositionError::InvalidAudioDuration:
      return "audio duration must be finite and non-negative";
    case PlaybackPositionError::InvalidPosition:
      return "playback position must be finite and not exceed audio end";
    case PlaybackPositionError::InvalidElapsedTime:
      return "elapsed playback time must be finite and non-negative";
    case PlaybackPositionError::InvalidState:
      return "playback state is invalid";
    case PlaybackPositionError::InvalidSampleRate:
      return "playback sample rate must be greater than zero";
  }
  return "unknown playback position error";
}

class PlaybackPositionResult final {
 public:
  explicit PlaybackPositionResult(PlaybackPosition position) noexcept;
  explicit PlaybackPositionResult(PlaybackPositionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const PlaybackPosition* position() const noexcept;
  [[nodiscard]] PlaybackPositionError error() const noexcept;

 private:
  std::variant<PlaybackPosition, PlaybackPositionError> storage_;
};

/// Starts from the exact timeline cursor. A cursor equal to audio end restarts
/// at zero. Finite negative cursor values are valid pre-audio timeline time.
[[nodiscard]] PlaybackPositionResult start_playback(
    std::optional<double> audio_duration_seconds,
    double cursor_timeline_seconds) noexcept;

/// Advances a playing position by elapsed wall/audio time. Reaching audio end
/// clamps to the exact duration and transitions to Paused. A paused state is
/// returned unchanged.
[[nodiscard]] PlaybackPositionResult advance_playback(
    PlaybackPosition position, double elapsed_seconds) noexcept;

/// Advances from a stable starting position by an audio device's exact played
/// frame count. Frame-to-second projection remains in the shared core.
[[nodiscard]] PlaybackPositionResult advance_playback_frames(
    PlaybackPosition start_position, std::uint64_t played_frames,
    std::uint32_t sample_rate) noexcept;

/// Transitions to Paused without changing the exact timeline position.
[[nodiscard]] PlaybackPositionResult pause_playback(
    PlaybackPosition position) noexcept;

}  // namespace otgy::core
