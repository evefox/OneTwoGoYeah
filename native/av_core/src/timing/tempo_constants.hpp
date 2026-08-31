// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace otgy::core::detail {

constexpr double kMinimumDetectedBpm = 89.0;
constexpr double kMaximumDetectedBpm = 205.0;
constexpr int kTempoGapWindowSize = 2'048;
constexpr int kMinimumSupportedTempoInterval = kTempoGapWindowSize / 2;

[[nodiscard]] inline int minimum_detected_tempo_interval(
    std::uint32_t sample_rate) noexcept {
  return static_cast<int>(static_cast<double>(sample_rate) * 60.0 /
                              kMaximumDetectedBpm +
                          0.5);
}

[[nodiscard]] inline bool supports_detected_tempo_sample_rate(
    std::uint32_t sample_rate) noexcept {
  return minimum_detected_tempo_interval(sample_rate) >=
         kMinimumSupportedTempoInterval;
}

}  // namespace otgy::core::detail
