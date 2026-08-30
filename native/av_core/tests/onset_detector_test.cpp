// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/onset_detector.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::Onset;
using otgy::core::OnsetDetectionError;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;

[[nodiscard]] std::vector<std::uint8_t> read_file(
    const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Pcm16::Sample read_sample(const std::uint8_t* bytes) {
  const auto bits = static_cast<std::uint16_t>(bytes[0]) |
                    (static_cast<std::uint16_t>(bytes[1]) << 8U);
  return static_cast<Pcm16::Sample>(bits);
}

[[nodiscard]] Pcm16 pcm_from_planar_bytes(
    const std::vector<std::uint8_t>& bytes, std::uint32_t sample_rate) {
  assert(bytes.size() % (Pcm16::kChannelCount * Pcm16::kBytesPerSample) ==
         0U);
  const auto frame_count =
      bytes.size() / (Pcm16::kChannelCount * Pcm16::kBytesPerSample);
  std::vector<Pcm16::Sample> left(frame_count);
  std::vector<Pcm16::Sample> right(frame_count);
  for (std::size_t frame = 0U; frame < frame_count; ++frame) {
    left[frame] = read_sample(bytes.data() + frame * 2U);
    right[frame] =
        read_sample(bytes.data() + (frame_count + frame) * 2U);
  }
  auto result = Pcm16::copy_from_channels(
      sample_rate, left.data(), left.size(), right.data(), right.size());
  assert(result);
  return std::move(result).take_pcm();
}

void append_u64_le(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

[[nodiscard]] std::vector<std::uint8_t> serialize_onsets(
    const std::vector<Onset>& onsets) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(onsets.size() * 16U);
  for (const auto& onset : onsets) {
    append_u64_le(bytes, onset.frame_index);
    std::uint64_t strength_bits = 0U;
    static_assert(sizeof(strength_bits) == sizeof(onset.strength));
    std::memcpy(&strength_bits, &onset.strength, sizeof(strength_bits));
    append_u64_le(bytes, strength_bits);
  }
  return bytes;
}

void assert_matches_golden(const Pcm16& pcm, const std::string& golden_name,
                           const std::vector<std::uint64_t>& positions) {
  auto result = otgy::core::detect_onsets(pcm);
  assert(result);
  assert(result.error() == OnsetDetectionError::None);
  assert(result.onsets() != nullptr);
  assert(result.onsets()->size() == positions.size());
  for (std::size_t index = 0U; index < positions.size(); ++index) {
    assert((*result.onsets())[index].frame_index == positions[index]);
    assert((*result.onsets())[index].strength == 1.0);
  }

  const auto golden = read_file(std::string{OTGY_ONSET_FIXTURE_DIR} + "/" +
                                golden_name);
  assert(serialize_onsets(*result.onsets()) == golden);

  const auto copy = *result.onsets();
  auto moved = std::move(result).take_onsets();
  assert(serialize_onsets(copy) == golden);
  assert(serialize_onsets(moved) == golden);
}

void test_reference_goldens() {
  const auto original_bytes =
      read_file(std::string{OTGY_MP3_FIXTURE_DIR} +
                "/generated_stereo_11025.planar.pcm16le");
  const auto original = pcm_from_planar_bytes(original_bytes, 11'025U);
  assert_matches_golden(original, "generated_stereo_11025.onsets", {992U});

  const auto repeated_bytes =
      read_file(std::string{OTGY_ONSET_FIXTURE_DIR} +
                "/generated_repeated_stereo_11025.planar.pcm16le");
  const auto repeated = pcm_from_planar_bytes(repeated_bytes, 11'025U);
  assert_matches_golden(repeated,
                        "generated_repeated_stereo_11025.onsets",
                        {992U, 6'112U, 11'232U, 16'352U, 21'472U,
                         26'592U});
}

void test_reference_stereo_mix() {
  const auto bytes = read_file(std::string{OTGY_ONSET_FIXTURE_DIR} +
                               "/generated_repeated_stereo_11025.planar.pcm16le");
  const auto source = pcm_from_planar_bytes(bytes, 11'025U);
  std::vector<Pcm16::Sample> split_left(source.frame_count());
  std::vector<Pcm16::Sample> split_right(source.frame_count());
  const auto* left = source.channel_data(PcmChannel::Left);
  const auto* right = source.channel_data(PcmChannel::Right);
  for (std::size_t frame = 0U; frame < source.frame_count(); ++frame) {
    const int sum = static_cast<int>(left[frame]) +
                    static_cast<int>(right[frame]);
    const int bounded_left = std::clamp(
        sum,
        static_cast<int>(std::numeric_limits<Pcm16::Sample>::min()),
        static_cast<int>(std::numeric_limits<Pcm16::Sample>::max()));
    split_left[frame] = static_cast<Pcm16::Sample>(bounded_left);
    split_right[frame] = static_cast<Pcm16::Sample>(sum - bounded_left);
  }

  auto split_result = Pcm16::copy_from_channels(
      source.sample_rate(), split_left.data(), split_left.size(),
      split_right.data(), split_right.size());
  assert(split_result);
  const auto source_onsets = otgy::core::detect_onsets(source);
  const auto split_onsets =
      otgy::core::detect_onsets(*split_result.pcm());
  assert(source_onsets);
  assert(split_onsets);
  assert(serialize_onsets(*source_onsets.onsets()) ==
         serialize_onsets(*split_onsets.onsets()));

  auto swapped_result = Pcm16::copy_from_channels(
      source.sample_rate(), right, source.frame_count(), left,
      source.frame_count());
  assert(swapped_result);
  const auto swapped_onsets =
      otgy::core::detect_onsets(*swapped_result.pcm());
  assert(swapped_onsets);
  assert(serialize_onsets(*source_onsets.onsets()) ==
         serialize_onsets(*swapped_onsets.onsets()));
}

void test_empty_short_and_silent_pcm() {
  auto empty = Pcm16::allocate(11'025U, 0U);
  assert(empty);
  const auto empty_onsets = otgy::core::detect_onsets(*empty.pcm());
  assert(empty_onsets);
  assert(empty_onsets.onsets()->empty());

  auto short_pcm = Pcm16::allocate(11'025U, 255U);
  assert(short_pcm);
  const auto short_onsets = otgy::core::detect_onsets(*short_pcm.pcm());
  assert(short_onsets);
  assert(short_onsets.onsets()->empty());

  auto silent_pcm = Pcm16::allocate(11'025U, 1'024U);
  assert(silent_pcm);
  const auto silent_onsets = otgy::core::detect_onsets(*silent_pcm.pcm());
  assert(silent_onsets);
  assert(silent_onsets.onsets()->empty());
}

void test_input_limit_and_stable_errors() {
  auto pcm = Pcm16::allocate(std::numeric_limits<std::uint32_t>::max(), 256U);
  assert(pcm);
  const auto result = otgy::core::detect_onsets(*pcm.pcm());
  assert(!result);
  assert(!result.has_value());
  assert(result.onsets() == nullptr);
  assert(result.error() == OnsetDetectionError::InputTooLarge);

  assert(otgy::core::onset_detection_error_message(
             OnsetDetectionError::None)
             .empty());
  for (const auto error : {OnsetDetectionError::InputTooLarge,
                           OnsetDetectionError::AllocationFailure}) {
    assert(!otgy::core::onset_detection_error_message(error).empty());
  }
  assert(!otgy::core::onset_detection_error_message(
              static_cast<OnsetDetectionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_reference_goldens();
  test_reference_stereo_mix();
  test_empty_short_and_silent_pcm();
  test_input_limit_and_stable_errors();
  return 0;
}
