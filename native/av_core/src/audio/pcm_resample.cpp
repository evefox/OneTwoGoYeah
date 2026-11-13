// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm_resample.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace otgy::core {

Pcm16ResampleResult resample_pcm(const Pcm16& source,
                                 std::uint32_t target_sample_rate) noexcept {
  if (target_sample_rate == 0U) {
    return {PcmError::InvalidSampleRate, std::nullopt};
  }
  const std::uint32_t source_rate = source.sample_rate();
  if (target_sample_rate == source_rate) {
    return {PcmError::None, Pcm16(source)};
  }

  const std::uint64_t source_frames = source.frame_count();
  if (source_frames >
      (std::numeric_limits<std::uint64_t>::max() - source_rate / 2U) /
          target_sample_rate) {
    return {PcmError::SizeOverflow, std::nullopt};
  }
  const std::uint64_t output_frames_64 =
      (source_frames * target_sample_rate + source_rate / 2U) / source_rate;
  if (output_frames_64 >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return {PcmError::SizeOverflow, std::nullopt};
  }

  auto allocation = Pcm16::allocate(
      target_sample_rate, static_cast<std::size_t>(output_frames_64));
  if (!allocation) {
    return {allocation.error(), std::nullopt};
  }
  auto target = std::move(allocation).take_pcm();
  if (output_frames_64 == 0U || source_frames == 0U) {
    return {PcmError::None, std::move(target)};
  }

  const auto* source_left = source.channel_data(PcmChannel::Left);
  const auto* source_right = source.channel_data(PcmChannel::Right);
  auto* target_left = target.mutable_channel_data(PcmChannel::Left);
  auto* target_right = target.mutable_channel_data(PcmChannel::Right);

  const auto interpolate = [](Pcm16::Sample left, Pcm16::Sample right,
                              double fraction) {
    const double value =
        static_cast<double>(left) +
        (static_cast<double>(right) - static_cast<double>(left)) * fraction;
    return static_cast<Pcm16::Sample>(std::lround(value));
  };

  for (std::size_t index = 0U; index < target.frame_count(); ++index) {
    const double position =
        static_cast<double>(index) * static_cast<double>(source_rate) /
        static_cast<double>(target_sample_rate);
    auto first = static_cast<std::size_t>(position);
    if (first >= source.frame_count()) {
      first = source.frame_count() - 1U;
    }
    const auto second =
        first + 1U < source.frame_count() ? first + 1U : first;
    const double fraction = position - static_cast<double>(first);
    target_left[index] =
        interpolate(source_left[first], source_left[second], fraction);
    target_right[index] =
        interpolate(source_right[first], source_right[second], fraction);
  }
  return {PcmError::None, std::move(target)};
}

}  // namespace otgy::core
