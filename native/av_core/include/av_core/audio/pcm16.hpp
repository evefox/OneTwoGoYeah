// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

/// The canonical channel order used by av_core's planar PCM representation.
enum class PcmChannel : std::uint8_t {
  Left = 0,
  Right = 1,
};

/// Stable failures returned while constructing canonical PCM.
enum class PcmError : std::uint8_t {
  None = 0,
  InvalidSampleRate = 1,
  ChannelFrameCountMismatch = 2,
  NullChannelData = 3,
  SizeOverflow = 4,
  AllocationFailure = 5,
};

[[nodiscard]] constexpr std::string_view pcm_error_message(
    PcmError error) noexcept {
  switch (error) {
    case PcmError::None:
      return {};
    case PcmError::InvalidSampleRate:
      return "sample rate must be greater than zero";
    case PcmError::ChannelFrameCountMismatch:
      return "left and right channels must contain the same number of frames";
    case PcmError::NullChannelData:
      return "channel data must not be null when its frame count is non-zero";
    case PcmError::SizeOverflow:
      return "PCM dimensions exceed the representable size";
    case PcmError::AllocationFailure:
      return "unable to allocate PCM samples";
  }
  return "unknown PCM error";
}

/// Validates dimensions before any allocation or input buffer access.
///
/// A canonical PCM value always has two channels of signed 16-bit samples.
/// Zero frames are valid (for example, before a decoder has produced data),
/// but a sample rate of zero is not.  The size checks cover both each channel
/// allocation and the aggregate byte count for the two-channel value.
[[nodiscard]] PcmError validate_pcm_dimensions(std::uint32_t sample_rate,
                                               std::size_t frame_count) noexcept;

class PcmResult;

/// Owning, planar, stereo PCM16 samples.
///
/// The left and right channel buffers are independently owned by this value;
/// no constructor retains a pointer supplied by a caller.  Copying performs a
/// deep copy and moving transfers both buffers without changing their sample
/// values.  The frame count is immutable after construction, while decoders
/// may fill samples through the bounded mutable channel pointers.
class Pcm16 final {
 public:
  using Sample = std::int16_t;

  static constexpr std::size_t kChannelCount = 2U;
  static constexpr std::size_t kBytesPerSample = sizeof(Sample);
  static_assert(kBytesPerSample == 2U,
                "canonical PCM16 samples must occupy two bytes");

  [[nodiscard]] static PcmResult allocate(std::uint32_t sample_rate,
                                           std::size_t frame_count) noexcept;

  /// Copies two caller-owned channel buffers into a new PCM value.
  ///
  /// A null pointer is accepted only when the corresponding frame count is
  /// zero.  The two frame counts must match exactly.
  [[nodiscard]] static PcmResult copy_from_channels(
      std::uint32_t sample_rate, const Sample* left, std::size_t left_frames,
      const Sample* right, std::size_t right_frames) noexcept;

  Pcm16(const Pcm16&) = default;
  Pcm16& operator=(const Pcm16&) = default;
  Pcm16(Pcm16&&) noexcept = default;
  Pcm16& operator=(Pcm16&&) noexcept = default;
  ~Pcm16() = default;

  [[nodiscard]] std::uint32_t sample_rate() const noexcept {
    return sample_rate_;
  }

  [[nodiscard]] std::size_t frame_count() const noexcept {
    return frame_count_;
  }

  [[nodiscard]] static constexpr std::size_t channel_count() noexcept {
    return kChannelCount;
  }

  [[nodiscard]] std::size_t channel_byte_size() const noexcept {
    return channel_byte_size_;
  }

  [[nodiscard]] std::size_t total_byte_size() const noexcept {
    return total_byte_size_;
  }

  [[nodiscard]] const Sample* channel_data(PcmChannel channel) const noexcept;

  /// Returns a pointer to exactly frame_count() writable samples.  The pointer
  /// is null for an empty channel and never permits resizing the owned buffer.
  [[nodiscard]] Sample* mutable_channel_data(PcmChannel channel) noexcept;

 private:
  Pcm16(std::uint32_t sample_rate, std::size_t frame_count,
        std::vector<Sample>&& left, std::vector<Sample>&& right,
        std::size_t channel_byte_size, std::size_t total_byte_size) noexcept;

  std::uint32_t sample_rate_;
  std::size_t frame_count_;
  std::size_t channel_byte_size_;
  std::size_t total_byte_size_;
  std::vector<Sample> left_;
  std::vector<Sample> right_;
};

/// Result type used by all PCM factories.  It never aliases caller buffers;
/// on failure it contains only a stable PcmError and no partially-built PCM.
class PcmResult final {
 public:
  explicit PcmResult(Pcm16 pcm) noexcept;
  explicit PcmResult(PcmError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const Pcm16* pcm() const noexcept;
  [[nodiscard]] Pcm16* pcm() noexcept;
  [[nodiscard]] PcmError error() const noexcept;

  /// Moves the owned PCM out of a successful result.  Callers must check
  /// has_value() (or operator bool) first.
  [[nodiscard]] Pcm16 take_pcm() &&;

 private:
  std::variant<Pcm16, PcmError> storage_;
};

}  // namespace otgy::core
