// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_pcm.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

using otgy::core::Pcm16;
using otgy::core::PlaybackPcmError;

Pcm16 fixture() {
  constexpr std::array<Pcm16::Sample, 4U> kLeft{{
      static_cast<Pcm16::Sample>(0x1234),
      static_cast<Pcm16::Sample>(-2),
      static_cast<Pcm16::Sample>(0x5678),
      static_cast<Pcm16::Sample>(-32768),
  }};
  constexpr std::array<Pcm16::Sample, 4U> kRight{{
      static_cast<Pcm16::Sample>(0x4321),
      static_cast<Pcm16::Sample>(3),
      static_cast<Pcm16::Sample>(0x7fff),
      static_cast<Pcm16::Sample>(-1),
  }};
  auto result = Pcm16::copy_from_channels(4U, kLeft.data(), kLeft.size(),
                                          kRight.data(), kRight.size());
  assert(result);
  return std::move(result).take_pcm();
}

void test_interleaves_pcm16le() {
  const auto pcm = fixture();
  const auto result = otgy::core::render_playback_pcm(pcm, 0.0);
  assert(result);
  assert(result.pcm()->sample_rate() == 4U);
  assert(result.pcm()->frame_count() == 4U);
  constexpr std::array<std::uint8_t, 16U> kExpected{{
      0x34, 0x12, 0x21, 0x43, 0xfe, 0xff, 0x03, 0x00,
      0x78, 0x56, 0xff, 0x7f, 0x00, 0x80, 0xff, 0xff,
  }};
  assert(result.pcm()->bytes().size() == kExpected.size());
  assert(std::equal(result.pcm()->bytes().begin(),
                    result.pcm()->bytes().end(), kExpected.begin()));
}

void test_start_projection_and_half_frame_rounding() {
  const auto pcm = fixture();
  const auto skipped = otgy::core::render_playback_pcm(pcm, 0.375);
  assert(skipped);
  assert(skipped.pcm()->frame_count() == 2U);
  assert(skipped.pcm()->bytes()[0] == 0x78U);

  const auto lead_in = otgy::core::render_playback_pcm(pcm, -0.375);
  assert(lead_in);
  assert(lead_in.pcm()->frame_count() == 6U);
  for (std::size_t index = 0U; index < 8U; ++index) {
    assert(lead_in.pcm()->bytes()[index] == 0U);
  }
  assert(lead_in.pcm()->bytes()[8] == 0x34U);

  const auto near_end = otgy::core::render_playback_pcm(pcm, 0.999);
  assert(near_end);
  assert(near_end.pcm()->frame_count() == 0U);
  assert(near_end.pcm()->bytes().empty());
}

void test_input_and_errors() {
  const auto pcm = fixture();
  for (const double invalid : {
           std::numeric_limits<double>::quiet_NaN(),
           std::numeric_limits<double>::infinity(), 1.001,
       }) {
    const auto result = otgy::core::render_playback_pcm(pcm, invalid);
    assert(!result);
    assert(result.error() == PlaybackPcmError::InvalidTimelineStart);
  }
  const auto minimum_frame =
      otgy::core::render_playback_pcm(pcm, -std::ldexp(1.0, 61));
  assert(!minimum_frame);
  assert(minimum_frame.error() == PlaybackPcmError::SizeOverflow);
  assert(!otgy::core::playback_pcm_error_message(
              PlaybackPcmError::InvalidTimelineStart)
              .empty());
  assert(!otgy::core::playback_pcm_error_message(
              static_cast<PlaybackPcmError>(255))
              .empty());
}

}  // namespace

int main() {
  test_interleaves_pcm16le();
  test_start_projection_and_half_frame_rounding();
  test_input_and_errors();
  return 0;
}
