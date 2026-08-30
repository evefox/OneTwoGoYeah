// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/mp3_decoder.hpp"
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

using otgy::core::Mp3DecodeError;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;

[[nodiscard]] std::vector<std::uint8_t> read_fixture(
    const std::string& name) {
  const std::string path =
      std::string{OTGY_MP3_FIXTURE_DIR} + "/" + name;
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

void assert_matches_golden(const std::string& mp3_name,
                           const std::string& pcm_name,
                           std::uint32_t expected_sample_rate,
                           std::size_t expected_frame_count,
                           std::string_view expected_pcm_sha256,
                           bool expect_identical_channels) {
  const auto encoded = read_fixture(mp3_name);
  const auto expected_pcm = read_fixture(pcm_name);
  auto result = otgy::core::decode_mp3(encoded.data(), encoded.size());

  assert(result.has_value());
  assert(result.error() == Mp3DecodeError::None);
  assert(result.pcm() != nullptr);
  const auto& pcm = *result.pcm();
  assert(pcm.sample_rate() == expected_sample_rate);
  assert(pcm.frame_count() == expected_frame_count);
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
  assert(moved.frame_count() == expected_frame_count);
}

void test_generated_golden_fixtures() {
  assert_matches_golden("generated_mono_8000.mp3",
                        "generated_mono_8000.planar.pcm16le", 8'000U,
                        9'216U,
                        "b1d85a34af1e9e16f7530f6efdbde26e3f8e5e5a71fc8c7345aed2345a77a451",
                        true);

  const auto stereo = read_fixture("generated_stereo_11025.mp3");
  constexpr std::array<std::uint8_t, 4> kXing{{'X', 'i', 'n', 'g'}};
  assert(std::search(stereo.begin(), stereo.end(), kXing.begin(), kXing.end()) !=
         stereo.end());
  assert_matches_golden("generated_stereo_11025.mp3",
                        "generated_stereo_11025.planar.pcm16le", 11'025U,
                        9'216U,
                        "a5ba7143a10ad4cc9d235d00069e81ea41029127e29a4bfbf04a15a10f9ad460",
                        false);
}

void test_leading_id3_tag_is_skipped() {
  const auto encoded = read_fixture("generated_mono_8000.mp3");
  std::vector<std::uint8_t> tagged{{
      'I', 'D', '3', 0x04U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x04U, 'T', 'E', 'S', 'T',
  }};
  tagged.insert(tagged.end(), encoded.begin(), encoded.end());

  auto result = otgy::core::decode_mp3(tagged.data(), tagged.size());
  assert(result);
  assert(result.pcm()->sample_rate() == 8'000U);
  assert(result.pcm()->frame_count() == 9'216U);
  assert(planar_pcm16le_bytes(*result.pcm()) ==
         read_fixture("generated_mono_8000.planar.pcm16le"));
}

void assert_failure_has_no_pcm(otgy::core::Mp3DecodeResult result,
                               Mp3DecodeError expected) {
  assert(!result);
  assert(!result.has_value());
  assert(result.error() == expected);
  assert(result.pcm() == nullptr);
}

void test_input_errors() {
  std::uint8_t byte = 0U;
  assert_failure_has_no_pcm(otgy::core::decode_mp3(nullptr, 1U),
                            Mp3DecodeError::NullInput);
  assert_failure_has_no_pcm(otgy::core::decode_mp3(nullptr, 0U),
                            Mp3DecodeError::EmptyInput);
  assert_failure_has_no_pcm(otgy::core::decode_mp3(&byte, 0U),
                            Mp3DecodeError::EmptyInput);
  assert_failure_has_no_pcm(
      otgy::core::decode_mp3(&byte, std::numeric_limits<std::size_t>::max()),
      Mp3DecodeError::InputTooLarge);

  const std::array<std::uint8_t, 24> corrupt{{
      0x49U, 0x44U, 0x33U, 0x04U, 0x00U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x6eU, 0x6fU, 0x74U, 0x2dU, 0x6dU, 0x70U,
      0x33U, 0x2dU, 0x61U, 0x75U, 0x64U, 0x69U, 0x6fU, 0x00U,
  }};
  assert_failure_has_no_pcm(
      otgy::core::decode_mp3(corrupt.data(), corrupt.size()),
      Mp3DecodeError::InvalidData);

  auto truncated = read_fixture("generated_stereo_11025.mp3");
  truncated.resize(64U);
  assert_failure_has_no_pcm(
      otgy::core::decode_mp3(truncated.data(), truncated.size()),
      Mp3DecodeError::InvalidData);
}

void test_format_change_discards_partial_pcm() {
  auto mixed_stream = read_fixture("generated_mono_8000.mp3");
  const auto stereo = read_fixture("generated_stereo_11025.mp3");
  mixed_stream.insert(mixed_stream.end(), stereo.begin(), stereo.end());

  // The mono frames are decoded before the incompatible stereo frame is
  // reached. The public result still exposes only the error, never that
  // already-staged partial PCM.
  assert_failure_has_no_pcm(
      otgy::core::decode_mp3(mixed_stream.data(), mixed_stream.size()),
      Mp3DecodeError::StreamFormatChanged);
}

void test_stable_error_messages() {
  assert(otgy::core::mp3_decode_error_message(Mp3DecodeError::None).empty());
  for (const auto error : {
           Mp3DecodeError::NullInput,
           Mp3DecodeError::EmptyInput,
           Mp3DecodeError::InputTooLarge,
           Mp3DecodeError::InvalidData,
           Mp3DecodeError::StreamFormatChanged,
           Mp3DecodeError::SizeOverflow,
           Mp3DecodeError::AllocationFailure,
       }) {
    assert(!otgy::core::mp3_decode_error_message(error).empty());
  }
  assert(!otgy::core::mp3_decode_error_message(
              static_cast<Mp3DecodeError>(255))
              .empty());
}

}  // namespace

int main() {
  test_generated_golden_fixtures();
  test_leading_id3_tag_is_skipped();
  test_input_errors();
  test_format_change_discards_partial_pcm();
  test_stable_error_messages();
  return 0;
}
