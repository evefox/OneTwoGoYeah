// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstdint>
#include <optional>

namespace otgy::core {

struct Pcm16ResampleResult final {
  PcmError error;
  std::optional<Pcm16> pcm;
};

/// Resamples canonical stereo PCM16 with linear interpolation so a song can
/// be rendered at the device's native output rate. The duration is preserved;
/// the output frame count is `round(frames * target / source)`.
[[nodiscard]] Pcm16ResampleResult resample_pcm(
    const Pcm16& source, std::uint32_t target_sample_rate) noexcept;

}  // namespace otgy::core
