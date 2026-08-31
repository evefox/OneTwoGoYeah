// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

/// One BPM candidate paired with its ArrowVortex detector phase and the
/// explicitly sign-converted App authority value.
struct OffsetCandidate final {
  double bpm;
  double detector_offset_seconds;
  double app_offset_seconds;
  double fitness;
};

enum class OffsetDetectionError : std::uint8_t {
  None = 0,
  InvalidBpmCandidates = 1,
  UnsupportedSampleRate = 2,
  InputTooLarge = 3,
  AllocationFailure = 4,
};

[[nodiscard]] constexpr std::string_view offset_detection_error_message(
    OffsetDetectionError error) noexcept {
  switch (error) {
    case OffsetDetectionError::None:
      return {};
    case OffsetDetectionError::InvalidBpmCandidates:
      return "Offset detection requires at most three finite 89..205 BPM candidates";
    case OffsetDetectionError::UnsupportedSampleRate:
      return "sample rate is too low for the ArrowVortex tempo gap window";
    case OffsetDetectionError::InputTooLarge:
      return "PCM dimensions exceed the ArrowVortex Offset detector limit";
    case OffsetDetectionError::AllocationFailure:
      return "unable to allocate Offset detector working memory";
  }
  return "unknown Offset detection error";
}

class OffsetDetectionResult final {
 public:
  explicit OffsetDetectionResult(
      std::vector<OffsetCandidate> candidates) noexcept;
  explicit OffsetDetectionResult(OffsetDetectionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const std::vector<OffsetCandidate>* candidates()
      const noexcept;
  [[nodiscard]] std::vector<OffsetCandidate>* candidates() noexcept;
  [[nodiscard]] OffsetDetectionError error() const noexcept;

  [[nodiscard]] std::vector<OffsetCandidate> take_candidates() &&;

 private:
  std::variant<std::vector<OffsetCandidate>, OffsetDetectionError> storage_;
};

/// Calculates a compatible Offset for each supplied BPM candidate.
///
/// Candidate order, BPM, and fitness are preserved. detector_offset_seconds
/// is the positive phase produced by FindTempo; app_offset_seconds is its
/// exact unary negation, matching ArrowVortex's application boundary.
[[nodiscard]] OffsetDetectionResult detect_offsets(
    const Pcm16& pcm,
    const std::vector<BpmCandidate>& bpm_candidates) noexcept;

}  // namespace otgy::core
