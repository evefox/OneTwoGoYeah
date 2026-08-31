// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm_offset.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] PcmOffsetError map_pcm_error(PcmError error) noexcept {
  switch (error) {
    case PcmError::AllocationFailure:
      return PcmOffsetError::AllocationFailure;
    case PcmError::SizeOverflow:
      return PcmOffsetError::SizeOverflow;
    case PcmError::InvalidSampleRate:
      return PcmOffsetError::InvalidSampleRate;
    case PcmError::None:
    case PcmError::ChannelFrameCountMismatch:
    case PcmError::NullChannelData:
      return PcmOffsetError::SizeOverflow;
  }
  return PcmOffsetError::SizeOverflow;
}

}  // namespace

PcmOffsetPlanResult::PcmOffsetPlanResult(PcmOffsetPlan plan) noexcept
    : storage_(plan) {}

PcmOffsetPlanResult::PcmOffsetPlanResult(PcmOffsetError error) noexcept
    : storage_(error) {}

PcmOffsetPlanResult::operator bool() const noexcept {
  return std::holds_alternative<PcmOffsetPlan>(storage_);
}

const PcmOffsetPlan* PcmOffsetPlanResult::plan() const noexcept {
  return std::get_if<PcmOffsetPlan>(&storage_);
}

PcmOffsetError PcmOffsetPlanResult::error() const noexcept {
  const auto* error = std::get_if<PcmOffsetError>(&storage_);
  return error == nullptr ? PcmOffsetError::None : *error;
}

PcmOffsetResult::PcmOffsetResult(Pcm16 pcm) noexcept
    : storage_(std::move(pcm)) {}

PcmOffsetResult::PcmOffsetResult(PcmOffsetError error) noexcept
    : storage_(error) {}

PcmOffsetResult::operator bool() const noexcept {
  return std::holds_alternative<Pcm16>(storage_);
}

const Pcm16* PcmOffsetResult::pcm() const noexcept {
  return std::get_if<Pcm16>(&storage_);
}

PcmOffsetError PcmOffsetResult::error() const noexcept {
  const auto* error = std::get_if<PcmOffsetError>(&storage_);
  return error == nullptr ? PcmOffsetError::None : *error;
}

Pcm16 PcmOffsetResult::take_pcm() && {
  return std::move(std::get<Pcm16>(storage_));
}

PcmOffsetPlanResult calculate_pcm_offset_plan(
    std::uint32_t sample_rate, std::size_t frame_count,
    double offset_seconds) noexcept {
  if (sample_rate == 0U) {
    return PcmOffsetPlanResult{PcmOffsetError::InvalidSampleRate};
  }
  if (!std::isfinite(offset_seconds) || offset_seconds < -30.0 ||
      offset_seconds > 30.0) {
    return PcmOffsetPlanResult{PcmOffsetError::InvalidOffset};
  }

  const auto rounded_shift =
      std::round(offset_seconds * static_cast<double>(sample_rate));
  const auto signed_shift = static_cast<std::int64_t>(rounded_shift);
  std::size_t output_frames = frame_count;

  if (signed_shift > 0) {
    const auto added = static_cast<std::uint64_t>(signed_shift);
    if (added > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max()) ||
        static_cast<std::size_t>(added) >
            std::numeric_limits<std::size_t>::max() - frame_count) {
      return PcmOffsetPlanResult{PcmOffsetError::SizeOverflow};
    }
    output_frames += static_cast<std::size_t>(added);
  } else if (signed_shift < 0) {
    const auto removed = static_cast<std::uint64_t>(-signed_shift);
    if (removed >= static_cast<std::uint64_t>(frame_count)) {
      return PcmOffsetPlanResult{PcmOffsetError::RemovesAllFrames};
    }
    output_frames -= static_cast<std::size_t>(removed);
  }

  const auto dimensions = validate_pcm_dimensions(sample_rate, output_frames);
  if (dimensions != PcmError::None) {
    return PcmOffsetPlanResult{map_pcm_error(dimensions)};
  }
  return PcmOffsetPlanResult{PcmOffsetPlan{signed_shift, output_frames}};
}

PcmOffsetResult apply_pcm_offset(const Pcm16& pcm,
                                 double offset_seconds) noexcept {
  const auto plan = calculate_pcm_offset_plan(
      pcm.sample_rate(), pcm.frame_count(), offset_seconds);
  if (!plan) {
    return PcmOffsetResult{plan.error()};
  }

  auto output_result =
      Pcm16::allocate(pcm.sample_rate(), plan.plan()->output_frame_count);
  if (!output_result) {
    return PcmOffsetResult{map_pcm_error(output_result.error())};
  }
  auto output = std::move(output_result).take_pcm();

  const auto shift = plan.plan()->signed_shift_frames;
  const auto input_frames = pcm.frame_count();
  if (shift >= 0) {
    const auto destination_offset = static_cast<std::size_t>(shift);
    if (input_frames != 0U) {
      std::copy_n(pcm.channel_data(PcmChannel::Left), input_frames,
                  output.mutable_channel_data(PcmChannel::Left) +
                      destination_offset);
      std::copy_n(pcm.channel_data(PcmChannel::Right), input_frames,
                  output.mutable_channel_data(PcmChannel::Right) +
                      destination_offset);
    }
  } else {
    const auto source_offset = static_cast<std::size_t>(-shift);
    const auto copied_frames = input_frames - source_offset;
    std::copy_n(pcm.channel_data(PcmChannel::Left) + source_offset,
                copied_frames,
                output.mutable_channel_data(PcmChannel::Left));
    std::copy_n(pcm.channel_data(PcmChannel::Right) + source_offset,
                copied_frames,
                output.mutable_channel_data(PcmChannel::Right));
  }
  return PcmOffsetResult{std::move(output)};
}

}  // namespace otgy::core
