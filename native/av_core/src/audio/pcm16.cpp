// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] bool multiplication_fits(std::size_t lhs, std::size_t rhs,
                                       std::size_t* result) noexcept {
  if (rhs != 0U && lhs > std::numeric_limits<std::size_t>::max() / rhs) {
    return false;
  }
  *result = lhs * rhs;
  return true;
}

}  // namespace

PcmError validate_pcm_dimensions(std::uint32_t sample_rate,
                                 std::size_t frame_count) noexcept {
  if (sample_rate == 0U) {
    return PcmError::InvalidSampleRate;
  }

  // A vector stores one channel at a time, but validating the aggregate size
  // prevents a pair of individually representable allocations from exceeding
  // the addressable byte range when treated as one canonical PCM value.
  std::size_t channel_bytes = 0U;
  if (!multiplication_fits(frame_count, Pcm16::kBytesPerSample,
                           &channel_bytes)) {
    return PcmError::SizeOverflow;
  }

  std::size_t total_bytes = 0U;
  if (!multiplication_fits(channel_bytes, Pcm16::kChannelCount,
                           &total_bytes)) {
    return PcmError::SizeOverflow;
  }

  // Keep the check in terms of the implementation's vector limit too.  This
  // turns a potentially throwing length_error into the same stable size error
  // on every supported standard library.
  const auto channel_max = std::vector<Pcm16::Sample>{}.max_size();
  if (frame_count > channel_max) {
    return PcmError::SizeOverflow;
  }

  (void)total_bytes;
  return PcmError::None;
}

Pcm16::Pcm16(std::uint32_t sample_rate, std::size_t frame_count,
             std::vector<Sample>&& left, std::vector<Sample>&& right,
             std::size_t channel_byte_size,
             std::size_t total_byte_size) noexcept
    : sample_rate_(sample_rate),
      frame_count_(frame_count),
      channel_byte_size_(channel_byte_size),
      total_byte_size_(total_byte_size),
      left_(std::move(left)),
      right_(std::move(right)) {}

PcmResult Pcm16::allocate(std::uint32_t sample_rate,
                          std::size_t frame_count) noexcept {
  const auto dimensions = validate_pcm_dimensions(sample_rate, frame_count);
  if (dimensions != PcmError::None) {
    return PcmResult{dimensions};
  }

  std::size_t channel_bytes = 0U;
  std::size_t total_bytes = 0U;
  // validate_pcm_dimensions already checked both operations.  Keeping the
  // calculations local makes the stored size values independent of any later
  // vector implementation details.
  (void)multiplication_fits(frame_count, kBytesPerSample, &channel_bytes);
  (void)multiplication_fits(channel_bytes, kChannelCount, &total_bytes);

  try {
    std::vector<Sample> left(frame_count, Sample{0});
    std::vector<Sample> right(frame_count, Sample{0});
    return PcmResult{Pcm16{sample_rate, frame_count, std::move(left),
                           std::move(right), channel_bytes, total_bytes}};
  } catch (const std::length_error&) {
    return PcmResult{PcmError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return PcmResult{PcmError::AllocationFailure};
  }
}

PcmResult Pcm16::copy_from_channels(std::uint32_t sample_rate,
                                    const Sample* left,
                                    std::size_t left_frames,
                                    const Sample* right,
                                    std::size_t right_frames) noexcept {
  if (left_frames != right_frames) {
    return PcmResult{PcmError::ChannelFrameCountMismatch};
  }

  const auto dimensions = validate_pcm_dimensions(sample_rate, left_frames);
  if (dimensions != PcmError::None) {
    return PcmResult{dimensions};
  }

  if ((left_frames != 0U && left == nullptr) ||
      (right_frames != 0U && right == nullptr)) {
    return PcmResult{PcmError::NullChannelData};
  }

  std::size_t channel_bytes = 0U;
  std::size_t total_bytes = 0U;
  (void)multiplication_fits(left_frames, kBytesPerSample, &channel_bytes);
  (void)multiplication_fits(channel_bytes, kChannelCount, &total_bytes);

  try {
    std::vector<Sample> left_copy(left_frames);
    std::vector<Sample> right_copy(right_frames);
    if (left_frames != 0U) {
      std::copy_n(left, left_frames, left_copy.begin());
      std::copy_n(right, right_frames, right_copy.begin());
    }
    return PcmResult{Pcm16{sample_rate, left_frames, std::move(left_copy),
                           std::move(right_copy), channel_bytes, total_bytes}};
  } catch (const std::length_error&) {
    return PcmResult{PcmError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return PcmResult{PcmError::AllocationFailure};
  }
}

const Pcm16::Sample* Pcm16::channel_data(PcmChannel channel) const noexcept {
  switch (channel) {
    case PcmChannel::Left:
      return frame_count_ == 0U ? nullptr : left_.data();
    case PcmChannel::Right:
      return frame_count_ == 0U ? nullptr : right_.data();
  }
  return nullptr;
}

Pcm16::Sample* Pcm16::mutable_channel_data(PcmChannel channel) noexcept {
  switch (channel) {
    case PcmChannel::Left:
      return frame_count_ == 0U ? nullptr : left_.data();
    case PcmChannel::Right:
      return frame_count_ == 0U ? nullptr : right_.data();
  }
  return nullptr;
}

PcmResult::PcmResult(Pcm16 pcm) noexcept : storage_(std::move(pcm)) {}

PcmResult::PcmResult(PcmError error) noexcept : storage_(error) {}

bool PcmResult::has_value() const noexcept {
  return std::holds_alternative<Pcm16>(storage_);
}

const Pcm16* PcmResult::pcm() const noexcept {
  return std::get_if<Pcm16>(&storage_);
}

Pcm16* PcmResult::pcm() noexcept {
  return std::get_if<Pcm16>(&storage_);
}

PcmError PcmResult::error() const noexcept {
  if (const auto* error = std::get_if<PcmError>(&storage_)) {
    return *error;
  }
  return PcmError::None;
}

Pcm16 PcmResult::take_pcm() && {
  return std::move(std::get<Pcm16>(storage_));
}

}  // namespace otgy::core
