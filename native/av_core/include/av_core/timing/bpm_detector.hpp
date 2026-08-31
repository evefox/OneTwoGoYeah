// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

/// One ArrowVortex-compatible BPM candidate before Offset detection.
struct BpmCandidate final {
  double bpm;
  double fitness;
};

enum class BpmDetectionError : std::uint8_t {
  None = 0,
  UnsupportedSampleRate = 1,
  InputTooLarge = 2,
  AllocationFailure = 3,
};

[[nodiscard]] constexpr std::string_view bpm_detection_error_message(
    BpmDetectionError error) noexcept {
  switch (error) {
    case BpmDetectionError::None:
      return {};
    case BpmDetectionError::UnsupportedSampleRate:
      return "sample rate is too low for the ArrowVortex BPM interval window";
    case BpmDetectionError::InputTooLarge:
      return "PCM dimensions exceed the ArrowVortex BPM detector limit";
    case BpmDetectionError::AllocationFailure:
      return "unable to allocate BPM detector working memory";
  }
  return "unknown BPM detection error";
}

class BpmDetectionResult final {
 public:
  explicit BpmDetectionResult(std::vector<BpmCandidate> candidates) noexcept;
  explicit BpmDetectionResult(BpmDetectionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const std::vector<BpmCandidate>* candidates() const noexcept;
  [[nodiscard]] std::vector<BpmCandidate>* candidates() noexcept;
  [[nodiscard]] BpmDetectionError error() const noexcept;

  [[nodiscard]] std::vector<BpmCandidate> take_candidates() &&;

 private:
  std::variant<std::vector<BpmCandidate>, BpmDetectionError> storage_;
};

/// Detects and ranks at most three BPM candidates from canonical PCM.
///
/// The operation calls the authoritative onset detector, applies the
/// ArrowVortex onset-strength projection, and preserves raw binary64 BPM and
/// fitness values. Offset calculation is deliberately outside this slice.
[[nodiscard]] BpmDetectionResult detect_bpm_candidates(
    const Pcm16& pcm) noexcept;

}  // namespace otgy::core
