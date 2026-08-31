// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export_quality.hpp"

#include <cassert>
#include <cstdint>

namespace {

using otgy::core::AudioExportQualityError;
using otgy::core::AudioSourceCodec;
using otgy::core::AudioSourceInfo;

void assert_nominal_table(const otgy::core::AudioExportQualityPolicy& policy) {
  for (std::size_t index = 0U;
       index < policy.nominal_bitrates_bps.size(); ++index) {
    assert(policy.nominal_bitrates_bps[index] != 0U);
    if (index != 0U) {
      assert(policy.nominal_bitrates_bps[index] >
             policy.nominal_bitrates_bps[index - 1U]);
    }
  }
}

void test_unknown_source_and_defaults() {
  const auto policy = otgy::core::calculate_audio_export_quality(
      48'000U, {AudioSourceCodec::Mp3, 0U, 0U});
  assert(policy);
  assert_nominal_table(*policy.policy());
  assert(policy.policy()->source_bitrate_bps == 0U);
  assert(policy.policy()->quality_ceiling == 5U);
  assert(policy.policy()->default_quality == 3U);
}

void test_mp3_measured_boundaries() {
  const auto baseline = otgy::core::calculate_audio_export_quality(
      48'000U, {AudioSourceCodec::Mp3, 0U, 999'999U});
  assert(baseline);
  const auto& nominal = baseline.policy()->nominal_bitrates_bps;

  for (std::uint8_t quality = 1U; quality <= 5U; ++quality) {
    const auto exact = otgy::core::calculate_audio_export_quality(
        48'000U,
        {AudioSourceCodec::Mp3, nominal[quality - 1U], 1U});
    assert(exact);
    assert(exact.policy()->quality_ceiling == quality);
    assert(exact.policy()->source_bitrate_bps == nominal[quality - 1U]);
  }

  const auto below_three = otgy::core::calculate_audio_export_quality(
      48'000U, {AudioSourceCodec::Mp3, nominal[2U] - 1U, 999'999U});
  assert(below_three);
  assert(below_three.policy()->quality_ceiling == 2U);
  assert(below_three.policy()->default_quality == 2U);
}

void test_ogg_prefers_nominal_then_measured() {
  const auto baseline = otgy::core::calculate_audio_export_quality(
      44'100U, {AudioSourceCodec::OggVorbis, 0U, 0U});
  assert(baseline);
  const auto nominal_two = baseline.policy()->nominal_bitrates_bps[1U];
  const auto nominal_four = baseline.policy()->nominal_bitrates_bps[3U];

  const auto preferred = otgy::core::calculate_audio_export_quality(
      44'100U,
      {AudioSourceCodec::OggVorbis, nominal_four, nominal_two});
  assert(preferred);
  assert(preferred.policy()->source_bitrate_bps == nominal_two);
  assert(preferred.policy()->quality_ceiling == 2U);

  const auto fallback = otgy::core::calculate_audio_export_quality(
      44'100U, {AudioSourceCodec::OggVorbis, nominal_four, 0U});
  assert(fallback);
  assert(fallback.policy()->source_bitrate_bps == nominal_four);
  assert(fallback.policy()->quality_ceiling == 4U);
}

void test_explicit_quality_validation() {
  const auto result = otgy::core::calculate_audio_export_quality(
      48'000U, {AudioSourceCodec::Mp3, 0U, 0U});
  assert(result);
  auto policy = *result.policy();
  policy.quality_ceiling = 3U;
  policy.default_quality = 3U;
  assert(otgy::core::validate_audio_export_quality(policy, 0U) ==
         AudioExportQualityError::InvalidQuality);
  assert(otgy::core::validate_audio_export_quality(policy, 3U) ==
         AudioExportQualityError::None);
  assert(otgy::core::validate_audio_export_quality(policy, 4U) ==
         AudioExportQualityError::ExceedsSourceQuality);
  policy.quality_ceiling = 0U;
  assert(otgy::core::validate_audio_export_quality(policy, 1U) ==
         AudioExportQualityError::InvalidSourceInfo);
}

void test_errors() {
  assert(otgy::core::calculate_audio_export_quality(
             48'000U,
             {static_cast<AudioSourceCodec>(255), 0U, 0U})
             .error() == AudioExportQualityError::InvalidSourceInfo);
  assert(otgy::core::calculate_audio_export_quality(
             0U, {AudioSourceCodec::Mp3, 0U, 0U})
             .error() == AudioExportQualityError::UnsupportedSampleRate);
  assert(otgy::core::calculate_audio_export_quality(
             1U, {AudioSourceCodec::Mp3, 0U, 0U})
             .error() == AudioExportQualityError::UnsupportedSampleRate);

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  AudioExportQualityError::EncoderInitializationFailed);
       ++raw) {
    const auto message = otgy::core::audio_export_quality_error_message(
        static_cast<AudioExportQualityError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::audio_export_quality_error_message(
              static_cast<AudioExportQualityError>(255))
              .empty());
}

}  // namespace

int main() {
  test_unknown_source_and_defaults();
  test_mp3_measured_boundaries();
  test_ogg_prefers_nominal_then_measured();
  test_explicit_quality_validation();
  test_errors();
  return 0;
}
