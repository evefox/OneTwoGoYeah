// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm_resample.hpp"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using otgy::core::Pcm16;
using otgy::core::PcmChannel;
using otgy::core::PcmError;
using otgy::core::resample_pcm;

Pcm16 make_pcm(std::uint32_t sample_rate, const std::vector<std::int16_t>& left,
               const std::vector<std::int16_t>& right) {
  auto result = Pcm16::copy_from_channels(sample_rate, left.data(), left.size(),
                                          right.data(), right.size());
  assert(result);
  return std::move(result).take_pcm();
}

void test_double_rate_linear_interpolation() {
  const std::vector<std::int16_t> ramp{0, 1000, 2000, 3000};
  const auto source = make_pcm(8000U, ramp, ramp);
  const auto result = resample_pcm(source, 16000U);
  assert(result.error == PcmError::None);
  assert(result.pcm);
  const auto& output = *result.pcm;
  assert(output.sample_rate() == 16000U);
  assert(output.frame_count() == 8U);

  const auto* left = output.channel_data(PcmChannel::Left);
  const auto* right = output.channel_data(PcmChannel::Right);
  const std::int16_t expected[] = {0, 500, 1000, 1500, 2000, 2500, 3000, 3000};
  for (std::size_t index = 0U; index < 8U; ++index) {
    assert(left[index] == expected[index]);
    assert(right[index] == expected[index]);
  }
}

void test_frame_count_preserves_duration() {
  const std::vector<std::int16_t> samples(44100, 1234);
  const auto source = make_pcm(44100U, samples, samples);
  const auto result = resample_pcm(source, 48000U);
  assert(result.error == PcmError::None);
  assert(result.pcm);
  assert(result.pcm->sample_rate() == 48000U);
  assert(result.pcm->frame_count() == 48000U);

  const auto reversed = resample_pcm(*result.pcm, 44100U);
  assert(reversed.error == PcmError::None);
  assert(reversed.pcm);
  assert(reversed.pcm->frame_count() == 44100U);
}

void test_rejects_invalid_rate_and_copies_equal_rate() {
  const std::vector<std::int16_t> samples{1, 2, 3};
  const auto source = make_pcm(8000U, samples, samples);

  const auto invalid = resample_pcm(source, 0U);
  assert(invalid.error == PcmError::InvalidSampleRate);
  assert(!invalid.pcm);

  const auto copy = resample_pcm(source, 8000U);
  assert(copy.error == PcmError::None);
  assert(copy.pcm);
  assert(copy.pcm->frame_count() == source.frame_count());
  assert(copy.pcm->channel_data(PcmChannel::Left)[1] == 2);
}

void test_empty_pcm() {
  const auto source = make_pcm(44100U, {}, {});
  const auto result = resample_pcm(source, 48000U);
  assert(result.error == PcmError::None);
  assert(result.pcm);
  assert(result.pcm->frame_count() == 0U);
}

}  // namespace

int main() {
  test_double_rate_linear_interpolation();
  test_frame_count_preserves_duration();
  test_rejects_invalid_rate_and_copies_equal_rate();
  test_empty_pcm();
  return 0;
}
