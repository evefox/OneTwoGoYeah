// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/waveform/waveform_tile.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using otgy::core::Pcm16;
using otgy::core::PcmChannel;
using otgy::core::WaveformError;
using otgy::core::WaveformTile;

[[nodiscard]] std::vector<std::uint8_t> read_fixture(
    const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Pcm16::Sample read_pcm16le(const std::uint8_t* bytes) {
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[0]) |
      (static_cast<std::uint16_t>(bytes[1]) << 8U));
  const auto value =
      bits <= static_cast<std::uint16_t>(
                  std::numeric_limits<Pcm16::Sample>::max())
          ? static_cast<std::int32_t>(bits)
          : static_cast<std::int32_t>(bits) - 65'536;
  return static_cast<Pcm16::Sample>(value);
}

[[nodiscard]] Pcm16 load_stereo_pcm_fixture() {
  const auto bytes = read_fixture(
      std::string{OTGY_MP3_FIXTURE_DIR} +
      "/generated_stereo_11025.planar.pcm16le");
  assert(bytes.size() % (Pcm16::kChannelCount * Pcm16::kBytesPerSample) ==
         0U);
  const auto frames =
      bytes.size() / (Pcm16::kChannelCount * Pcm16::kBytesPerSample);
  std::array<std::vector<Pcm16::Sample>, Pcm16::kChannelCount> channels{
      std::vector<Pcm16::Sample>(frames),
      std::vector<Pcm16::Sample>(frames),
  };
  for (std::size_t channel = 0U; channel < channels.size(); ++channel) {
    for (std::size_t frame = 0U; frame < frames; ++frame) {
      const auto offset = (channel * frames + frame) * 2U;
      channels[channel][frame] = read_pcm16le(bytes.data() + offset);
    }
  }

  auto result = Pcm16::copy_from_channels(
      11'025U, channels[0].data(), channels[0].size(), channels[1].data(),
      channels[1].size());
  assert(result);
  return std::move(result).take_pcm();
}

[[nodiscard]] bool channel_is_zero(const WaveformTile& tile,
                                   PcmChannel channel) {
  const auto* begin = tile.channel_alpha(channel);
  return std::all_of(begin, begin + tile.channel_byte_size(),
                     [](std::uint8_t value) { return value == 0U; });
}

void assert_matches_golden(const WaveformTile& tile,
                           const std::string& fixture_name) {
  const auto expected = read_fixture(
      std::string{OTGY_WAVEFORM_FIXTURE_DIR} + "/" + fixture_name);
  assert(expected.size() == tile.total_byte_size());
  const auto channel_size = tile.channel_byte_size();
  assert(std::equal(tile.channel_alpha(PcmChannel::Left),
                    tile.channel_alpha(PcmChannel::Left) + channel_size,
                    expected.begin()));
  assert(std::equal(tile.channel_alpha(PcmChannel::Right),
                    tile.channel_alpha(PcmChannel::Right) + channel_size,
                    expected.begin() +
                        static_cast<std::ptrdiff_t>(channel_size)));
}

void assert_binary_alpha(const WaveformTile& tile) {
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* begin = tile.channel_alpha(channel);
    assert(std::all_of(begin, begin + tile.channel_byte_size(),
                       [](std::uint8_t value) {
                         return value == 0U || value == 255U;
                       }));
  }
}

void assert_uniform_2x_alpha(const WaveformTile& tile) {
  bool found_63 = false;
  bool found_127 = false;
  bool found_191 = false;
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* begin = tile.channel_alpha(channel);
    for (const auto* value = begin;
         value != begin + tile.channel_byte_size(); ++value) {
      assert(*value == 0U || *value == 63U || *value == 127U ||
             *value == 191U || *value == 255U);
      found_63 = found_63 || *value == 63U;
      found_127 = found_127 || *value == 127U;
      found_191 = found_191 || *value == 191U;
    }
  }
  assert(found_63);
  assert(found_127);
  assert(found_191);
}

void assert_horizontally_symmetric(const WaveformTile& tile) {
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* alpha = tile.channel_alpha(channel);
    for (std::size_t y = 0U; y < WaveformTile::kHeight; ++y) {
      const auto row = y * tile.channel_width();
      for (std::size_t x = 0U; x < tile.channel_width(); ++x) {
        const auto mirror = tile.channel_width() - 1U - x;
        assert(alpha[row + x] == alpha[row + mirror]);
      }
    }
  }
}

void assert_amplitude_luminance(const WaveformTile& tile) {
  bool found_intermediate_alpha = false;
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* alpha = tile.channel_alpha(channel);
    for (std::size_t y = 0U; y < WaveformTile::kHeight; ++y) {
      const auto row = y * tile.channel_width();
      std::uint8_t row_alpha = 0U;
      for (std::size_t x = 0U; x < tile.channel_width(); ++x) {
        const auto value = alpha[row + x];
        assert(value != 255U);
        if (value == 0U) {
          continue;
        }
        found_intermediate_alpha = true;
        if (row_alpha == 0U) {
          row_alpha = value;
        } else {
          assert(value == row_alpha);
        }
      }
    }
  }
  assert(found_intermediate_alpha);
}

void test_empty_pcm_geometry() {
  auto pcm_result =
      Pcm16::copy_from_channels(8'000U, nullptr, 0U, nullptr, 0U);
  assert(pcm_result);
  const auto& pcm = *pcm_result.pcm();

  auto minimum = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 1.0, 1.0, 0U);
  assert(minimum);
  assert(minimum.error() == WaveformError::None);
  assert(minimum.tile()->pixels_per_second() == 9U);
  assert(minimum.tile()->channel_width() == 64U);
  assert(minimum.tile()->channel_spacing() == 6U);
  assert(minimum.tile()->channel_byte_size() == 64U * 128U);
  assert(channel_is_zero(*minimum.tile(), PcmChannel::Left));
  assert(channel_is_zero(*minimum.tile(), PcmChannel::Right));

  auto quarter_step =
      otgy::core::generate_signed_uniform_waveform_tile_1x(
          pcm, 11.25, 2.5, 7U);
  assert(quarter_step);
  assert(quarter_step.tile()->block_index() == 7U);
  assert(quarter_step.tile()->pixels_per_second() == 660U);
  assert(quarter_step.tile()->channel_width() == 160U);
  assert(quarter_step.tile()->channel_spacing() == 15U);

  auto maximum = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 19.0, 10.0, std::numeric_limits<std::uint64_t>::max());
  assert(maximum);
  assert(maximum.tile()->pixels_per_second() == 16'755U);
  assert(maximum.tile()->channel_width() == 256U);
  assert(maximum.tile()->channel_spacing() == 60U);
  assert(channel_is_zero(*maximum.tile(), PcmChannel::Left));
  assert(channel_is_zero(*maximum.tile(), PcmChannel::Right));
  assert(maximum.tile()->channel_alpha(static_cast<PcmChannel>(255)) ==
         nullptr);
}

void test_signed_reference_golden_tile() {
  const auto pcm = load_stereo_pcm_fixture();
  auto result = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 11.0, 4.0, 1U);
  assert(result);
  assert(result.tile()->pixels_per_second() == 594U);
  assert(result.tile()->channel_width() == 256U);
  assert(result.tile()->channel_spacing() == 24U);
  assert(result.tile()->channel_byte_size() == 256U * 128U);

  const auto channel_size = result.tile()->channel_byte_size();
  assert_matches_golden(
      *result.tile(),
      "generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_1x.alpha");
  assert_binary_alpha(*result.tile());
  assert(!std::equal(result.tile()->channel_alpha(PcmChannel::Left),
                     result.tile()->channel_alpha(PcmChannel::Left) +
                         channel_size,
                     result.tile()->channel_alpha(PcmChannel::Right)));

  WaveformTile copy = *result.tile();
  assert(copy.channel_alpha(PcmChannel::Left) !=
         result.tile()->channel_alpha(PcmChannel::Left));
  assert(std::equal(copy.channel_alpha(PcmChannel::Left),
                    copy.channel_alpha(PcmChannel::Left) + channel_size,
                    result.tile()->channel_alpha(PcmChannel::Left)));
  WaveformTile moved = std::move(result).take_tile();
  assert(moved.block_index() == 1U);
}

void test_rectified_reference_golden_tile() {
  const auto pcm = load_stereo_pcm_fixture();
  auto rectified =
      otgy::core::generate_rectified_uniform_waveform_tile_1x(
          pcm, 11.0, 4.0, 1U);
  assert(rectified);
  assert(rectified.tile()->pixels_per_second() == 594U);
  assert(rectified.tile()->channel_width() == 256U);
  assert(rectified.tile()->channel_spacing() == 24U);
  assert(rectified.tile()->channel_byte_size() == 256U * 128U);
  assert_matches_golden(
      *rectified.tile(),
      "generated_stereo_11025.ui_zoom11.scale4.block1.rectified_uniform_1x.alpha");
  assert_binary_alpha(*rectified.tile());

  auto signed_tile = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 11.0, 4.0, 1U);
  assert(signed_tile);
  const auto channel_size = rectified.tile()->channel_byte_size();
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* rectified_alpha = rectified.tile()->channel_alpha(channel);
    assert(!std::equal(rectified_alpha, rectified_alpha + channel_size,
                       signed_tile.tile()->channel_alpha(channel)));
  }
  assert_horizontally_symmetric(*rectified.tile());
}

void test_signed_uniform_2x_reference_golden_tile() {
  const auto pcm = load_stereo_pcm_fixture();
  auto two_x =
      otgy::core::generate_signed_uniform_waveform_tile_2x(
          pcm, 11.0, 4.0, 1U);
  assert(two_x);
  assert(two_x.tile()->pixels_per_second() == 594U);
  assert(two_x.tile()->channel_width() == 256U);
  assert(two_x.tile()->channel_spacing() == 24U);
  assert(two_x.tile()->channel_byte_size() == 256U * 128U);
  assert_matches_golden(
      *two_x.tile(),
      "generated_stereo_11025.ui_zoom11.scale4.block1.signed_uniform_2x.alpha");
  assert_uniform_2x_alpha(*two_x.tile());

  auto one_x = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 11.0, 4.0, 1U);
  assert(one_x);
  const auto channel_size = two_x.tile()->channel_byte_size();
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* two_x_alpha = two_x.tile()->channel_alpha(channel);
    assert(!std::equal(two_x_alpha, two_x_alpha + channel_size,
                       one_x.tile()->channel_alpha(channel)));
  }
}

void test_signed_amplitude_reference_golden_tile() {
  const auto pcm = load_stereo_pcm_fixture();
  auto amplitude =
      otgy::core::generate_signed_amplitude_waveform_tile_1x(
          pcm, 11.0, 4.0, 1U);
  assert(amplitude);
  assert(amplitude.tile()->pixels_per_second() == 594U);
  assert(amplitude.tile()->channel_width() == 256U);
  assert(amplitude.tile()->channel_spacing() == 24U);
  assert(amplitude.tile()->channel_byte_size() == 256U * 128U);
  assert_matches_golden(
      *amplitude.tile(),
      "generated_stereo_11025.ui_zoom11.scale4.block1.signed_amplitude_1x.alpha");

  auto uniform = otgy::core::generate_signed_uniform_waveform_tile_1x(
      pcm, 11.0, 4.0, 1U);
  assert(uniform);
  const auto channel_size = amplitude.tile()->channel_byte_size();
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* amplitude_alpha = amplitude.tile()->channel_alpha(channel);
    assert(!std::equal(amplitude_alpha, amplitude_alpha + channel_size,
                       uniform.tile()->channel_alpha(channel)));
  }
  assert_amplitude_luminance(*amplitude.tile());
}

void test_rectified_amplitude_reference_golden_tile() {
  const auto pcm = load_stereo_pcm_fixture();
  auto amplitude =
      otgy::core::generate_rectified_amplitude_waveform_tile_1x(
          pcm, 11.0, 4.0, 1U);
  assert(amplitude);
  assert(amplitude.tile()->pixels_per_second() == 594U);
  assert(amplitude.tile()->channel_width() == 256U);
  assert(amplitude.tile()->channel_spacing() == 24U);
  assert(amplitude.tile()->channel_byte_size() == 256U * 128U);
  assert_matches_golden(
      *amplitude.tile(),
      "generated_stereo_11025.ui_zoom11.scale4.block1.rectified_amplitude_1x.alpha");
  assert_horizontally_symmetric(*amplitude.tile());
  assert_amplitude_luminance(*amplitude.tile());

  auto uniform =
      otgy::core::generate_rectified_uniform_waveform_tile_1x(
          pcm, 11.0, 4.0, 1U);
  auto signed_amplitude =
      otgy::core::generate_signed_amplitude_waveform_tile_1x(
          pcm, 11.0, 4.0, 1U);
  assert(uniform);
  assert(signed_amplitude);
  const auto channel_size = amplitude.tile()->channel_byte_size();
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* amplitude_alpha = amplitude.tile()->channel_alpha(channel);
    assert(!std::equal(amplitude_alpha, amplitude_alpha + channel_size,
                       uniform.tile()->channel_alpha(channel)));
    assert(!std::equal(amplitude_alpha, amplitude_alpha + channel_size,
                       signed_amplitude.tile()->channel_alpha(channel)));
  }
}

void test_terminal_rounding_is_bounded() {
  const std::array<Pcm16::Sample, 1> left{{
      std::numeric_limits<Pcm16::Sample>::min(),
  }};
  const std::array<Pcm16::Sample, 1> right{{
      std::numeric_limits<Pcm16::Sample>::max(),
  }};
  auto pcm_result = Pcm16::copy_from_channels(
      8'000U, left.data(), left.size(), right.data(), right.size());
  assert(pcm_result);

  auto result = otgy::core::generate_signed_uniform_waveform_tile_1x(
      *pcm_result.pcm(), 19.0, 1.0, 0U);
  assert(result);
  assert(!channel_is_zero(*result.tile(), PcmChannel::Left));
  assert(!channel_is_zero(*result.tile(), PcmChannel::Right));

  auto amplitude =
      otgy::core::generate_signed_amplitude_waveform_tile_1x(
          *pcm_result.pcm(), 19.0, 1.0, 0U);
  assert(amplitude);
  assert(amplitude.tile()->channel_width() == 64U);
  const auto* left_alpha = amplitude.tile()->channel_alpha(PcmChannel::Left);
  assert(left_alpha[0] == 0U);
  assert(left_alpha[1] == 247U);
  assert(left_alpha[31] == 247U);
  assert(left_alpha[32] == 0U);
  const auto* right_alpha =
      amplitude.tile()->channel_alpha(PcmChannel::Right);
  assert(right_alpha[31] == 0U);
  assert(right_alpha[32] == 239U);
  assert(right_alpha[61] == 239U);
  assert(right_alpha[62] == 0U);

  auto past_end = otgy::core::generate_signed_uniform_waveform_tile_1x(
      *pcm_result.pcm(), 19.0, 1.0, 1U);
  assert(past_end);
  assert(channel_is_zero(*past_end.tile(), PcmChannel::Left));
  assert(channel_is_zero(*past_end.tile(), PcmChannel::Right));

  auto two_x =
      otgy::core::generate_signed_uniform_waveform_tile_2x(
          *pcm_result.pcm(), 19.0, 1.0, 0U);
  assert(two_x);
  assert(!channel_is_zero(*two_x.tile(), PcmChannel::Left));
  assert(!channel_is_zero(*two_x.tile(), PcmChannel::Right));
  auto two_x_past_end =
      otgy::core::generate_signed_uniform_waveform_tile_2x(
          *pcm_result.pcm(), 19.0, 1.0, 1U);
  assert(two_x_past_end);
  assert(channel_is_zero(*two_x_past_end.tile(), PcmChannel::Left));
  assert(channel_is_zero(*two_x_past_end.tile(), PcmChannel::Right));
}

void assert_error(double ui_zoom, double scale, WaveformError expected) {
  auto pcm_result =
      Pcm16::copy_from_channels(8'000U, nullptr, 0U, nullptr, 0U);
  assert(pcm_result);
  auto result = otgy::core::generate_signed_uniform_waveform_tile_1x(
      *pcm_result.pcm(), ui_zoom, scale, 0U);
  assert(!result);
  assert(result.tile() == nullptr);
  assert(result.error() == expected);
}

void test_stable_errors() {
  assert_error(0.999, 4.0, WaveformError::InvalidUiZoom);
  assert_error(19.001, 4.0, WaveformError::InvalidUiZoom);
  assert_error(std::numeric_limits<double>::quiet_NaN(), 4.0,
               WaveformError::InvalidUiZoom);
  assert_error(11.0, 0.999, WaveformError::InvalidScale);
  assert_error(11.0, 10.001, WaveformError::InvalidScale);
  assert_error(11.0, std::numeric_limits<double>::infinity(),
               WaveformError::InvalidScale);

  auto pcm_result =
      Pcm16::copy_from_channels(8'000U, nullptr, 0U, nullptr, 0U);
  assert(pcm_result);
  auto rectified_error =
      otgy::core::generate_rectified_uniform_waveform_tile_1x(
          *pcm_result.pcm(), 0.999, 4.0, 0U);
  assert(!rectified_error);
  assert(rectified_error.error() == WaveformError::InvalidUiZoom);
  auto amplitude_error =
      otgy::core::generate_signed_amplitude_waveform_tile_1x(
          *pcm_result.pcm(), 11.0, 10.001, 0U);
  assert(!amplitude_error);
  assert(amplitude_error.error() == WaveformError::InvalidScale);
  auto rectified_amplitude_error =
      otgy::core::generate_rectified_amplitude_waveform_tile_1x(
          *pcm_result.pcm(), std::numeric_limits<double>::infinity(), 4.0,
          0U);
  assert(!rectified_amplitude_error);
  assert(rectified_amplitude_error.error() ==
         WaveformError::InvalidUiZoom);
  auto two_x_error =
      otgy::core::generate_signed_uniform_waveform_tile_2x(
          *pcm_result.pcm(), 11.0, 10.001, 0U);
  assert(!two_x_error);
  assert(two_x_error.error() == WaveformError::InvalidScale);

  assert(otgy::core::waveform_error_message(WaveformError::None).empty());
  for (const auto error : {
           WaveformError::InvalidUiZoom,
           WaveformError::InvalidScale,
           WaveformError::AllocationFailure,
       }) {
    assert(!otgy::core::waveform_error_message(error).empty());
  }
  assert(!otgy::core::waveform_error_message(
              static_cast<WaveformError>(255))
              .empty());
}

}  // namespace

int main() {
  test_empty_pcm_geometry();
  test_signed_reference_golden_tile();
  test_rectified_reference_golden_tile();
  test_signed_uniform_2x_reference_golden_tile();
  test_signed_amplitude_reference_golden_tile();
  test_rectified_amplitude_reference_golden_tile();
  test_terminal_rounding_is_bounded();
  test_stable_errors();
  return 0;
}
