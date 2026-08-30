// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"

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

using otgy::core::OggVorbisDecodeError;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;

[[nodiscard]] std::vector<std::uint8_t> read_fixture(
    const std::string& name) {
  const std::string path =
      std::string{OTGY_OGG_VORBIS_FIXTURE_DIR} + "/" + name;
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::uint8_t> planar_pcm16le_bytes(
    const Pcm16& pcm) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(pcm.total_byte_size());
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* samples = pcm.channel_data(channel);
    for (std::size_t index = 0U; index < pcm.frame_count(); ++index) {
      const auto bits = static_cast<std::uint16_t>(samples[index]);
      bytes.push_back(static_cast<std::uint8_t>(bits & 0xffU));
      bytes.push_back(static_cast<std::uint8_t>(bits >> 8U));
    }
  }
  return bytes;
}

void assert_matches_golden(const std::string& ogg_name,
                           const std::string& pcm_name,
                           std::uint32_t expected_sample_rate,
                           std::string_view expected_pcm_sha256,
                           bool expect_identical_channels) {
  const auto encoded = read_fixture(ogg_name);
  const auto expected_pcm = read_fixture(pcm_name);
  auto result =
      otgy::core::decode_ogg_vorbis(encoded.data(), encoded.size());

  assert(result.has_value());
  assert(result.error() == OggVorbisDecodeError::None);
  assert(result.pcm() != nullptr);
  const auto& pcm = *result.pcm();
  assert(pcm.sample_rate() == expected_sample_rate);
  assert(pcm.frame_count() == 8'192U);
  assert(pcm.total_byte_size() == expected_pcm.size());
  assert(planar_pcm16le_bytes(pcm) == expected_pcm);
  const auto hash = otgy::core::pcm16le_sha256_hex(pcm);
  assert(std::string_view(hash.data(), hash.size()) == expected_pcm_sha256);

  const auto* left = pcm.channel_data(PcmChannel::Left);
  const auto* right = pcm.channel_data(PcmChannel::Right);
  assert(left != nullptr);
  assert(right != nullptr);
  const auto channels_identical =
      std::equal(left, left + pcm.frame_count(), right);
  assert(channels_identical == expect_identical_channels);

  Pcm16 moved = std::move(result).take_pcm();
  assert(moved.sample_rate() == expected_sample_rate);
  assert(moved.frame_count() == 8'192U);
}

void test_generated_golden_fixtures() {
  assert_matches_golden("generated_mono_8000.ogg",
                        "generated_mono_8000.planar.pcm16le", 8'000U,
                        "47f4ad0d6677be6728ddc280eb6269f2d8e5c50de935d84f798be1da0f176f56",
                        true);
  assert_matches_golden("generated_stereo_11025.ogg",
                        "generated_stereo_11025.planar.pcm16le", 11'025U,
                        "176275320fa9703e04e26040b18e0f5e5e91c6405e75f2b317c9acbd8ff8f58e",
                        false);
  assert_matches_golden("generated_three_channel_16000.ogg",
                        "generated_three_channel_16000.planar.pcm16le",
                        16'000U,
                        "ed097f42dfeb0679fddd4ae6d2f3ce987e520c0509c4bc6abd1c744311f69987",
                        false);
}

void assert_failure_has_no_pcm(otgy::core::OggVorbisDecodeResult result,
                               OggVorbisDecodeError expected) {
  assert(!result);
  assert(!result.has_value());
  assert(result.error() == expected);
  assert(result.pcm() == nullptr);
}

void test_input_errors() {
  std::uint8_t byte = 0U;
  assert_failure_has_no_pcm(otgy::core::decode_ogg_vorbis(nullptr, 1U),
                            OggVorbisDecodeError::NullInput);
  assert_failure_has_no_pcm(otgy::core::decode_ogg_vorbis(nullptr, 0U),
                            OggVorbisDecodeError::EmptyInput);
  assert_failure_has_no_pcm(otgy::core::decode_ogg_vorbis(&byte, 0U),
                            OggVorbisDecodeError::EmptyInput);
  assert_failure_has_no_pcm(
      otgy::core::decode_ogg_vorbis(
          &byte, std::numeric_limits<std::size_t>::max()),
      OggVorbisDecodeError::InputTooLarge);

  const std::array<std::uint8_t, 20> corrupt{{
      'O', 'g', 'g', 'S', 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U,
  }};
  assert_failure_has_no_pcm(
      otgy::core::decode_ogg_vorbis(corrupt.data(), corrupt.size()),
      OggVorbisDecodeError::InvalidData);

  auto damaged = read_fixture("generated_mono_8000.ogg");
  damaged[damaged.size() / 2U] ^= 0xffU;
  assert_failure_has_no_pcm(
      otgy::core::decode_ogg_vorbis(damaged.data(), damaged.size()),
      OggVorbisDecodeError::InvalidData);

  auto truncated = read_fixture("generated_mono_8000.ogg");
  truncated.resize(truncated.size() - 64U);
  assert_failure_has_no_pcm(
      otgy::core::decode_ogg_vorbis(truncated.data(), truncated.size()),
      OggVorbisDecodeError::InvalidData);
}

void test_chained_format_change_has_no_partial_pcm() {
  auto changed_stream = read_fixture("generated_mono_8000.ogg");
  const auto stereo = read_fixture("generated_stereo_11025.ogg");
  changed_stream.insert(changed_stream.end(), stereo.begin(), stereo.end());

  assert_failure_has_no_pcm(
      otgy::core::decode_ogg_vorbis(changed_stream.data(),
                                    changed_stream.size()),
      OggVorbisDecodeError::StreamFormatChanged);
}

void test_stable_error_messages() {
  assert(otgy::core::ogg_vorbis_decode_error_message(
             OggVorbisDecodeError::None)
             .empty());
  for (const auto error : {
           OggVorbisDecodeError::NullInput,
           OggVorbisDecodeError::EmptyInput,
           OggVorbisDecodeError::InputTooLarge,
           OggVorbisDecodeError::InvalidData,
           OggVorbisDecodeError::StreamFormatChanged,
           OggVorbisDecodeError::SizeOverflow,
           OggVorbisDecodeError::AllocationFailure,
       }) {
    assert(!otgy::core::ogg_vorbis_decode_error_message(error).empty());
  }
  assert(!otgy::core::ogg_vorbis_decode_error_message(
              static_cast<OggVorbisDecodeError>(255))
              .empty());
}

}  // namespace

int main() {
  test_generated_golden_fixtures();
  test_input_errors();
  test_chained_format_change_has_no_partial_pcm();
  test_stable_error_messages();
  return 0;
}
