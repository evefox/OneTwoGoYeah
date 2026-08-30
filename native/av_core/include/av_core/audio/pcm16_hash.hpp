// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace otgy::core {

/// Stable identifier for the byte projection hashed by pcm16le_sha256_hex.
inline constexpr std::string_view kPlanarPcm16LeFormat =
    "pcm16le-planar-left-right";

/// Lowercase SHA-256 of canonical planar PCM16LE bytes.
///
/// Samples are hashed as all Left frames followed by all Right frames. Each
/// signed sample is projected to exactly two little-endian bytes, independent
/// of the host byte order. Sample rate and frame count are reported separately
/// by callers and are not part of the digest input.
[[nodiscard]] std::array<char, 64> pcm16le_sha256_hex(
    const Pcm16& pcm) noexcept;

}  // namespace otgy::core
