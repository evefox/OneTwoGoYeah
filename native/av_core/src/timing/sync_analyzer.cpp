// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/sync_analyzer.hpp"

#include "bpm_detector_internal.hpp"
#include "offset_detector_internal.hpp"
#include "onset_detector_internal.hpp"
#include "tempo_constants.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace otgy::core {
namespace {

void report_stage(SyncAnalysisStageCallback callback, void* context,
                  SyncAnalysisStage stage) noexcept {
  if (callback != nullptr) {
    callback(stage, context);
  }
}

struct BpmStageContext final {
  SyncAnalysisStageCallback callback;
  void* context;
};

void report_bpm_stage(detail::BpmCalculationStage stage,
                      void* opaque_context) noexcept {
  auto* context = static_cast<BpmStageContext*>(opaque_context);
  const auto public_stage =
      stage == detail::BpmCalculationStage::RefiningCandidates
          ? SyncAnalysisStage::RefiningCandidates
          : SyncAnalysisStage::SelectingBpm;
  report_stage(context->callback, context->context, public_stage);
}

[[nodiscard]] SyncAnalysisError map_onset_error(
    OnsetDetectionError error) noexcept {
  switch (error) {
    case OnsetDetectionError::InputTooLarge:
      return SyncAnalysisError::InputTooLarge;
    case OnsetDetectionError::AllocationFailure:
      return SyncAnalysisError::AllocationFailure;
    case OnsetDetectionError::None:
      return SyncAnalysisError::None;
  }
  return SyncAnalysisError::AllocationFailure;
}

[[nodiscard]] SyncAnalysisError map_bpm_error(
    BpmDetectionError error) noexcept {
  switch (error) {
    case BpmDetectionError::UnsupportedSampleRate:
      return SyncAnalysisError::UnsupportedSampleRate;
    case BpmDetectionError::InputTooLarge:
      return SyncAnalysisError::InputTooLarge;
    case BpmDetectionError::AllocationFailure:
      return SyncAnalysisError::AllocationFailure;
    case BpmDetectionError::None:
      return SyncAnalysisError::None;
  }
  return SyncAnalysisError::AllocationFailure;
}

[[nodiscard]] SyncAnalysisError map_offset_error(
    OffsetDetectionError error) noexcept {
  switch (error) {
    case OffsetDetectionError::UnsupportedSampleRate:
      return SyncAnalysisError::UnsupportedSampleRate;
    case OffsetDetectionError::InputTooLarge:
      return SyncAnalysisError::InputTooLarge;
    case OffsetDetectionError::AllocationFailure:
      return SyncAnalysisError::AllocationFailure;
    case OffsetDetectionError::InvalidBpmCandidates:
      return SyncAnalysisError::AllocationFailure;
    case OffsetDetectionError::None:
      return SyncAnalysisError::None;
  }
  return SyncAnalysisError::AllocationFailure;
}

}  // namespace

SyncAnalysis::SyncAnalysis(
    std::uint64_t analyzed_frame_count,
    std::vector<OffsetCandidate> candidates) noexcept
    : analyzed_frame_count_(analyzed_frame_count),
      candidates_(std::move(candidates)) {}

SyncAnalysisResult::SyncAnalysisResult(SyncAnalysis analysis) noexcept
    : storage_(std::move(analysis)) {}

SyncAnalysisResult::SyncAnalysisResult(SyncAnalysisError error) noexcept
    : storage_(error) {}

bool SyncAnalysisResult::has_value() const noexcept {
  return std::holds_alternative<SyncAnalysis>(storage_);
}

const SyncAnalysis* SyncAnalysisResult::analysis() const noexcept {
  return std::get_if<SyncAnalysis>(&storage_);
}

SyncAnalysis* SyncAnalysisResult::analysis() noexcept {
  return std::get_if<SyncAnalysis>(&storage_);
}

SyncAnalysisError SyncAnalysisResult::error() const noexcept {
  if (const auto* error = std::get_if<SyncAnalysisError>(&storage_)) {
    return *error;
  }
  return SyncAnalysisError::None;
}

SyncAnalysis SyncAnalysisResult::take_analysis() && {
  return std::move(std::get<SyncAnalysis>(storage_));
}

SyncAnalysisResult analyze_sync(
    const Pcm16& pcm, SyncAnalysisStageCallback stage_callback,
    void* stage_context) noexcept {
  if (!detail::supports_detected_tempo_sample_rate(pcm.sample_rate())) {
    return SyncAnalysisResult{SyncAnalysisError::UnsupportedSampleRate};
  }

  const auto analyzed_frame_count = static_cast<std::size_t>(
      sync_analysis_frame_count(
          pcm.sample_rate(),
          static_cast<std::uint64_t>(pcm.frame_count())));

  report_stage(stage_callback, stage_context,
               SyncAnalysisStage::DetectingOnsets);
  auto onset_result =
      detail::detect_onsets_prefix(pcm, analyzed_frame_count);
  if (!onset_result) {
    return SyncAnalysisResult{map_onset_error(onset_result.error())};
  }

  report_stage(stage_callback, stage_context,
               SyncAnalysisStage::ScanningIntervals);
  BpmStageContext bpm_stage_context{stage_callback, stage_context};
  auto bpm_result = detail::detect_bpm_candidates_from_onsets(
      pcm, analyzed_frame_count, *onset_result.onsets(), report_bpm_stage,
      &bpm_stage_context);
  if (!bpm_result) {
    return SyncAnalysisResult{map_bpm_error(bpm_result.error())};
  }

  report_stage(stage_callback, stage_context,
               SyncAnalysisStage::CalculatingOffsets);
  auto offset_result = detail::detect_offsets_from_onsets(
      pcm, analyzed_frame_count, *onset_result.onsets(),
      *bpm_result.candidates());
  if (!offset_result) {
    return SyncAnalysisResult{map_offset_error(offset_result.error())};
  }

  report_stage(stage_callback, stage_context,
               SyncAnalysisStage::FinalizingResults);
  return SyncAnalysisResult{SyncAnalysis{
      static_cast<std::uint64_t>(analyzed_frame_count),
      std::move(offset_result).take_candidates()}};
}

}  // namespace otgy::core
