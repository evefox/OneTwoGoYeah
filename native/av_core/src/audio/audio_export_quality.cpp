// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export_quality.hpp"

#include "vorbis_export_config.hpp"

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cstdint>

namespace otgy::core {
namespace {

class VorbisInfoState final {
 public:
  VorbisInfoState() noexcept { vorbis_info_init(&value); }
  ~VorbisInfoState() { vorbis_info_clear(&value); }
  VorbisInfoState(const VorbisInfoState&) = delete;
  VorbisInfoState& operator=(const VorbisInfoState&) = delete;
  vorbis_info value{};
};

[[nodiscard]] bool valid_codec(AudioSourceCodec codec) noexcept {
  return codec == AudioSourceCodec::Mp3 ||
         codec == AudioSourceCodec::OggVorbis;
}

[[nodiscard]] std::uint64_t source_bitrate(
    AudioSourceInfo source_info) noexcept {
  if (source_info.codec == AudioSourceCodec::OggVorbis &&
      source_info.nominal_bitrate_bps != 0U) {
    return source_info.nominal_bitrate_bps;
  }
  return source_info.measured_bitrate_bps;
}

}  // namespace

AudioExportQualityPolicyResult::AudioExportQualityPolicyResult(
    AudioExportQualityPolicy policy) noexcept
    : storage_(policy) {}

AudioExportQualityPolicyResult::AudioExportQualityPolicyResult(
    AudioExportQualityError error) noexcept
    : storage_(error) {}

AudioExportQualityPolicyResult::operator bool() const noexcept {
  return std::holds_alternative<AudioExportQualityPolicy>(storage_);
}

const AudioExportQualityPolicy*
AudioExportQualityPolicyResult::policy() const noexcept {
  return std::get_if<AudioExportQualityPolicy>(&storage_);
}

AudioExportQualityError AudioExportQualityPolicyResult::error() const noexcept {
  const auto* error = std::get_if<AudioExportQualityError>(&storage_);
  return error == nullptr ? AudioExportQualityError::None : *error;
}

AudioExportQualityPolicyResult calculate_audio_export_quality(
    std::uint32_t sample_rate, AudioSourceInfo source_info) noexcept {
  if (!valid_codec(source_info.codec)) {
    return AudioExportQualityPolicyResult{
        AudioExportQualityError::InvalidSourceInfo};
  }
  if (!detail::valid_vorbis_export_sample_rate(sample_rate)) {
    return AudioExportQualityPolicyResult{
        AudioExportQualityError::UnsupportedSampleRate};
  }

  AudioExportQualityPolicy policy{{}, source_bitrate(source_info), 1U, 1U};
  for (std::uint8_t quality = 1U; quality <= 5U; ++quality) {
    VorbisInfoState info;
    const auto quality_value = detail::vorbis_export_quality_value(quality);
    const auto initialized = vorbis_encode_init_vbr(
        &info.value, 2L, static_cast<long>(sample_rate), quality_value);
    if (initialized != 0) {
      return AudioExportQualityPolicyResult{
          AudioExportQualityError::UnsupportedSampleRate};
    }
    if (info.value.bitrate_nominal <= 0L) {
      return AudioExportQualityPolicyResult{
          AudioExportQualityError::EncoderInitializationFailed};
    }
    policy.nominal_bitrates_bps[quality - 1U] =
        static_cast<std::uint64_t>(info.value.bitrate_nominal);
  }

  if (policy.source_bitrate_bps == 0U) {
    policy.quality_ceiling = 5U;
  } else {
    for (std::uint8_t quality = 1U; quality <= 5U; ++quality) {
      if (policy.nominal_bitrates_bps[quality - 1U] <=
          policy.source_bitrate_bps) {
        policy.quality_ceiling = quality;
      }
    }
  }
  policy.default_quality = std::min<std::uint8_t>(3U,
                                                  policy.quality_ceiling);
  return AudioExportQualityPolicyResult{policy};
}

AudioExportQualityError validate_audio_export_quality(
    const AudioExportQualityPolicy& policy, std::uint8_t quality) noexcept {
  if (quality < 1U || quality > 5U) {
    return AudioExportQualityError::InvalidQuality;
  }
  if (policy.quality_ceiling < 1U || policy.quality_ceiling > 5U ||
      policy.default_quality < 1U ||
      policy.default_quality > policy.quality_ceiling) {
    return AudioExportQualityError::InvalidSourceInfo;
  }
  return quality > policy.quality_ceiling
             ? AudioExportQualityError::ExceedsSourceQuality
             : AudioExportQualityError::None;
}

}  // namespace otgy::core
