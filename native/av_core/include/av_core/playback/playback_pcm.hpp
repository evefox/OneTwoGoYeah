// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class PlaybackPcmError : std::uint8_t {
  None = 0,
  InvalidTimelineStart = 1,
  SizeOverflow = 2,
  AllocationFailure = 3,
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

}  // namespace otgy::core
