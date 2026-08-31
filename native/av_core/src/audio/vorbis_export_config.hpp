// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace otgy::core::detail {

inline constexpr std::uint32_t kVorbisExportMinimumSampleRate = 8'000U;
inline constexpr std::uint32_t kVorbisExportMaximumSampleRate = 200'000U;

[[nodiscard]] constexpr bool valid_vorbis_export_sample_rate(
    std::uint32_t sample_rate) noexcept {
  return sample_rate >= kVorbisExportMinimumSampleRate &&
         sample_rate <= kVorbisExportMaximumSampleRate;
}

[[nodiscard]] constexpr float vorbis_export_quality_value(
    std::uint8_t quality) noexcept {
  return static_cast<float>(quality) / 10.0F;
}

}  // namespace otgy::core::detail
