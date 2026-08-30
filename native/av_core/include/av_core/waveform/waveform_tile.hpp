// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

/// Stable failures returned by the first ArrowVortex waveform slice.
enum class WaveformError : std::uint8_t {
  None = 0,
  InvalidUiZoom = 1,
  InvalidScale = 2,
  AllocationFailure = 3,
};

[[nodiscard]] constexpr std::string_view waveform_error_message(
    WaveformError error) noexcept {
  switch (error) {
    case WaveformError::None:
      return {};
    case WaveformError::InvalidUiZoom:
      return "waveform UI Zoom must be finite and within 1..19";
    case WaveformError::InvalidScale:
      return "waveform Scale must be finite and within 1..10";
    case WaveformError::AllocationFailure:
      return "unable to allocate waveform Alpha samples";
  }
  return "unknown waveform error";
}

class WaveformTileResult;

/// One 128-row, 1x Alpha tile.
///
/// Left and Right are independently owned row-major Alpha planes. Each plane
/// has channel_width() columns and kHeight rows. The Alpha values are the
/// ArrowVortex-compatible 1x result before UI colorization.
class WaveformTile final {
 public:
  static constexpr std::size_t kHeight = 128U;

  WaveformTile(const WaveformTile&) = default;
  WaveformTile& operator=(const WaveformTile&) = default;
  WaveformTile(WaveformTile&&) noexcept = default;
  WaveformTile& operator=(WaveformTile&&) noexcept = default;
  ~WaveformTile() = default;

  [[nodiscard]] std::uint64_t block_index() const noexcept {
    return block_index_;
  }

  [[nodiscard]] std::uint32_t pixels_per_second() const noexcept {
    return pixels_per_second_;
  }

  [[nodiscard]] std::uint16_t channel_width() const noexcept {
    return channel_width_;
  }

  [[nodiscard]] std::uint16_t channel_spacing() const noexcept {
    return channel_spacing_;
  }

  [[nodiscard]] std::size_t channel_byte_size() const noexcept {
    return channel_byte_size_;
  }

  [[nodiscard]] std::size_t total_byte_size() const noexcept {
    return channel_byte_size_ * Pcm16::kChannelCount;
  }

  [[nodiscard]] const std::uint8_t* channel_alpha(
      PcmChannel channel) const noexcept;

 private:
  friend WaveformTileResult generate_signed_uniform_waveform_tile_1x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_uniform_waveform_tile_1x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_amplitude_waveform_tile_1x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_amplitude_waveform_tile_1x(
      const Pcm16&, double, double, std::uint64_t) noexcept;

  enum class Variant : std::uint8_t {
    SignedUniform,
    RectifiedUniform,
    SignedAmplitude,
    RectifiedAmplitude,
  };

  [[nodiscard]] static WaveformTileResult generate_waveform_tile_1x(
      const Pcm16& pcm, double ui_zoom, double scale,
      std::uint64_t block_index, Variant variant) noexcept;

  WaveformTile(std::uint64_t block_index, std::uint32_t pixels_per_second,
               std::uint16_t channel_width, std::uint16_t channel_spacing,
               std::vector<std::uint8_t>&& left,
               std::vector<std::uint8_t>&& right) noexcept;

  std::uint64_t block_index_;
  std::uint32_t pixels_per_second_;
  std::uint16_t channel_width_;
  std::uint16_t channel_spacing_;
  std::size_t channel_byte_size_;
  std::vector<std::uint8_t> left_;
  std::vector<std::uint8_t> right_;
};

class WaveformTileResult final {
 public:
  explicit WaveformTileResult(WaveformTile tile) noexcept;
  explicit WaveformTileResult(WaveformError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const WaveformTile* tile() const noexcept;
  [[nodiscard]] WaveformTile* tile() noexcept;
  [[nodiscard]] WaveformError error() const noexcept;

  [[nodiscard]] WaveformTile take_tile() &&;

 private:
  std::variant<WaveformTile, WaveformError> storage_;
};

/// Generates the signed ArrowVortex-compatible waveform slice.
///
/// ui_zoom uses the public 1..19 scale and maps to ArrowVortex's internal
/// -2..16 Zoom. scale uses 1..10. The output is fixed to signed shape,
/// uniform luminance and 1x sampling.
[[nodiscard]] WaveformTileResult generate_signed_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified ArrowVortex-compatible waveform slice.
///
/// Geometry, sampling edges and parameter ranges are identical to the signed
/// uniform entry point. Only the edge projection is rectified around the
/// channel center. Amplitude luminance and 2x/3x/4x downsampling remain
/// outside this entry point.
[[nodiscard]] WaveformTileResult generate_rectified_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, amplitude-luminance ArrowVortex waveform slice.
///
/// Geometry, sampling edges, signed projection and parameter ranges are
/// identical to the signed uniform entry point. Only each row's Alpha changes
/// to ArrowVortex's integer amplitude luminance. Rectified projection and
/// 2x/3x/4x downsampling remain outside this entry point.
[[nodiscard]] WaveformTileResult generate_signed_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, amplitude-luminance ArrowVortex waveform slice.
///
/// Geometry, sampling edges and parameter ranges are identical to the other
/// 1x entry points. Each row uses the rectified projection and ArrowVortex's
/// integer amplitude luminance. 2x/3x/4x downsampling remains outside this
/// entry point.
[[nodiscard]] WaveformTileResult
generate_rectified_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

}  // namespace otgy::core
