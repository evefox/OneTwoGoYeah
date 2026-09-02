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
  InvalidOption = 4,
  InvalidFilterStrength = 5,
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
    case WaveformError::InvalidOption:
      return "waveform option is invalid";
    case WaveformError::InvalidFilterStrength:
      return "waveform filter strength must be finite and within 0..1";
  }
  return "unknown waveform error";
}

class WaveformTileResult;
class WaveformPcmResult;

/// Display options used by the configurable waveform entry point. The integer
/// values are part of the C ABI contract.
enum class WaveformShape : std::uint32_t {
  Signed = 0,
  Rectified = 1,
};

enum class WaveformLuminance : std::uint32_t {
  Uniform = 0,
  Amplitude = 1,
};

enum class WaveformSampling : std::uint32_t {
  OneX = 0,
  TwoX = 1,
  ThreeX = 2,
  FourX = 3,
};

enum class WaveformFilter : std::uint32_t {
  None = 0,
  HighPass = 1,
  LowPass = 2,
};

/// Validates every option used by the configurable waveform entry point.
/// Keeping this query in av_core lets adapters prepare and cache filtered PCM
/// without reproducing option validation or its error ordering.
[[nodiscard]] WaveformError validate_waveform_configuration(
    double ui_zoom, double scale, WaveformShape shape,
    WaveformLuminance luminance, WaveformSampling sampling,
    WaveformFilter filter, double filter_strength) noexcept;

/// Applies ArrowVortex's complete-channel waveform filter to canonical PCM.
///
/// The filter state starts at frame zero and each channel is normalized only
/// after the complete channel has been processed, matching the reference
/// WaveFilter path. `None` returns an independent PCM copy.
[[nodiscard]] WaveformPcmResult
filter_waveform_pcm(const Pcm16& pcm, WaveformFilter filter,
                    double filter_strength) noexcept;

class WaveformPcmResult final {
 public:
  explicit WaveformPcmResult(Pcm16 pcm) noexcept;
  explicit WaveformPcmResult(WaveformError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const Pcm16* pcm() const noexcept;
  [[nodiscard]] Pcm16* pcm() noexcept;
  [[nodiscard]] WaveformError error() const noexcept;
  [[nodiscard]] Pcm16 take_pcm() &&;

 private:
  std::variant<Pcm16, WaveformError> storage_;
};

/// One 128-row Alpha tile.
///
/// Left and Right are independently owned row-major Alpha planes. Each plane
/// has channel_width() columns and kHeight rows. The Alpha values are the
/// ArrowVortex-compatible result before UI colorization.
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
  friend WaveformTileResult generate_signed_uniform_waveform_tile_2x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_uniform_waveform_tile_2x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_amplitude_waveform_tile_2x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_amplitude_waveform_tile_2x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_uniform_waveform_tile_3x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_uniform_waveform_tile_3x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_amplitude_waveform_tile_3x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_amplitude_waveform_tile_3x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_uniform_waveform_tile_4x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_uniform_waveform_tile_4x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_signed_amplitude_waveform_tile_4x(
      const Pcm16&, double, double, std::uint64_t) noexcept;
  friend WaveformTileResult generate_rectified_amplitude_waveform_tile_4x(
      const Pcm16&, double, double, std::uint64_t) noexcept;

  friend WaveformTileResult
  generate_waveform_tile(const Pcm16&, double, double, std::uint64_t,
                         WaveformShape, WaveformLuminance, WaveformSampling,
                         WaveformFilter, double) noexcept;

  [[nodiscard]] static WaveformTileResult generate_waveform_tile(
      const Pcm16& pcm, double ui_zoom, double scale,
      std::uint64_t block_index, WaveformShape shape,
      WaveformLuminance luminance, WaveformSampling sampling,
      WaveformFilter filter, double filter_strength) noexcept;

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

/// Generates a waveform tile using the complete ArrowVortex display option
/// set. `filter_strength` is interpreted as 0..1 and is ignored visually when
/// `filter` is None, but is still validated for deterministic diagnostics.
[[nodiscard]] WaveformTileResult
generate_waveform_tile(const Pcm16& pcm, double ui_zoom, double scale,
                       std::uint64_t block_index, WaveformShape shape,
                       WaveformLuminance luminance, WaveformSampling sampling,
                       WaveformFilter filter, double filter_strength) noexcept;

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

/// Generates the signed, uniform-luminance ArrowVortex 2x waveform slice.
///
/// Geometry and parameter ranges are identical to the signed uniform 1x entry
/// point. Sampling and signed projection use twice the final width and height,
/// then each 2x2 Alpha block is reduced with ArrowVortex's integer average.
/// Amplitude luminance and 3x/4x remain outside this entry point.
[[nodiscard]] WaveformTileResult generate_signed_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, uniform-luminance ArrowVortex 2x waveform slice.
///
/// Geometry, parameter ranges, 2x sampling and integer downsampling are
/// identical to the signed uniform 2x entry point. Only the edge projection is
/// rectified around the channel center. Amplitude luminance and 3x/4x remain
/// outside this entry point.
[[nodiscard]] WaveformTileResult generate_rectified_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, amplitude-luminance ArrowVortex 2x waveform slice.
///
/// Geometry, parameter ranges, 2x sampling and integer downsampling are
/// identical to the signed uniform 2x entry point. Only each supersampled
/// row's Alpha uses ArrowVortex's integer amplitude luminance. Rectified
/// amplitude luminance and 3x/4x remain outside this entry point.
[[nodiscard]] WaveformTileResult generate_signed_amplitude_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, amplitude-luminance ArrowVortex 2x waveform slice.
///
/// Geometry, parameter ranges, 2x sampling, amplitude luminance and integer
/// downsampling are identical to the signed amplitude 2x entry point. Only the
/// edge projection is rectified around the channel center. 3x/4x remain
/// outside this entry point.
[[nodiscard]] WaveformTileResult
generate_rectified_amplitude_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, uniform-luminance ArrowVortex 3x waveform slice.
///
/// Geometry and parameter ranges are identical to the signed uniform 1x entry
/// point. Sampling and signed projection use three times the final width and
/// height, then each 3x3 Alpha block is reduced with ArrowVortex's integer
/// average. Other shape/luminance combinations and 4x remain outside this
/// entry point.
[[nodiscard]] WaveformTileResult generate_signed_uniform_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, uniform-luminance ArrowVortex 3x waveform slice.
///
/// Geometry, parameter ranges, 3x sampling and integer downsampling are
/// identical to the signed uniform 3x entry point. Only the edge projection is
/// rectified around the channel center. Amplitude luminance and 4x remain
/// outside this entry point.
[[nodiscard]] WaveformTileResult generate_rectified_uniform_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, amplitude-luminance ArrowVortex 3x waveform slice.
///
/// Geometry, parameter ranges, 3x sampling and integer downsampling are
/// identical to the signed uniform 3x entry point. Only each supersampled
/// row's Alpha uses ArrowVortex's integer amplitude luminance. Rectified
/// amplitude luminance and 4x remain outside this entry point.
[[nodiscard]] WaveformTileResult generate_signed_amplitude_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, amplitude-luminance ArrowVortex 3x waveform slice.
///
/// Geometry, parameter ranges, 3x sampling, amplitude luminance and integer
/// downsampling are identical to the signed amplitude 3x entry point. Only the
/// edge projection is rectified around the channel center. 4x remains outside
/// this entry point.
[[nodiscard]] WaveformTileResult
generate_rectified_amplitude_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, uniform-luminance ArrowVortex 4x waveform slice.
///
/// Geometry and parameter ranges are identical to the signed uniform 1x entry
/// point. Sampling and signed projection use four times the final width and
/// height, then each 4x4 Alpha block is reduced with ArrowVortex's integer
/// average. Other 4x shape/luminance combinations remain outside this entry
/// point.
[[nodiscard]] WaveformTileResult generate_signed_uniform_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, uniform-luminance ArrowVortex 4x waveform slice.
///
/// Geometry, parameter ranges, 4x sampling and integer downsampling are
/// identical to the signed uniform 4x entry point. Only the edge projection is
/// rectified around the channel center. Amplitude luminance remains outside
/// this entry point.
[[nodiscard]] WaveformTileResult generate_rectified_uniform_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the signed, amplitude-luminance ArrowVortex 4x waveform slice.
///
/// Geometry, parameter ranges, 4x sampling and integer downsampling are
/// identical to the signed uniform 4x entry point. Only each supersampled
/// row's Alpha uses ArrowVortex's integer amplitude luminance. Rectified
/// amplitude luminance remains outside this entry point.
[[nodiscard]] WaveformTileResult generate_signed_amplitude_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

/// Generates the rectified, amplitude-luminance ArrowVortex 4x waveform slice.
///
/// Geometry, parameter ranges, 4x sampling, amplitude luminance and integer
/// downsampling are identical to the signed amplitude 4x entry point. Only the
/// edge projection is rectified around the channel center.
[[nodiscard]] WaveformTileResult
generate_rectified_amplitude_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept;

}  // namespace otgy::core
