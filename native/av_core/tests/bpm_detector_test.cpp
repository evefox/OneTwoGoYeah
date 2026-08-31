// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
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

using otgy::core::BpmCandidate;
using otgy::core::BpmDetectionError;
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

[[nodiscard]] std::vector<std::uint8_t> serialize_candidates(
    const std::vector<BpmCandidate>& candidates) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(candidates.size() * 16U);
  for (const auto& candidate : candidates) {
    std::uint64_t bpm_bits = 0U;
    std::uint64_t fitness_bits = 0U;
    static_assert(sizeof(bpm_bits) == sizeof(candidate.bpm));
    static_assert(sizeof(fitness_bits) == sizeof(candidate.fitness));
    std::memcpy(&bpm_bits, &candidate.bpm, sizeof(bpm_bits));
    std::memcpy(&fitness_bits, &candidate.fitness, sizeof(fitness_bits));
    append_u64_le(bytes, bpm_bits);
    append_u64_le(bytes, fitness_bits);
  }
  return bytes;
}

void assert_matches_golden(const Pcm16& pcm, const std::string& golden_name,
                           std::size_t expected_count) {
  auto result = otgy::core::detect_bpm_candidates(pcm);
  assert(result);
  assert(result.error() == BpmDetectionError::None);
  assert(result.candidates() != nullptr);
  assert(result.candidates()->size() == expected_count);

  const auto golden = read_file(std::string{OTGY_BPM_FIXTURE_DIR} + "/" +
                                golden_name);
  assert(serialize_candidates(*result.candidates()) == golden);

  const auto copy = *result.candidates();
  const auto moved = std::move(result).take_candidates();
  assert(serialize_candidates(copy) == golden);
  assert(serialize_candidates(moved) == golden);
}

void test_reference_goldens_and_order() {
  const auto original_bytes =
      read_file(std::string{OTGY_MP3_FIXTURE_DIR} +
                "/generated_stereo_11025.planar.pcm16le");
  const auto original = pcm_from_planar_bytes(original_bytes, 11'025U);
  assert_matches_golden(original,
                        "generated_stereo_11025.bpm_candidates", 1U);
  const auto default_result = otgy::core::detect_bpm_candidates(original);
  assert(default_result);
  assert((*default_result.candidates())[0].bpm == 120.0);
  assert((*default_result.candidates())[0].fitness == 1.0);

  const auto repeated_bytes =
      read_file(std::string{OTGY_ONSET_FIXTURE_DIR} +
                "/generated_repeated_stereo_11025.planar.pcm16le");
  const auto repeated = pcm_from_planar_bytes(repeated_bytes, 11'025U);
  assert_matches_golden(repeated,
                        "generated_repeated_stereo_11025.bpm_candidates", 3U);

  const auto result = otgy::core::detect_bpm_candidates(repeated);
  assert(result);
  const auto& candidates = *result.candidates();
  assert(candidates[0].bpm == 129.24970691676435);
  assert(candidates[0].fitness == 0.0089304293574241252);
  assert(candidates[1].bpm == 129.50274079874706);
  assert(candidates[1].fitness == 0.0089239658120640712);
  assert(candidates[2].bpm == 129.0);
  assert(candidates[2].fitness == 0.0088604295138801949);
  assert(candidates[0].fitness > candidates[1].fitness);
  assert(candidates[1].fitness > candidates[2].fitness);
  for (const auto& candidate : candidates) {
    assert(std::isfinite(candidate.bpm));
    assert(std::isfinite(candidate.fitness));
    assert(candidate.bpm >= 89.0);
    assert(candidate.bpm <= 205.0);
  }
}

void test_shared_stereo_mix() {
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
  const auto source_candidates = otgy::core::detect_bpm_candidates(source);
  const auto split_candidates =
      otgy::core::detect_bpm_candidates(*split_result.pcm());
  assert(source_candidates);
  assert(split_candidates);
  assert(serialize_candidates(*source_candidates.candidates()) ==
         serialize_candidates(*split_candidates.candidates()));
}

void test_empty_short_and_silent_pcm_use_reference_default() {
  for (const auto frame_count : {0U, 255U, 1'024U}) {
    auto pcm = Pcm16::allocate(11'025U, frame_count);
    assert(pcm);
    const auto result = otgy::core::detect_bpm_candidates(*pcm.pcm());
    assert(result);
    assert(result.candidates()->size() == 1U);
    assert((*result.candidates())[0].bpm == 120.0);
    assert((*result.candidates())[0].fitness == 1.0);
  }
}

void assert_failure_has_no_candidates(otgy::core::BpmDetectionResult result,
                                      BpmDetectionError expected) {
  assert(!result);
  assert(!result.has_value());
  assert(result.candidates() == nullptr);
  assert(result.error() == expected);
}

void test_input_errors_and_stable_messages() {
  auto low_rate = Pcm16::allocate(3'496U, 0U);
  assert(low_rate);
  assert_failure_has_no_candidates(
      otgy::core::detect_bpm_candidates(*low_rate.pcm()),
      BpmDetectionError::UnsupportedSampleRate);

  auto minimum_rate = Pcm16::allocate(3'497U, 0U);
  assert(minimum_rate);
  const auto minimum_rate_result =
      otgy::core::detect_bpm_candidates(*minimum_rate.pcm());
  assert(minimum_rate_result);
  assert(minimum_rate_result.candidates()->size() == 1U);
  assert((*minimum_rate_result.candidates())[0].bpm == 120.0);
  assert((*minimum_rate_result.candidates())[0].fitness == 1.0);

  auto oversized_rate =
      Pcm16::allocate(std::numeric_limits<std::uint32_t>::max(), 256U);
  assert(oversized_rate);
  assert_failure_has_no_candidates(
      otgy::core::detect_bpm_candidates(*oversized_rate.pcm()),
      BpmDetectionError::InputTooLarge);

  assert(otgy::core::bpm_detection_error_message(BpmDetectionError::None)
             .empty());
  for (const auto error : {BpmDetectionError::UnsupportedSampleRate,
                           BpmDetectionError::InputTooLarge,
                           BpmDetectionError::AllocationFailure}) {
    assert(!otgy::core::bpm_detection_error_message(error).empty());
  }
  assert(!otgy::core::bpm_detection_error_message(
              static_cast<BpmDetectionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_reference_goldens_and_order();
  test_shared_stereo_mix();
  test_empty_short_and_silent_pcm_use_reference_default();
  test_input_errors_and_stable_messages();
  return 0;
}
