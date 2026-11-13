// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/playback/playback_pcm.hpp"
#include "av_core/timing/timing_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace otgy::core::detail {

constexpr double kSignedFrameLimit = 0x1p63;

[[nodiscard]] inline bool project_frame(std::uint64_t row,
                                        std::uint32_t sample_rate,
                                        TimingParameters timing,
                                        std::int64_t& frame) noexcept {
  const auto seconds =
      sixteenth_row_to_seconds(timing, static_cast<double>(row));
  if (!seconds) {
    return false;
  }
  const auto rounded =
      std::round(*seconds.value() * static_cast<double>(sample_rate));
  if (!std::isfinite(rounded) || rounded <= -kSignedFrameLimit ||
      rounded >= kSignedFrameLimit) {
    return false;
  }
  frame = static_cast<std::int64_t>(rounded);
  return true;
}

[[nodiscard]] inline bool subtract_frames(std::int64_t left,
                                          std::int64_t right,
                                          std::int64_t& result) noexcept {
  if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
      (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
    return false;
  }
  result = left - right;
  return true;
}

[[nodiscard]] inline std::int16_t read_sample_le(
    const std::uint8_t* bytes, std::size_t offset) noexcept {
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
  const auto value = bits <= 0x7fffU ? static_cast<std::int32_t>(bits)
                                    : static_cast<std::int32_t>(bits) - 65536;
  return static_cast<std::int16_t>(value);
}

inline void write_sample_le(std::uint8_t* bytes, std::size_t offset,
                            std::int16_t sample) noexcept {
  const auto bits = static_cast<std::uint16_t>(sample);
  bytes[offset] = static_cast<std::uint8_t>(bits & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>(bits >> 8U);
}

[[nodiscard]] inline std::int16_t saturating_add(std::int16_t left,
                                                 std::int16_t right) noexcept {
  const auto sum = static_cast<std::int32_t>(left) +
                   static_cast<std::int32_t>(right);
  return static_cast<std::int16_t>(
      std::max<std::int32_t>(-32768, std::min<std::int32_t>(32767, sum)));
}

[[nodiscard]] inline bool resampled_frame_count(
    const Pcm16& clip, std::uint32_t target_rate,
    std::uint64_t& output) noexcept {
  const auto frames = static_cast<std::uint64_t>(clip.frame_count());
  if (frames == 0U) {
    output = 0U;
    return true;
  }
  if (frames > UINT64_MAX / target_rate) {
    return false;
  }
  const auto numerator = frames * target_rate;
  const auto rate = static_cast<std::uint64_t>(clip.sample_rate());
  output = numerator / rate + (numerator % rate == 0U ? 0U : 1U);
  return true;
}

[[nodiscard]] inline std::int16_t resampled_sample(
    const Pcm16& clip, PcmChannel channel, std::uint64_t output_frame,
    std::uint32_t target_rate) noexcept {
  const auto source_rate = static_cast<std::uint64_t>(clip.sample_rate());
  const auto numerator = output_frame * source_rate;
  const auto base = numerator / target_rate;
  const auto remainder = numerator % target_rate;
  const auto last = static_cast<std::uint64_t>(clip.frame_count() - 1U);
  const auto first_index = std::min(base, last);
  const auto second_index = std::min(first_index + 1U, last);
  const auto* samples = clip.channel_data(channel);
  const auto first = static_cast<std::int64_t>(samples[first_index]);
  const auto second = static_cast<std::int64_t>(samples[second_index]);
  const auto scaled = (second - first) * static_cast<std::int64_t>(remainder);
  const auto divisor = static_cast<std::int64_t>(target_rate);
  const auto rounded = scaled >= 0 ? (scaled + divisor / 2) / divisor
                                   : (scaled - divisor / 2) / divisor;
  return static_cast<std::int16_t>(first + rounded);
}

/// Mixes one clip into an existing interleaved stereo PCM16 buffer. A negative
/// destination start skips the already-played leading frames of the clip.
[[nodiscard]] inline PlaybackPcmError mix_trigger(
    std::uint8_t* output, std::uint64_t output_frames,
    std::uint32_t target_rate, std::int64_t destination_start,
    const Pcm16& clip) noexcept {
  if (output == nullptr) {
    return PlaybackPcmError::None;
  }
  std::uint64_t clip_frames = 0U;
  if (!resampled_frame_count(clip, target_rate, clip_frames)) {
    return PlaybackPcmError::SizeOverflow;
  }
  if (clip_frames == 0U ||
      (destination_start >= 0 &&
       static_cast<std::uint64_t>(destination_start) >= output_frames)) {
    return PlaybackPcmError::None;
  }

  std::uint64_t source_begin = 0U;
  std::uint64_t destination_begin = 0U;
  if (destination_start < 0) {
    const auto skipped = static_cast<std::uint64_t>(-(destination_start + 1)) +
                         1U;
    if (skipped >= clip_frames) {
      return PlaybackPcmError::None;
    }
    source_begin = skipped;
  } else {
    destination_begin = static_cast<std::uint64_t>(destination_start);
  }
  const auto available_clip = clip_frames - source_begin;
  const auto available_output = output_frames - destination_begin;
  const auto mixed_frames = std::min(available_clip, available_output);
  constexpr std::size_t kBytesPerFrame = 4U;
  for (std::uint64_t index = 0U; index < mixed_frames; ++index) {
    const auto destination = destination_begin + index;
    if (destination >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                   kBytesPerFrame)) {
      return PlaybackPcmError::SizeOverflow;
    }
    const auto byte_offset = static_cast<std::size_t>(destination) *
                             kBytesPerFrame;
    const auto source = source_begin + index;
    const auto left = resampled_sample(clip, PcmChannel::Left, source,
                                       target_rate);
    const auto right = resampled_sample(clip, PcmChannel::Right, source,
                                        target_rate);
    write_sample_le(output, byte_offset,
                    saturating_add(read_sample_le(output, byte_offset), left));
    write_sample_le(
        output, byte_offset + 2U,
        saturating_add(read_sample_le(output, byte_offset + 2U), right));
  }
  return PlaybackPcmError::None;
}

/// Writes an interleaved stereo block from the planar song starting at
/// `source_start_frame`. Frames outside the song become silence.
inline void mix_song_block(const Pcm16& song, std::int64_t source_start_frame,
                           std::uint32_t frames,
                           std::uint8_t* output) noexcept {
  if (output == nullptr) {
    return;
  }
  const auto* left = song.channel_data(PcmChannel::Left);
  const auto* right = song.channel_data(PcmChannel::Right);
  const auto total = static_cast<std::int64_t>(song.frame_count());
  constexpr std::size_t kBytesPerFrame = 4U;
  for (std::uint32_t index = 0U; index < frames; ++index) {
    const auto source = source_start_frame + static_cast<std::int64_t>(index);
    std::int16_t left_value = 0;
    std::int16_t right_value = 0;
    if (source >= 0 && source < total) {
      left_value = left[source];
      right_value = right[source];
    }
    const auto byte_offset =
        static_cast<std::size_t>(index) * kBytesPerFrame;
    write_sample_le(output, byte_offset, left_value);
    write_sample_le(output, byte_offset + 2U, right_value);
  }
}

}  // namespace otgy::core::detail
