// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class PcmOffsetError : std::uint8_t {
  None = 0,
  InvalidSampleRate = 1,
  InvalidOffset = 2,
  RemovesAllFrames = 3,
  SizeOverflow = 4,
  AllocationFailure = 5,
};

[[nodiscard]] constexpr std::string_view pcm_offset_error_message(
    PcmOffsetError error) noexcept {
  switch (error) {
    case PcmOffsetError::None:
      return {};
    case PcmOffsetError::InvalidSampleRate:
      return "sample rate must be greater than zero";
    case PcmOffsetError::InvalidOffset:
      return "offset must be finite and between -30 and 30 seconds";
    case PcmOffsetError::RemovesAllFrames:
      return "negative offset removes all PCM frames";
    case PcmOffsetError::SizeOverflow:
      return "offset PCM dimensions exceed the representable size";
    case PcmOffsetError::AllocationFailure:
      return "unable to allocate offset PCM samples";
  }
  return "unknown PCM offset error";
}

struct PcmOffsetPlan final {
  std::int64_t signed_shift_frames;
  std::size_t output_frame_count;
};

class PcmOffsetPlanResult final {
 public:
  explicit PcmOffsetPlanResult(PcmOffsetPlan plan) noexcept;
  explicit PcmOffsetPlanResult(PcmOffsetError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const PcmOffsetPlan* plan() const noexcept;
  [[nodiscard]] PcmOffsetError error() const noexcept;

 private:
  std::variant<PcmOffsetPlan, PcmOffsetError> storage_;
};

class PcmOffsetResult final {
 public:
  explicit PcmOffsetResult(Pcm16 pcm) noexcept;
  explicit PcmOffsetResult(PcmOffsetError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const Pcm16* pcm() const noexcept;
  [[nodiscard]] PcmOffsetError error() const noexcept;
  [[nodiscard]] Pcm16 take_pcm() &&;

 private:
  std::variant<Pcm16, PcmOffsetError> storage_;
};

/// Projects an App Offset to the nearest whole PCM frame.
///
/// Exact half-frame values round away from zero. A positive shift prepends
/// silence, while a negative shift removes leading frames. Removing every
/// input frame is rejected before allocation.
[[nodiscard]] PcmOffsetPlanResult calculate_pcm_offset_plan(
    std::uint32_t sample_rate, std::size_t frame_count,
    double offset_seconds) noexcept;

/// Applies the deterministic offset projection without modifying the input.
[[nodiscard]] PcmOffsetResult apply_pcm_offset(
    const Pcm16& pcm, double offset_seconds) noexcept;

}  // namespace otgy::core
