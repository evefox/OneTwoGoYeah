// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/offset_detector.hpp"

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
using otgy::core::OffsetCandidate;
using otgy::core::OffsetDetectionError;
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

[[nodiscard]] std::vector<std::uint8_t> serialize_offsets(
    const std::vector<OffsetCandidate>& candidates) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(candidates.size() * 16U);
  for (const auto& candidate : candidates) {
    std::uint64_t detector_bits = 0U;
    std::uint64_t app_bits = 0U;
    static_assert(sizeof(detector_bits) ==
                  sizeof(candidate.detector_offset_seconds));
    static_assert(sizeof(app_bits) == sizeof(candidate.app_offset_seconds));
    std::memcpy(&detector_bits, &candidate.detector_offset_seconds,
                sizeof(detector_bits));
    std::memcpy(&app_bits, &candidate.app_offset_seconds, sizeof(app_bits));
    append_u64_le(bytes, detector_bits);
    append_u64_le(bytes, app_bits);
  }
  return bytes;
}

[[nodiscard]] std::vector<BpmCandidate> bpm_candidates_for(
    const Pcm16& pcm) {
  auto result = otgy::core::detect_bpm_candidates(pcm);
  assert(result);
  return std::move(result).take_candidates();
}

void assert_matches_golden(const Pcm16& pcm, const std::string& golden_name,
                           std::size_t expected_count) {
  const auto bpm_candidates = bpm_candidates_for(pcm);
  auto result = otgy::core::detect_offsets(pcm, bpm_candidates);
  assert(result);
  assert(result.error() == OffsetDetectionError::None);
  assert(result.candidates() != nullptr);
  assert(result.candidates()->size() == expected_count);
  for (std::size_t index = 0U; index < expected_count; ++index) {
    assert((*result.candidates())[index].bpm == bpm_candidates[index].bpm);
    assert((*result.candidates())[index].fitness ==
           bpm_candidates[index].fitness);
  }

  const auto golden = read_file(std::string{OTGY_OFFSET_FIXTURE_DIR} + "/" +
                                golden_name);
  assert(serialize_offsets(*result.candidates()) == golden);

  const auto copy = *result.candidates();
  const auto moved = std::move(result).take_candidates();
  assert(serialize_offsets(copy) == golden);
  assert(serialize_offsets(moved) == golden);
}

void test_reference_goldens_and_sign_conversion() {
  const auto original_bytes =
      read_file(std::string{OTGY_MP3_FIXTURE_DIR} +
                "/generated_stereo_11025.planar.pcm16le");
  const auto original = pcm_from_planar_bytes(original_bytes, 11'025U);
  assert_matches_golden(original, "generated_stereo_11025.offsets", 1U);
  const auto original_result = otgy::core::detect_offsets(
      original, bpm_candidates_for(original));
  assert(original_result);
  assert((*original_result.candidates())[0].detector_offset_seconds ==
         0.089977324263038547);
  assert((*original_result.candidates())[0].app_offset_seconds ==
         -0.089977324263038547);

  const auto repeated_bytes =
      read_file(std::string{OTGY_ONSET_FIXTURE_DIR} +
                "/generated_repeated_stereo_11025.planar.pcm16le");
  const auto repeated = pcm_from_planar_bytes(repeated_bytes, 11'025U);
  assert_matches_golden(repeated,
                        "generated_repeated_stereo_11025.offsets", 3U);
  const auto repeated_result = otgy::core::detect_offsets(
      repeated, bpm_candidates_for(repeated));
  assert(repeated_result);
  const auto& candidates = *repeated_result.candidates();
  assert(candidates[0].detector_offset_seconds ==
         0.090521541950113374);
  assert(candidates[0].app_offset_seconds == -0.090521541950113374);
  assert(candidates[1].detector_offset_seconds ==
         0.093242630385487524);
  assert(candidates[1].app_offset_seconds == -0.093242630385487524);
  assert(candidates[2].detector_offset_seconds ==
         0.088526077097505665);
  assert(candidates[2].app_offset_seconds == -0.088526077097505665);
}

void test_offbeat_selection() {
  const auto bytes =
      read_file(std::string{OTGY_OGG_VORBIS_FIXTURE_DIR} +
                "/generated_three_channel_16000.planar.pcm16le");
  const auto pcm = pcm_from_planar_bytes(bytes, 16'000U);
  assert_matches_golden(pcm, "generated_three_channel_16000.offsets", 1U);
  const auto result =
      otgy::core::detect_offsets(pcm, bpm_candidates_for(pcm));
  assert(result);
  assert((*result.candidates())[0].bpm == 120.0);
  assert((*result.candidates())[0].detector_offset_seconds == 0.25);
  assert((*result.candidates())[0].app_offset_seconds == -0.25);
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
  const auto bpm_candidates = bpm_candidates_for(source);
  const auto source_offsets =
      otgy::core::detect_offsets(source, bpm_candidates);
  const auto split_offsets =
      otgy::core::detect_offsets(*split_result.pcm(), bpm_candidates);
  assert(source_offsets);
  assert(split_offsets);
  assert(serialize_offsets(*source_offsets.candidates()) ==
         serialize_offsets(*split_offsets.candidates()));
}

void test_empty_short_and_silent_pcm() {
  const std::vector<BpmCandidate> candidates{{120.0, 1.0}};
  for (const auto frame_count : {0U, 255U, 1'024U}) {
    auto pcm = Pcm16::allocate(11'025U, frame_count);
    assert(pcm);
    const auto result =
        otgy::core::detect_offsets(*pcm.pcm(), candidates);
    assert(result);
    assert(result.candidates()->size() == 1U);
    assert((*result.candidates())[0].detector_offset_seconds == 0.0);
    assert(!std::signbit(
        (*result.candidates())[0].detector_offset_seconds));
    assert((*result.candidates())[0].app_offset_seconds == 0.0);
    assert(std::signbit((*result.candidates())[0].app_offset_seconds));
  }

  auto low_rate = Pcm16::allocate(1U, 0U);
  assert(low_rate);
  const auto empty_result = otgy::core::detect_offsets(*low_rate.pcm(), {});
  assert(empty_result);
  assert(empty_result.candidates()->empty());
}

void assert_failure_has_no_candidates(
    otgy::core::OffsetDetectionResult result,
    OffsetDetectionError expected) {
  assert(!result);
  assert(!result.has_value());
  assert(result.candidates() == nullptr);
  assert(result.error() == expected);
}

void test_errors_and_stable_messages() {
  auto pcm = Pcm16::allocate(11'025U, 256U);
  assert(pcm);
  for (const auto& invalid : {
           std::vector<BpmCandidate>{{88.999, 1.0}},
           std::vector<BpmCandidate>{{205.001, 1.0}},
           std::vector<BpmCandidate>{{
               std::numeric_limits<double>::quiet_NaN(), 1.0}},
           std::vector<BpmCandidate>{{
               120.0, std::numeric_limits<double>::infinity()}},
           std::vector<BpmCandidate>{{120.0, 1.0}, {121.0, 1.0},
                                      {122.0, 1.0}, {123.0, 1.0}},
       }) {
    assert_failure_has_no_candidates(
        otgy::core::detect_offsets(*pcm.pcm(), invalid),
        OffsetDetectionError::InvalidBpmCandidates);
  }

  auto low_rate = Pcm16::allocate(3'496U, 0U);
  assert(low_rate);
  assert_failure_has_no_candidates(
      otgy::core::detect_offsets(*low_rate.pcm(), {{120.0, 1.0}}),
      OffsetDetectionError::UnsupportedSampleRate);

  auto minimum_rate = Pcm16::allocate(3'497U, 0U);
  assert(minimum_rate);
  const auto minimum_result = otgy::core::detect_offsets(
      *minimum_rate.pcm(), {{120.0, 1.0}});
  assert(minimum_result);
  assert(minimum_result.candidates()->size() == 1U);

  auto oversized_rate =
      Pcm16::allocate(std::numeric_limits<std::uint32_t>::max(), 256U);
  assert(oversized_rate);
  assert_failure_has_no_candidates(
      otgy::core::detect_offsets(*oversized_rate.pcm(), {{120.0, 1.0}}),
      OffsetDetectionError::InputTooLarge);

  assert(otgy::core::offset_detection_error_message(
             OffsetDetectionError::None)
             .empty());
  for (const auto error : {
           OffsetDetectionError::InvalidBpmCandidates,
           OffsetDetectionError::UnsupportedSampleRate,
           OffsetDetectionError::InputTooLarge,
           OffsetDetectionError::AllocationFailure,
       }) {
    assert(!otgy::core::offset_detection_error_message(error).empty());
  }
  assert(!otgy::core::offset_detection_error_message(
              static_cast<OffsetDetectionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_reference_goldens_and_sign_conversion();
  test_offbeat_selection();
  test_shared_stereo_mix();
  test_empty_short_and_silent_pcm();
  test_errors_and_stable_messages();
  return 0;
}
