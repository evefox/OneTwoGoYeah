// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace otgy::core::detail {

constexpr double kMinimumDetectedBpm = 89.0;
constexpr double kMaximumDetectedBpm = 205.0;
constexpr int kTempoGapWindowSize = 2'048;
constexpr int kMinimumSupportedTempoInterval = kTempoGapWindowSize / 2;

}  // namespace otgy::core::detail
