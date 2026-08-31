// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/offset_detector.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class SyncAnalysisStage : std::uint8_t {
  DetectingOnsets = 0,
  ScanningIntervals = 1,
  RefiningCandidates = 2,
  SelectingBpm = 3,
  CalculatingOffsets = 4,
  FinalizingResults = 5,
};

using SyncAnalysisStageCallback = void (*)(SyncAnalysisStage stage,
                                           void* context) noexcept;

enum class SyncAnalysisError : std::uint8_t {
  None = 0,
  UnsupportedSampleRate = 1,
  InputTooLarge = 2,
  AllocationFailure = 3,
};

[[nodiscard]] constexpr std::string_view sync_analysis_error_message(
    SyncAnalysisError error) noexcept {
  switch (error) {
    case SyncAnalysisError::None:
      return {};
    case SyncAnalysisError::UnsupportedSampleRate:
      return "sample rate is too low for ArrowVortex sync analysis";
    case SyncAnalysisError::InputTooLarge:
      return "PCM dimensions exceed the ArrowVortex sync analyzer limit";
    case SyncAnalysisError::AllocationFailure:
      return "unable to allocate sync analyzer working memory";
  }
  return "unknown sync analysis error";
}

class SyncAnalysis final {
 public:
  SyncAnalysis(std::uint64_t analyzed_frame_count,
               std::vector<OffsetCandidate> candidates) noexcept;

  SyncAnalysis(const SyncAnalysis&) = default;
  SyncAnalysis& operator=(const SyncAnalysis&) = default;
  SyncAnalysis(SyncAnalysis&&) noexcept = default;
  SyncAnalysis& operator=(SyncAnalysis&&) noexcept = default;
  ~SyncAnalysis() = default;

  [[nodiscard]] std::uint64_t analyzed_frame_count() const noexcept {
    return analyzed_frame_count_;
  }

  [[nodiscard]] const std::vector<OffsetCandidate>& candidates()
      const noexcept {
    return candidates_;
  }

 private:
  std::uint64_t analyzed_frame_count_;
  std::vector<OffsetCandidate> candidates_;
};

class SyncAnalysisResult final {
 public:
  explicit SyncAnalysisResult(SyncAnalysis analysis) noexcept;
  explicit SyncAnalysisResult(SyncAnalysisError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SyncAnalysis* analysis() const noexcept;
  [[nodiscard]] SyncAnalysis* analysis() noexcept;
  [[nodiscard]] SyncAnalysisError error() const noexcept;

  [[nodiscard]] SyncAnalysis take_analysis() &&;

 private:
  std::variant<SyncAnalysis, SyncAnalysisError> storage_;
};

[[nodiscard]] constexpr std::uint64_t sync_analysis_frame_count(
    std::uint32_t sample_rate,
    std::uint64_t available_frame_count) noexcept {
  constexpr std::uint64_t kMaximumAnalysisSeconds = 600U;
  const std::uint64_t maximum_frames =
      static_cast<std::uint64_t>(sample_rate) * kMaximumAnalysisSeconds;
  return available_frame_count < maximum_frames ? available_frame_count
                                                 : maximum_frames;
}

/// Runs the complete ArrowVortex-compatible onset/BPM/Offset analysis once.
///
/// Analysis always starts at PCM frame zero and is capped at 600 seconds.
/// The optional callback receives the six real phase transitions in enum
/// order. It must not throw and can be invoked synchronously before return.
[[nodiscard]] SyncAnalysisResult analyze_sync(
    const Pcm16& pcm, SyncAnalysisStageCallback stage_callback = nullptr,
    void* stage_context = nullptr) noexcept;

}  // namespace otgy::core
