// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_end.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

using otgy::core::AudioEndError;
using otgy::core::AudioEndMode;
using otgy::core::AudioEndPlan;
using otgy::core::AudioEndSpec;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;
using otgy::core::PcmError;

[[nodiscard]] Pcm16 make_pcm(std::uint32_t sample_rate,
                             std::size_t frame_count) {
  auto result = Pcm16::allocate(sample_rate, frame_count);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  for (std::size_t frame = 0U; frame < frame_count; ++frame) {
    pcm.mutable_channel_data(PcmChannel::Left)[frame] =
        static_cast<Pcm16::Sample>(frame + 1U);
    pcm.mutable_channel_data(PcmChannel::Right)[frame] =
        static_cast<Pcm16::Sample>(-static_cast<std::int64_t>(frame + 1U));
  }
  return pcm;
}

[[nodiscard]] AudioEndPlan plan_for(std::uint32_t sample_rate,
                                    std::size_t source_frames,
                                    std::int64_t shift,
                                    std::size_t after_offset,
                                    double bpm,
                                    AudioEndMode mode,
                                    std::uint64_t row) {
  const auto result = otgy::core::calculate_audio_end_plan(
      sample_rate, source_frames, shift, after_offset, bpm,
      AudioEndSpec{mode, row});
  assert(result);
  return *result.plan();
}

void test_source_end_is_identity() {
  const auto plan = plan_for(1'000U, 50'000U, 0, 50'000U, 120.0,
                             AudioEndMode::SourceEnd, 0U);
  assert(plan.output_frame_count == 50'000U);

  const auto shifted = plan_for(1'000U, 50'000U, 100, 50'100U, 120.0,
                                AudioEndMode::SourceEnd, 0U);
  assert(shifted.output_frame_count == 50'100U);
}

void test_custom_end_trims_and_pads() {
  // row 384 is 96 beats; at 120 BPM that is 48 seconds.
  const auto trim = plan_for(1'000U, 50'000U, 0, 50'000U, 120.0,
                             AudioEndMode::Beat, 384U);
  assert(trim.output_frame_count == 48'000U);

  const auto pad = plan_for(1'000U, 50'000U, 0, 50'000U, 120.0,
                            AudioEndMode::Beat, 384U + 64U);
  assert(pad.output_frame_count == 56'000U);
}

void test_half_frame_rounds_away_from_zero() {
  // row 4 is one beat at 120 BPM: 0.5 seconds. 1001 Hz gives 500.5 frames.
  const auto plan = plan_for(1'001U, 10'000U, 0, 10'000U, 120.0,
                             AudioEndMode::Beat, 4U);
  assert(plan.output_frame_count == 501U);
}

void test_positive_shift_requires_source_audio() {
  const auto accepted = plan_for(1'000U, 50'000U, 100, 50'100U, 120.0,
                                 AudioEndMode::Beat, 384U);
  assert(accepted.output_frame_count == 48'000U);

  const auto rejected = otgy::core::calculate_audio_end_plan(
      1'000U, 50'000U, 100, 50'100U, 120.0,
      AudioEndSpec{AudioEndMode::Beat, 0U});
  assert(!rejected);
  assert(rejected.error() == AudioEndError::EmptyOutput);
}

void test_invalid_inputs() {
  const auto zero_bpm = otgy::core::calculate_audio_end_plan(
      1'000U, 10'000U, 0, 10'000U, 0.0,
      AudioEndSpec{AudioEndMode::Beat, 4U});
  assert(!zero_bpm);
  assert(zero_bpm.error() == AudioEndError::InvalidBpm);

  const auto nan_bpm = otgy::core::calculate_audio_end_plan(
      1'000U, 10'000U, 0, 10'000U,
      std::numeric_limits<double>::quiet_NaN(),
      AudioEndSpec{AudioEndMode::Beat, 4U});
  assert(!nan_bpm);
  assert(nan_bpm.error() == AudioEndError::InvalidBpm);

  const auto bad_row = otgy::core::calculate_audio_end_plan(
      1'000U, 10'000U, 0, 10'000U, 120.0,
      AudioEndSpec{AudioEndMode::Beat, 5U});
  assert(!bad_row);
  assert(bad_row.error() == AudioEndError::InvalidBeatRow);

  const auto zero_rate = otgy::core::calculate_audio_end_plan(
      0U, 10'000U, 0, 10'000U, 120.0,
      AudioEndSpec{AudioEndMode::SourceEnd, 0U});
  assert(!zero_rate);
  assert(zero_rate.error() == AudioEndError::InvalidSampleRate);

  const auto overflow = otgy::core::calculate_audio_end_plan(
      1'000U, 10'000U, 0, 10'000U, 120.0,
      AudioEndSpec{AudioEndMode::Beat,
                   static_cast<std::uint64_t>(1) << 62U});
  assert(!overflow);
  assert(overflow.error() == AudioEndError::SizeOverflow);
}

void test_export_seconds() {
  const auto zero = otgy::core::calculate_audio_end_export_seconds(0U, 120.0);
  assert(zero);
  assert(*zero.seconds() == 0.0);

  const auto thirty = otgy::core::calculate_audio_end_export_seconds(240U,
                                                                     120.0);
  assert(thirty);
  assert(*thirty.seconds() == 30.0);

  const auto bad_row =
      otgy::core::calculate_audio_end_export_seconds(241U, 120.0);
  assert(!bad_row);
  assert(bad_row.error() == AudioEndError::InvalidBeatRow);
}

void test_window_copies_and_pads() {
  const auto input = make_pcm(1'000U, 4U);

  const auto shifted = otgy::core::apply_audio_end_window(input, 2, 6U);
  assert(shifted);
  const auto& shifted_pcm = *shifted.pcm();
  assert(shifted_pcm.frame_count() == 6U);
  const auto* left = shifted_pcm.channel_data(PcmChannel::Left);
  const auto* right = shifted_pcm.channel_data(PcmChannel::Right);
  assert(left[0] == 0);
  assert(left[1] == 0);
  assert(left[2] == 1);
  assert(left[5] == 4);
  assert(right[0] == 0);
  assert(right[1] == 0);
  assert(right[2] == -1);
  assert(right[5] == -4);

  const auto trimmed = otgy::core::apply_audio_end_window(input, -1, 3U);
  assert(trimmed);
  const auto& trimmed_pcm = *trimmed.pcm();
  assert(trimmed_pcm.frame_count() == 3U);
  const auto* trimmed_left = trimmed_pcm.channel_data(PcmChannel::Left);
  assert(trimmed_left[0] == 2);
  assert(trimmed_left[2] == 4);

  const auto empty = otgy::core::apply_audio_end_window(input, 0, 0U);
  assert(empty);
  assert(empty.pcm()->frame_count() == 0U);
}

}  // namespace

int main() {
  test_source_end_is_identity();
  test_custom_end_trims_and_pads();
  test_half_frame_rounds_away_from_zero();
  test_positive_shift_requires_source_audio();
  test_invalid_inputs();
  test_export_seconds();
  test_window_copies_and_pads();
  return 0;
}
