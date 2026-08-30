// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

/// One ArrowVortex-compatible onset in canonical PCM frame coordinates.
struct Onset final {
  std::uint64_t frame_index;
  double strength;
};

/// Stable failures returned by onset detection.
enum class OnsetDetectionError : std::uint8_t {
  None = 0,
  InputTooLarge = 1,
  AllocationFailure = 2,
};

[[nodiscard]] constexpr std::string_view onset_detection_error_message(
    OnsetDetectionError error) noexcept {
  switch (error) {
    case OnsetDetectionError::None:
      return {};
    case OnsetDetectionError::InputTooLarge:
      return "PCM dimensions exceed the ArrowVortex onset detector limit";
    case OnsetDetectionError::AllocationFailure:
      return "unable to allocate onset detector working memory";
  }
  return "unknown onset detection error";
}

/// Owning onset result. Failures never expose a partial onset sequence.
class OnsetDetectionResult final {
 public:
  explicit OnsetDetectionResult(std::vector<Onset> onsets) noexcept;
  explicit OnsetDetectionResult(OnsetDetectionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const std::vector<Onset>* onsets() const noexcept;
  [[nodiscard]] std::vector<Onset>* onsets() noexcept;
  [[nodiscard]] OnsetDetectionError error() const noexcept;

  [[nodiscard]] std::vector<Onset> take_onsets() &&;

 private:
  std::variant<std::vector<Onset>, OnsetDetectionError> storage_;
};

/// Detects onsets in canonical PCM with ArrowVortex's fixed single-thread
/// complex-domain detector.
///
/// Left and Right are mixed with the reference PCM16 arithmetic. Empty input
/// and inputs shorter than the 256-frame analysis hop succeed with an empty
/// sequence. No input alias is retained.
[[nodiscard]] OnsetDetectionResult detect_onsets(const Pcm16& pcm) noexcept;

}  // namespace otgy::core
