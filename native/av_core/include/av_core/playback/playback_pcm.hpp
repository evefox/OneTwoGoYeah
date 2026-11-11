// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace otgy::core {

enum class PlaybackPcmError : std::uint8_t {
  None = 0,
  InvalidTimelineStart = 1,
  SizeOverflow = 2,
  AllocationFailure = 3,
  InvalidSoundConfiguration = 4,
  MissingSoundAsset = 5,
  SoundTimingFailure = 6,
  InvalidKeepDuration = 7,
};

[[nodiscard]] constexpr std::string_view playback_pcm_error_message(
    PlaybackPcmError error) noexcept {
  switch (error) {
    case PlaybackPcmError::None:
      return {};
    case PlaybackPcmError::InvalidTimelineStart:
      return "playback start must be finite and not exceed audio end";
    case PlaybackPcmError::SizeOverflow:
      return "playback PCM dimensions exceed the representable size";
    case PlaybackPcmError::AllocationFailure:
      return "unable to allocate playback PCM frames";
    case PlaybackPcmError::InvalidSoundConfiguration:
      return "playback sound configuration is invalid";
    case PlaybackPcmError::MissingSoundAsset:
      return "playback sound bank is missing a required asset";
    case PlaybackPcmError::SoundTimingFailure:
      return "playback sound event cannot be projected to an audio frame";
    case PlaybackPcmError::InvalidKeepDuration:
      return "playback keep duration must be finite and non-negative";
  }
  return "unknown playback PCM error";
}

class PlaybackPcm final {
 public:
  PlaybackPcm(std::uint32_t sample_rate, std::uint64_t frame_count,
              std::vector<std::uint8_t>&& interleaved_pcm16le) noexcept;

  [[nodiscard]] std::uint32_t sample_rate() const noexcept {
    return sample_rate_;
  }
  [[nodiscard]] std::uint64_t frame_count() const noexcept {
    return frame_count_;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return interleaved_pcm16le_;
  }
  [[nodiscard]] std::vector<std::uint8_t> take_bytes() && noexcept {
    return std::move(interleaved_pcm16le_);
  }

 private:
  std::uint32_t sample_rate_;
  std::uint64_t frame_count_;
  std::vector<std::uint8_t> interleaved_pcm16le_;
};

class PlaybackPcmResult final {
 public:
  explicit PlaybackPcmResult(PlaybackPcm pcm) noexcept;
  explicit PlaybackPcmResult(PlaybackPcmError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const PlaybackPcm* pcm() const noexcept;
  [[nodiscard]] PlaybackPcmError error() const noexcept;
  [[nodiscard]] PlaybackPcm take_pcm() &&;

 private:
  std::variant<PlaybackPcm, PlaybackPcmError> storage_;
};

/// Renders the canonical planar PCM as interleaved stereo PCM16LE beginning at
/// an exact timeline position. The position is projected to the nearest frame,
/// with exact half frames rounded away from zero. Negative positions prepend
/// silence; non-negative positions skip source frames. The input is unchanged.
[[nodiscard]] PlaybackPcmResult render_playback_pcm(
    const Pcm16& pcm, double timeline_start_seconds) noexcept;

/// Keeps at most `keep_seconds` from the beginning of an already rendered
/// playback buffer.
///
/// The keep duration is projected to the nearest frame with exact half frames
/// rounded away from zero. The caller uses the returned frame count to slice
/// the interleaved buffer; the PCM mixing itself is never re-run.
class PlaybackFrameLimitResult final {
 public:
  explicit PlaybackFrameLimitResult(std::uint64_t frame_count) noexcept;
  explicit PlaybackFrameLimitResult(PlaybackPcmError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::uint64_t* frame_count() const noexcept;
  [[nodiscard]] PlaybackPcmError error() const noexcept;

 private:
  std::variant<std::uint64_t, PlaybackPcmError> storage_;
};

[[nodiscard]] PlaybackFrameLimitResult calculate_playback_frame_limit(
    std::uint32_t sample_rate, double keep_seconds) noexcept;

}  // namespace otgy::core
