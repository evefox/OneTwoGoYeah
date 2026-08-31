// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/onset_detector.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace otgy::core::detail {

enum class BpmCalculationStage : std::uint8_t {
  RefiningCandidates,
  SelectingBpm,
};

using BpmCalculationStageCallback = void (*)(
    BpmCalculationStage stage, void* context) noexcept;

[[nodiscard]] BpmDetectionResult detect_bpm_candidates_from_onsets(
    const Pcm16& pcm, std::size_t frame_count,
    const std::vector<Onset>& detected_onsets,
    BpmCalculationStageCallback stage_callback = nullptr,
    void* stage_context = nullptr) noexcept;

}  // namespace otgy::core::detail
