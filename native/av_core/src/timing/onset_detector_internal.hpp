// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/timing/onset_detector.hpp"

#include <cstddef>

namespace otgy::core::detail {

[[nodiscard]] OnsetDetectionResult detect_onsets_prefix(
    const Pcm16& pcm, std::size_t frame_count) noexcept;

}  // namespace otgy::core::detail
