// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/offset_detector.hpp"
#include "av_core/timing/onset_detector.hpp"

#include <cstddef>
#include <vector>

namespace otgy::core::detail {

[[nodiscard]] OffsetDetectionResult detect_offsets_from_onsets(
    const Pcm16& pcm, std::size_t frame_count,
    const std::vector<Onset>& detected_onsets,
    const std::vector<BpmCandidate>& bpm_candidates) noexcept;

}  // namespace otgy::core::detail
