// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_end.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace otgy::core {
namespace {

constexpr double kMaximumBpm = 100'000.0;

[[nodiscard]] AudioEndError map_pcm_error(PcmError error) noexcept {
  switch (error) {
    case PcmError::AllocationFailure:
      return AudioEndError::AllocationFailure;
    case PcmError::SizeOverflow:
      return AudioEndError::SizeOverflow;
    case PcmError::InvalidSampleRate:
      return AudioEndError::InvalidSampleRate;
    case PcmError::None:
    case PcmError::ChannelFrameCountMismatch:
    case PcmError::NullChannelData:
      return AudioEndError::SizeOverflow;
  }
  return AudioEndError::SizeOverflow;
}

[[nodiscard]] AudioEndError validate_bpm(double bpm) noexcept {
  if (!std::isfinite(bpm) || bpm <= 0.0 || bpm > kMaximumBpm) {
    return AudioEndError::InvalidBpm;
  }
  return AudioEndError::None;
}

}  // namespace

AudioEndPlanResult::AudioEndPlanResult(AudioEndPlan plan) noexcept
    : storage_(plan) {}

AudioEndPlanResult::AudioEndPlanResult(AudioEndError error) noexcept
    : storage_(error) {}

AudioEndPlanResult::operator bool() const noexcept {
  return std::holds_alternative<AudioEndPlan>(storage_);
}

const AudioEndPlan* AudioEndPlanResult::plan() const noexcept {
  return std::get_if<AudioEndPlan>(&storage_);
}

AudioEndError AudioEndPlanResult::error() const noexcept {
  const auto* error = std::get_if<AudioEndError>(&storage_);
  return error == nullptr ? AudioEndError::None : *error;
}

AudioEndSecondsResult::AudioEndSecondsResult(double seconds) noexcept
    : storage_(seconds) {}

AudioEndSecondsResult::AudioEndSecondsResult(AudioEndError error) noexcept
    : storage_(error) {}

AudioEndSecondsResult::operator bool() const noexcept {
  return std::holds_alternative<double>(storage_);
}

const double* AudioEndSecondsResult::seconds() const noexcept {
  return std::get_if<double>(&storage_);
}

AudioEndError AudioEndSecondsResult::error() const noexcept {
  const auto* error = std::get_if<AudioEndError>(&storage_);
  return error == nullptr ? AudioEndError::None : *error;
}

AudioEndSecondsResult calculate_audio_end_export_seconds(
    std::uint64_t sixteenth_row, double bpm) noexcept {
  const auto bpm_error = validate_bpm(bpm);
  if (bpm_error != AudioEndError::None) {
    return AudioEndSecondsResult{bpm_error};
  }
  if (sixteenth_row % 4U != 0U) {
    return AudioEndSecondsResult{AudioEndError::InvalidBeatRow};
  }

  // The exporter aligns beat zero with exported time zero, so the exported
  // duration is the chart beat time without the audio Offset subtraction.
  const double seconds =
      (static_cast<double>(sixteenth_row) / 4.0) * (60.0 / bpm);
  if (!std::isfinite(seconds) || seconds < 0.0) {
    return AudioEndSecondsResult{AudioEndError::SizeOverflow};
  }
  return AudioEndSecondsResult{seconds};
}

AudioEndPlanResult calculate_audio_end_plan(
    std::uint32_t sample_rate, std::size_t source_frame_count,
    std::int64_t signed_shift_frames,
    std::size_t after_offset_frame_count, double bpm,
    AudioEndSpec spec) noexcept {
  if (sample_rate == 0U) {
    return AudioEndPlanResult{AudioEndError::InvalidSampleRate};
  }

  if (spec.mode == AudioEndMode::SourceEnd) {
    const auto dimensions =
        validate_pcm_dimensions(sample_rate, after_offset_frame_count);
    if (dimensions != PcmError::None) {
      return AudioEndPlanResult{map_pcm_error(dimensions)};
    }
    return AudioEndPlanResult{
        AudioEndPlan{after_offset_frame_count}};
  }

  const auto seconds =
      calculate_audio_end_export_seconds(spec.sixteenth_row, bpm);
  if (!seconds) {
    return AudioEndPlanResult{seconds.error()};
  }

  const double frame_value =
      std::round(*seconds.seconds() * static_cast<double>(sample_rate));
  if (!std::isfinite(frame_value) || frame_value <= 0.0) {
    return AudioEndPlanResult{AudioEndError::EmptyOutput};
  }
  if (!(frame_value <
        static_cast<double>(std::numeric_limits<std::size_t>::max()))) {
    return AudioEndPlanResult{AudioEndError::SizeOverflow};
  }
  const auto output_frame_count = static_cast<std::size_t>(frame_value);

  // A custom end must leave at least one decoded source frame in the output.
  // A positive head shift occupies the beginning of the exported timeline.
  const auto audio_start_frame =
      signed_shift_frames > 0
          ? static_cast<std::size_t>(signed_shift_frames)
          : std::size_t{0};
  if (source_frame_count == 0U || output_frame_count <= audio_start_frame) {
    return AudioEndPlanResult{AudioEndError::EmptyOutput};
  }

  const auto dimensions =
      validate_pcm_dimensions(sample_rate, output_frame_count);
  if (dimensions != PcmError::None) {
    return AudioEndPlanResult{map_pcm_error(dimensions)};
  }
  return AudioEndPlanResult{AudioEndPlan{output_frame_count}};
}

PcmResult apply_audio_end_window(const Pcm16& pcm,
                                 std::int64_t signed_shift_frames,
                                 std::size_t output_frame_count) noexcept {
  auto allocated = Pcm16::allocate(pcm.sample_rate(), output_frame_count);
  if (!allocated) {
    return PcmResult{allocated.error()};
  }
  auto output = std::move(allocated).take_pcm();
  if (output_frame_count == 0U) {
    return PcmResult{std::move(output)};
  }

  const auto* source_left = pcm.channel_data(PcmChannel::Left);
  const auto* source_right = pcm.channel_data(PcmChannel::Right);
  auto* output_left = output.mutable_channel_data(PcmChannel::Left);
  auto* output_right = output.mutable_channel_data(PcmChannel::Right);
  const auto source_frame_count = pcm.frame_count();

  for (std::size_t frame = 0U; frame < output_frame_count; ++frame) {
    std::size_t source_frame = 0U;
    bool has_source = false;
    if (signed_shift_frames >= 0) {
      const auto shift = static_cast<std::size_t>(signed_shift_frames);
      if (frame >= shift) {
        source_frame = frame - shift;
        has_source = source_frame < source_frame_count;
      }
    } else {
      const auto skip = static_cast<std::size_t>(-signed_shift_frames);
      if (frame <= std::numeric_limits<std::size_t>::max() - skip) {
        source_frame = frame + skip;
        has_source = source_frame < source_frame_count;
      }
    }
    if (has_source) {
      output_left[frame] = source_left[source_frame];
      output_right[frame] = source_right[source_frame];
    } else {
      output_left[frame] = 0;
      output_right[frame] = 0;
    }
  }
  return PcmResult{std::move(output)};
}

}  // namespace otgy::core
