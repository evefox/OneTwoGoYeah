// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm_offset.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

using otgy::core::Pcm16;
using otgy::core::PcmChannel;
using otgy::core::PcmOffsetError;

[[nodiscard]] Pcm16 make_pcm() {
  const std::array<Pcm16::Sample, 4> left{{1, 2, 3, 4}};
  const std::array<Pcm16::Sample, 4> right{{-1, -2, -3, -4}};
  auto result = Pcm16::copy_from_channels(
      4U, left.data(), left.size(), right.data(), right.size());
  assert(result);
  return std::move(result).take_pcm();
}

void assert_sample(const Pcm16& pcm, PcmChannel channel, std::size_t frame,
                   Pcm16::Sample expected) {
  assert(pcm.channel_data(channel)[frame] == expected);
}

void test_positive_and_zero_offset() {
  const auto input = make_pcm();
  auto positive = otgy::core::apply_pcm_offset(input, 0.25);
  assert(positive);
  assert(positive.pcm()->sample_rate() == 4U);
  assert(positive.pcm()->frame_count() == 5U);
  assert_sample(*positive.pcm(), PcmChannel::Left, 0U, 0);
  assert_sample(*positive.pcm(), PcmChannel::Right, 0U, 0);
  for (std::size_t frame = 0U; frame < input.frame_count(); ++frame) {
    assert_sample(*positive.pcm(), PcmChannel::Left, frame + 1U,
                  input.channel_data(PcmChannel::Left)[frame]);
    assert_sample(*positive.pcm(), PcmChannel::Right, frame + 1U,
                  input.channel_data(PcmChannel::Right)[frame]);
  }

  auto zero = otgy::core::apply_pcm_offset(input, -0.0);
  assert(zero);
  assert(zero.pcm()->frame_count() == input.frame_count());
  assert(zero.pcm()->channel_data(PcmChannel::Left) !=
         input.channel_data(PcmChannel::Left));
  for (std::size_t frame = 0U; frame < input.frame_count(); ++frame) {
    assert_sample(*zero.pcm(), PcmChannel::Left, frame,
                  input.channel_data(PcmChannel::Left)[frame]);
    assert_sample(*zero.pcm(), PcmChannel::Right, frame,
                  input.channel_data(PcmChannel::Right)[frame]);
  }

  zero = otgy::core::apply_pcm_offset(input, 0.124);
  assert(zero);
  assert(zero.pcm()->frame_count() == input.frame_count());
}

void test_negative_offset() {
  const auto input = make_pcm();
  auto result = otgy::core::apply_pcm_offset(input, -0.25);
  assert(result);
  assert(result.pcm()->frame_count() == 3U);
  assert_sample(*result.pcm(), PcmChannel::Left, 0U, 2);
  assert_sample(*result.pcm(), PcmChannel::Left, 2U, 4);
  assert_sample(*result.pcm(), PcmChannel::Right, 0U, -2);
  assert_sample(*result.pcm(), PcmChannel::Right, 2U, -4);

  assert_sample(input, PcmChannel::Left, 0U, 1);
  assert_sample(input, PcmChannel::Right, 0U, -1);
}

void test_half_away_from_zero() {
  const auto positive =
      otgy::core::calculate_pcm_offset_plan(4U, 4U, 0.125);
  const auto negative =
      otgy::core::calculate_pcm_offset_plan(4U, 4U, -0.125);
  const auto below_positive =
      otgy::core::calculate_pcm_offset_plan(4U, 4U, 0.124);
  const auto below_negative =
      otgy::core::calculate_pcm_offset_plan(4U, 4U, -0.124);
  assert(positive && positive.plan()->signed_shift_frames == 1);
  assert(positive.plan()->output_frame_count == 5U);
  assert(negative && negative.plan()->signed_shift_frames == -1);
  assert(negative.plan()->output_frame_count == 3U);
  assert(below_positive && below_positive.plan()->signed_shift_frames == 0);
  assert(below_negative && below_negative.plan()->signed_shift_frames == 0);
}

void test_errors_and_boundaries() {
  assert(otgy::core::calculate_pcm_offset_plan(0U, 4U, 0.0).error() ==
         PcmOffsetError::InvalidSampleRate);
  assert(otgy::core::calculate_pcm_offset_plan(
             4U, 4U, std::numeric_limits<double>::infinity())
             .error() == PcmOffsetError::InvalidOffset);
  assert(otgy::core::calculate_pcm_offset_plan(4U, 4U, -30.001).error() ==
         PcmOffsetError::InvalidOffset);
  assert(otgy::core::calculate_pcm_offset_plan(4U, 4U, 30.001).error() ==
         PcmOffsetError::InvalidOffset);

  assert(otgy::core::calculate_pcm_offset_plan(4U, 4U, -1.0).error() ==
         PcmOffsetError::RemovesAllFrames);
  assert(otgy::core::calculate_pcm_offset_plan(4U, 4U, -2.0).error() ==
         PcmOffsetError::RemovesAllFrames);
  assert(otgy::core::calculate_pcm_offset_plan(
             1U, std::numeric_limits<std::size_t>::max(), 1.0)
             .error() == PcmOffsetError::SizeOverflow);

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(PcmOffsetError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::pcm_offset_error_message(
        static_cast<PcmOffsetError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::pcm_offset_error_message(
              static_cast<PcmOffsetError>(255))
              .empty());
}

void test_zero_frame_positive_offset_and_ownership() {
  auto empty = Pcm16::allocate(4U, 0U);
  assert(empty);
  auto result = otgy::core::apply_pcm_offset(*empty.pcm(), 0.5);
  assert(result);
  assert(result.pcm()->frame_count() == 2U);
  assert_sample(*result.pcm(), PcmChannel::Left, 1U, 0);
  assert_sample(*result.pcm(), PcmChannel::Right, 1U, 0);

  const auto copy = *result.pcm();
  auto moved = std::move(result).take_pcm();
  moved.mutable_channel_data(PcmChannel::Left)[0] = 9;
  assert(copy.channel_data(PcmChannel::Left)[0] == 0);
}

}  // namespace

int main() {
  test_positive_and_zero_offset();
  test_negative_offset();
  test_half_away_from_zero();
  test_errors_and_boundaries();
  test_zero_frame_positive_offset_and_ownership();
  return 0;
}
