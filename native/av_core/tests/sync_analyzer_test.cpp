// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/offset_detector.hpp"
#include "av_core/timing/sync_analyzer.hpp"

#include <algorithm>
#include <array>
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

using otgy::core::OffsetCandidate;
using otgy::core::Pcm16;
using otgy::core::SyncAnalysisError;
using otgy::core::SyncAnalysisStage;

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
    const std::vector<OffsetCandidate>& candidates) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(candidates.size() * 32U);
  for (const auto& candidate : candidates) {
    for (const double value : {
             candidate.bpm,
             candidate.detector_offset_seconds,
             candidate.app_offset_seconds,
             candidate.fitness,
         }) {
      std::uint64_t bits = 0U;
      static_assert(sizeof(bits) == sizeof(value));
      std::memcpy(&bits, &value, sizeof(bits));
      append_u64_le(bytes, bits);
    }
  }
  return bytes;
}

void collect_stage(SyncAnalysisStage stage, void* context) noexcept {
  auto* stages = static_cast<std::vector<SyncAnalysisStage>*>(context);
  stages->push_back(stage);
}

[[nodiscard]] std::vector<OffsetCandidate> direct_candidates_for(
    const Pcm16& pcm) {
  auto bpm_result = otgy::core::detect_bpm_candidates(pcm);
  assert(bpm_result);
  auto offset_result =
      otgy::core::detect_offsets(pcm, *bpm_result.candidates());
  assert(offset_result);
  return std::move(offset_result).take_candidates();
}

void assert_matches_golden(const Pcm16& pcm, const std::string& golden_name,
                           std::size_t expected_count) {
  std::vector<SyncAnalysisStage> stages;
  stages.reserve(6U);
  auto result = otgy::core::analyze_sync(pcm, collect_stage, &stages);
  assert(result);
  assert(result.error() == SyncAnalysisError::None);
  assert(result.analysis() != nullptr);
  assert(result.analysis()->analyzed_frame_count() == pcm.frame_count());
  assert(result.analysis()->candidates().size() == expected_count);

  constexpr std::array<SyncAnalysisStage, 6U> kExpectedStages{{
      SyncAnalysisStage::DetectingOnsets,
      SyncAnalysisStage::ScanningIntervals,
      SyncAnalysisStage::RefiningCandidates,
      SyncAnalysisStage::SelectingBpm,
      SyncAnalysisStage::CalculatingOffsets,
      SyncAnalysisStage::FinalizingResults,
  }};
  assert(std::equal(stages.begin(), stages.end(), kExpectedStages.begin(),
                    kExpectedStages.end()));

  const auto golden = read_file(std::string{OTGY_SYNC_FIXTURE_DIR} + "/" +
                                golden_name);
  assert(serialize_candidates(result.analysis()->candidates()) == golden);
  assert(serialize_candidates(direct_candidates_for(pcm)) == golden);

  const auto copy = *result.analysis();
  const auto moved = std::move(result).take_analysis();
  assert(serialize_candidates(copy.candidates()) == golden);
  assert(serialize_candidates(moved.candidates()) == golden);
}

void test_reference_goldens_and_stages() {
  const auto original = pcm_from_planar_bytes(
      read_file(std::string{OTGY_MP3_FIXTURE_DIR} +
                "/generated_stereo_11025.planar.pcm16le"),
      11'025U);
  assert_matches_golden(
      original, "generated_stereo_11025.sync_candidates", 1U);

  const auto repeated = pcm_from_planar_bytes(
      read_file(std::string{OTGY_ONSET_FIXTURE_DIR} +
                "/generated_repeated_stereo_11025.planar.pcm16le"),
      11'025U);
  assert_matches_golden(
      repeated, "generated_repeated_stereo_11025.sync_candidates", 3U);

  const auto offbeat = pcm_from_planar_bytes(
      read_file(std::string{OTGY_OGG_VORBIS_FIXTURE_DIR} +
                "/generated_three_channel_16000.planar.pcm16le"),
      16'000U);
  assert_matches_golden(
      offbeat, "generated_three_channel_16000.sync_candidates", 1U);
}

void test_six_hundred_second_frame_limit() {
  constexpr std::uint32_t kSampleRate = 3'497U;
  constexpr std::uint64_t kLimit =
      static_cast<std::uint64_t>(kSampleRate) * 600U;
  assert(otgy::core::sync_analysis_frame_count(kSampleRate, kLimit - 1U) ==
         kLimit - 1U);
  assert(otgy::core::sync_analysis_frame_count(kSampleRate, kLimit) ==
         kLimit);
  assert(otgy::core::sync_analysis_frame_count(kSampleRate, kLimit + 1U) ==
         kLimit);
  assert(otgy::core::sync_analysis_frame_count(
             std::numeric_limits<std::uint32_t>::max(),
             std::numeric_limits<std::uint64_t>::max()) ==
         static_cast<std::uint64_t>(
             std::numeric_limits<std::uint32_t>::max()) *
             600U);
}

void test_six_hundred_second_analysis_prefix() {
  constexpr std::uint32_t kSampleRate = 3'497U;
  constexpr std::size_t kLimit =
      static_cast<std::size_t>(kSampleRate) * 600U;
  auto pcm_result = Pcm16::allocate(kSampleRate, kLimit + 256U);
  assert(pcm_result);
  auto pcm = std::move(pcm_result).take_pcm();
  for (std::size_t frame = kLimit; frame < pcm.frame_count(); ++frame) {
    pcm.mutable_channel_data(otgy::core::PcmChannel::Left)[frame] =
        std::numeric_limits<Pcm16::Sample>::max();
    pcm.mutable_channel_data(otgy::core::PcmChannel::Right)[frame] =
        std::numeric_limits<Pcm16::Sample>::min();
  }

  const auto result = otgy::core::analyze_sync(pcm);
  assert(result);
  assert(result.analysis()->analyzed_frame_count() == kLimit);
  assert(result.analysis()->candidates().size() == 1U);
  const auto& candidate = result.analysis()->candidates()[0];
  assert(candidate.bpm == 120.0);
  assert(candidate.detector_offset_seconds == 0.0);
  assert(candidate.app_offset_seconds == 0.0);
  assert(std::signbit(candidate.app_offset_seconds));
  assert(candidate.fitness == 1.0);
}

void test_empty_pcm_and_error_boundaries() {
  auto empty = Pcm16::allocate(11'025U, 0U);
  assert(empty);
  std::vector<SyncAnalysisStage> stages;
  stages.reserve(6U);
  const auto empty_result =
      otgy::core::analyze_sync(*empty.pcm(), collect_stage, &stages);
  assert(empty_result);
  assert(stages.size() == 6U);
  assert(empty_result.analysis()->analyzed_frame_count() == 0U);
  assert(empty_result.analysis()->candidates().size() == 1U);
  const auto& candidate = empty_result.analysis()->candidates()[0];
  assert(candidate.bpm == 120.0);
  assert(candidate.detector_offset_seconds == 0.0);
  assert(candidate.app_offset_seconds == 0.0);
  assert(std::signbit(candidate.app_offset_seconds));
  assert(candidate.fitness == 1.0);

  auto low_rate = Pcm16::allocate(3'496U, 0U);
  assert(low_rate);
  stages.clear();
  const auto low_rate_result =
      otgy::core::analyze_sync(*low_rate.pcm(), collect_stage, &stages);
  assert(!low_rate_result);
  assert(low_rate_result.analysis() == nullptr);
  assert(low_rate_result.error() == SyncAnalysisError::UnsupportedSampleRate);
  assert(stages.empty());

  auto oversized_rate =
      Pcm16::allocate(std::numeric_limits<std::uint32_t>::max(), 256U);
  assert(oversized_rate);
  stages.clear();
  const auto oversized_result = otgy::core::analyze_sync(
      *oversized_rate.pcm(), collect_stage, &stages);
  assert(!oversized_result);
  assert(oversized_result.analysis() == nullptr);
  assert(oversized_result.error() == SyncAnalysisError::InputTooLarge);
  assert(stages.size() == 1U);
  assert(stages[0] == SyncAnalysisStage::DetectingOnsets);

  assert(otgy::core::sync_analysis_error_message(SyncAnalysisError::None)
             .empty());
  for (const auto error : {SyncAnalysisError::UnsupportedSampleRate,
                           SyncAnalysisError::InputTooLarge,
                           SyncAnalysisError::AllocationFailure}) {
    assert(!otgy::core::sync_analysis_error_message(error).empty());
  }
  assert(!otgy::core::sync_analysis_error_message(
              static_cast<SyncAnalysisError>(255))
              .empty());
}

}  // namespace

int main() {
  test_reference_goldens_and_stages();
  test_six_hundred_second_frame_limit();
  test_six_hundred_second_analysis_prefix();
  test_empty_pcm_and_error_boundaries();
  return 0;
}
