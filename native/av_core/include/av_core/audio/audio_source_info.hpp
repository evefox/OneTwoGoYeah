// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace otgy::core {

enum class AudioSourceCodec : std::uint8_t {
  Mp3 = 1,
  OggVorbis = 2,
};

/// Source properties retained beside canonical PCM for export policy.
/// A zero bitrate means that the corresponding value is unavailable.
struct AudioSourceInfo final {
  AudioSourceCodec codec;
  std::uint64_t measured_bitrate_bps;
  std::uint64_t nominal_bitrate_bps;
};

}  // namespace otgy::core
