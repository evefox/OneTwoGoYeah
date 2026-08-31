// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/audio_source_info.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class AudioExportQualityError : std::uint8_t {
  None = 0,
  InvalidSourceInfo = 1,
  UnsupportedSampleRate = 2,
  InvalidQuality = 3,
  ExceedsSourceQuality = 4,
  EncoderInitializationFailed = 5,
};

[[nodiscard]] constexpr std::string_view audio_export_quality_error_message(
    AudioExportQualityError error) noexcept {
  switch (error) {
    case AudioExportQualityError::None:
      return {};
    case AudioExportQualityError::InvalidSourceInfo:
      return "audio source quality information is invalid";
    case AudioExportQualityError::UnsupportedSampleRate:
      return "sample rate is not supported by the pinned Vorbis encoder";
    case AudioExportQualityError::InvalidQuality:
      return "audio export quality must be between 1 and 5";
    case AudioExportQualityError::ExceedsSourceQuality:
      return "audio export quality exceeds the source quality ceiling";
    case AudioExportQualityError::EncoderInitializationFailed:
      return "unable to query the pinned Vorbis quality setup";
  }
  return "unknown audio export quality error";
}

struct AudioExportQualityPolicy final {
  std::array<std::uint64_t, 5> nominal_bitrates_bps;
  std::uint64_t source_bitrate_bps;
  std::uint8_t quality_ceiling;
  std::uint8_t default_quality;
};

class AudioExportQualityPolicyResult final {
 public:
  explicit AudioExportQualityPolicyResult(
      AudioExportQualityPolicy policy) noexcept;
  explicit AudioExportQualityPolicyResult(
      AudioExportQualityError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const AudioExportQualityPolicy* policy() const noexcept;
  [[nodiscard]] AudioExportQualityError error() const noexcept;

 private:
  std::variant<AudioExportQualityPolicy, AudioExportQualityError> storage_;
};

/// Derives the source ceiling from measured/nominal bitrate and the pinned
/// stereo VBR setup at the current sample rate.
[[nodiscard]] AudioExportQualityPolicyResult calculate_audio_export_quality(
    std::uint32_t sample_rate, AudioSourceInfo source_info) noexcept;

/// Validates an explicit user choice without silently reducing it.
[[nodiscard]] AudioExportQualityError validate_audio_export_quality(
    const AudioExportQualityPolicy& policy, std::uint8_t quality) noexcept;

}  // namespace otgy::core
