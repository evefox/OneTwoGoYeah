// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_pcm.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace otgy::core {
namespace {

void write_sample_le(std::vector<std::uint8_t>& output, std::size_t offset,
                     Pcm16::Sample sample) noexcept {
  const auto value = static_cast<std::uint16_t>(sample);
  output[offset] = static_cast<std::uint8_t>(value & 0xffU);
  output[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

}  // namespace

PlaybackPcm::PlaybackPcm(
    std::uint32_t sample_rate, std::uint64_t frame_count,
    std::vector<std::uint8_t>&& interleaved_pcm16le) noexcept
    : sample_rate_(sample_rate),
      frame_count_(frame_count),
      interleaved_pcm16le_(std::move(interleaved_pcm16le)) {}

PlaybackPcmResult::PlaybackPcmResult(PlaybackPcm pcm) noexcept
    : storage_(std::move(pcm)) {}

PlaybackPcmResult::PlaybackPcmResult(PlaybackPcmError error) noexcept
    : storage_(error) {}

PlaybackPcmResult::operator bool() const noexcept {
  return std::holds_alternative<PlaybackPcm>(storage_);
}

const PlaybackPcm* PlaybackPcmResult::pcm() const noexcept {
  return std::get_if<PlaybackPcm>(&storage_);
}

PlaybackPcmError PlaybackPcmResult::error() const noexcept {
  const auto* error = std::get_if<PlaybackPcmError>(&storage_);
  return error == nullptr ? PlaybackPcmError::None : *error;
}

PlaybackPcm PlaybackPcmResult::take_pcm() && {
  return std::move(std::get<PlaybackPcm>(storage_));
}

PlaybackPcmResult render_playback_pcm(
    const Pcm16& pcm, double timeline_start_seconds) noexcept {
  const double duration = static_cast<double>(pcm.frame_count()) /
                          static_cast<double>(pcm.sample_rate());
  if (!std::isfinite(timeline_start_seconds) ||
      timeline_start_seconds > duration) {
    return PlaybackPcmResult{PlaybackPcmError::InvalidTimelineStart};
  }

  const double rounded = std::round(
      timeline_start_seconds * static_cast<double>(pcm.sample_rate()));
  constexpr double kSignedFrameLimit = 0x1p63;
  if (!std::isfinite(rounded) || rounded <= -kSignedFrameLimit ||
      rounded >= kSignedFrameLimit) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }
  const auto signed_start_frame = static_cast<std::int64_t>(rounded);
  const auto input_frames = pcm.frame_count();
  std::size_t silence_frames = 0U;
  std::size_t source_offset = 0U;
  if (signed_start_frame < 0) {
    const auto silence = static_cast<std::uint64_t>(-signed_start_frame);
    if (silence > static_cast<std::uint64_t>(
                      std::numeric_limits<std::size_t>::max())) {
      return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
    }
    silence_frames = static_cast<std::size_t>(silence);
  } else {
    const auto offset = static_cast<std::uint64_t>(signed_start_frame);
    source_offset = offset >= static_cast<std::uint64_t>(input_frames)
                        ? input_frames
                        : static_cast<std::size_t>(offset);
  }

  const auto copied_frames = input_frames - source_offset;
  if (silence_frames >
      std::numeric_limits<std::size_t>::max() - copied_frames) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }
  const auto output_frames = silence_frames + copied_frames;
  constexpr std::size_t kBytesPerFrame = 4U;
  if (output_frames >
      std::numeric_limits<std::size_t>::max() / kBytesPerFrame) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }

  std::vector<std::uint8_t> bytes;
  try {
    bytes.resize(output_frames * kBytesPerFrame, 0U);
  } catch (const std::bad_alloc&) {
    return PlaybackPcmResult{PlaybackPcmError::AllocationFailure};
  } catch (const std::length_error&) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }

  const auto* left = pcm.channel_data(PcmChannel::Left);
  const auto* right = pcm.channel_data(PcmChannel::Right);
  for (std::size_t index = 0U; index < copied_frames; ++index) {
    const auto destination = silence_frames + index;
    const auto byte_offset = destination * kBytesPerFrame;
    write_sample_le(bytes, byte_offset, left[source_offset + index]);
    write_sample_le(bytes, byte_offset + 2U, right[source_offset + index]);
  }
  return PlaybackPcmResult{PlaybackPcm{
      pcm.sample_rate(), static_cast<std::uint64_t>(output_frames),
      std::move(bytes)}};
}

PlaybackFrameLimitResult::PlaybackFrameLimitResult(
    std::uint64_t frame_count) noexcept
    : storage_(frame_count) {}

PlaybackFrameLimitResult::PlaybackFrameLimitResult(
    PlaybackPcmError error) noexcept
    : storage_(error) {}

PlaybackFrameLimitResult::operator bool() const noexcept {
  return std::holds_alternative<std::uint64_t>(storage_);
}

const std::uint64_t* PlaybackFrameLimitResult::frame_count() const noexcept {
  return std::get_if<std::uint64_t>(&storage_);
}

PlaybackPcmError PlaybackFrameLimitResult::error() const noexcept {
  const auto* error = std::get_if<PlaybackPcmError>(&storage_);
  return error == nullptr ? PlaybackPcmError::None : *error;
}

PlaybackFrameLimitResult calculate_playback_frame_limit(
    std::uint32_t sample_rate, double keep_seconds) noexcept {
  if (sample_rate == 0U || !std::isfinite(keep_seconds) ||
      keep_seconds < 0.0) {
    return PlaybackFrameLimitResult{PlaybackPcmError::InvalidKeepDuration};
  }
  const double rounded =
      std::round(keep_seconds * static_cast<double>(sample_rate));
  if (!std::isfinite(rounded) || rounded < 0.0) {
    return PlaybackFrameLimitResult{PlaybackPcmError::SizeOverflow};
  }
  if (rounded >=
      static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return PlaybackFrameLimitResult{PlaybackPcmError::SizeOverflow};
  }
  return PlaybackFrameLimitResult{static_cast<std::uint64_t>(rounded)};
}

}  // namespace otgy::core
