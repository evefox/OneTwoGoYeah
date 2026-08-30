// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/waveform/waveform_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

// Compatibility arithmetic ported from ArrowVortex Waveform.cpp and View.cpp.
constexpr double kMinUiZoom = 1.0;
constexpr double kMaxUiZoom = 19.0;
constexpr double kMinScale = 1.0;
constexpr double kMaxScale = 10.0;

struct WaveEdge final {
  int left;
  int right;
};

struct WaveformGeometry final {
  std::uint32_t pixels_per_second;
  std::uint16_t channel_width;
  std::uint16_t channel_spacing;
};

enum class WaveformShape : std::uint8_t {
  Signed,
  Rectified,
};

enum class WaveformLuminance : std::uint8_t {
  Uniform,
  Amplitude,
};

[[nodiscard]] int divide_by_32768_floor(int value) noexcept {
  if (value >= 0) {
    return value / 32'768;
  }
  return -((-value + 32'767) / 32'768);
}

[[nodiscard]] WaveformGeometry calculate_geometry(double ui_zoom,
                                                   double scale) noexcept {
  const double internal_zoom = ui_zoom - 3.0;
  const double raw_pixels_per_second =
      21.077 * std::pow(1.518, internal_zoom);
  const auto pixels_per_second = static_cast<std::uint32_t>(
      std::round(raw_pixels_per_second));

  const int scale_fixed = static_cast<int>(64.0 * scale);
  const int scaled_channel_width = (256 * scale_fixed) >> 8;
  const int channel_width = std::min(256, scaled_channel_width);
  const int channel_spacing = (24 * scale_fixed) >> 8;
  return {
      pixels_per_second,
      static_cast<std::uint16_t>(channel_width),
      static_cast<std::uint16_t>(channel_spacing),
  };
}

void render_channel(const Pcm16& pcm, PcmChannel channel,
                    const WaveformGeometry& geometry,
                    std::uint64_t block_index, WaveformShape shape,
                    WaveformLuminance luminance, std::size_t sampling_factor,
                    std::vector<std::uint8_t>& alpha) noexcept {
  const double samples_per_pixel =
      static_cast<double>(pcm.sample_rate()) /
      static_cast<double>(geometry.pixels_per_second);
  const double samples_per_block =
      static_cast<double>(WaveformTile::kHeight) * samples_per_pixel;
  const double raw_sample_position =
      samples_per_block * static_cast<double>(block_index);

  if (raw_sample_position >= static_cast<double>(pcm.frame_count())) {
    return;
  }

  const auto sample_position =
      static_cast<std::size_t>(raw_sample_position);
  if (sample_position >= pcm.frame_count()) {
    return;
  }

  const std::size_t available_frames = pcm.frame_count() - sample_position;
  const double sample_count =
      std::min(static_cast<double>(available_frames), samples_per_block);
  if (sample_count <= 0.0) {
    return;
  }

  const double sample_skip = std::max(0.001, samples_per_pixel / 200.0);
  const int width = static_cast<int>(geometry.channel_width) *
                    static_cast<int>(sampling_factor);
  const auto height = WaveformTile::kHeight * sampling_factor;
  const int half_width = width / 2 - 1;
  const auto* input = pcm.channel_data(channel) + sample_position;
  const double advance =
      samples_per_pixel * static_cast<double>(WaveformTile::kHeight) /
      static_cast<double>(height);

  std::array<WaveEdge, WaveformTile::kHeight * 2U> edges{};
  double offset = 0.0;
  for (std::size_t y = 0U; y < height; ++y) {
    const double end = std::min(
        sample_count, static_cast<double>(y + 1U) * advance);
    int minimum = std::numeric_limits<Pcm16::Sample>::max();
    int maximum = std::numeric_limits<Pcm16::Sample>::min();
    while (offset < end) {
      const auto rounded_offset =
          static_cast<std::size_t>(std::round(offset));
      const auto bounded_offset =
          std::min(rounded_offset, available_frames - 1U);
      const int sample = input[bounded_offset];
      minimum = std::min(minimum, sample);
      maximum = std::max(maximum, sample);
      offset += sample_skip;
    }

    if (minimum > maximum) {
      edges[y] = {0, 0};
      continue;
    }

    const int left = divide_by_32768_floor(minimum * half_width);
    const int right = divide_by_32768_floor(maximum * half_width);
    if (right >= left) {
      edges[y] = {
          std::clamp(left, -half_width, half_width),
          std::clamp(right, -half_width, half_width),
      };
    } else {
      edges[y] = {0, 0};
    }
  }

  const int center = width / 2;
  const int amplitude_scalar = ((255 << 16) * 2) / width;
  for (std::size_t y = 0U; y < height; ++y) {
    const int magnitude =
        std::max(std::abs(edges[y].left), std::abs(edges[y].right));
    int left = 0;
    int right = 0;
    if (shape == WaveformShape::Rectified) {
      left = center - magnitude;
      right = center + magnitude;
    } else {
      left = center + std::min(edges[y].left, 0);
      right = center + std::max(edges[y].right, 0);
    }
    const auto alpha_value =
        luminance == WaveformLuminance::Amplitude
            ? static_cast<std::uint8_t>((magnitude * amplitude_scalar) >> 16)
            : static_cast<std::uint8_t>(255U);
    auto row = alpha.begin() + static_cast<std::ptrdiff_t>(
                                   y * static_cast<std::size_t>(width));
    std::fill(row + left, row + right, alpha_value);
  }
}

void downsample_2x(const std::vector<std::uint8_t>& source,
                   const WaveformGeometry& geometry,
                   std::vector<std::uint8_t>& destination) noexcept {
  const auto width = static_cast<std::size_t>(geometry.channel_width);
  const auto source_width = width * 2U;
  for (std::size_t y = 0U; y < WaveformTile::kHeight; ++y) {
    const auto source_row = (y * 2U) * source_width;
    const auto destination_row = y * width;
    for (std::size_t x = 0U; x < width; ++x) {
      const auto top_left = source_row + x * 2U;
      const auto bottom_left = top_left + source_width;
      const int sum = source[top_left] + source[top_left + 1U] +
                      source[bottom_left] + source[bottom_left + 1U];
      destination[destination_row + x] =
          static_cast<std::uint8_t>(sum / 4);
    }
  }
}

}  // namespace

WaveformTile::WaveformTile(std::uint64_t block_index,
                           std::uint32_t pixels_per_second,
                           std::uint16_t channel_width,
                           std::uint16_t channel_spacing,
                           std::vector<std::uint8_t>&& left,
                           std::vector<std::uint8_t>&& right) noexcept
    : block_index_(block_index),
      pixels_per_second_(pixels_per_second),
      channel_width_(channel_width),
      channel_spacing_(channel_spacing),
      channel_byte_size_(left.size()),
      left_(std::move(left)),
      right_(std::move(right)) {}

const std::uint8_t* WaveformTile::channel_alpha(
    PcmChannel channel) const noexcept {
  switch (channel) {
    case PcmChannel::Left:
      return left_.data();
    case PcmChannel::Right:
      return right_.data();
  }
  return nullptr;
}

WaveformTileResult::WaveformTileResult(WaveformTile tile) noexcept
    : storage_(std::move(tile)) {}

WaveformTileResult::WaveformTileResult(WaveformError error) noexcept
    : storage_(error) {}

bool WaveformTileResult::has_value() const noexcept {
  return std::holds_alternative<WaveformTile>(storage_);
}

const WaveformTile* WaveformTileResult::tile() const noexcept {
  return std::get_if<WaveformTile>(&storage_);
}

WaveformTile* WaveformTileResult::tile() noexcept {
  return std::get_if<WaveformTile>(&storage_);
}

WaveformError WaveformTileResult::error() const noexcept {
  const auto* error = std::get_if<WaveformError>(&storage_);
  return error == nullptr ? WaveformError::None : *error;
}

WaveformTile WaveformTileResult::take_tile() && {
  return std::move(std::get<WaveformTile>(storage_));
}

WaveformTileResult WaveformTile::generate_waveform_tile(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index, Variant variant, Sampling sampling) noexcept {
  if (!std::isfinite(ui_zoom) || ui_zoom < kMinUiZoom ||
      ui_zoom > kMaxUiZoom) {
    return WaveformTileResult{WaveformError::InvalidUiZoom};
  }
  if (!std::isfinite(scale) || scale < kMinScale || scale > kMaxScale) {
    return WaveformTileResult{WaveformError::InvalidScale};
  }

  const auto geometry = calculate_geometry(ui_zoom, scale);
  const auto channel_size =
      static_cast<std::size_t>(geometry.channel_width) *
      WaveformTile::kHeight;
  try {
    std::vector<std::uint8_t> left(channel_size, 0U);
    std::vector<std::uint8_t> right(channel_size, 0U);
    const auto shape =
        variant == Variant::RectifiedUniform ||
                variant == Variant::RectifiedAmplitude
            ? WaveformShape::Rectified
            : WaveformShape::Signed;
    const auto luminance =
        variant == Variant::SignedAmplitude ||
                variant == Variant::RectifiedAmplitude
            ? WaveformLuminance::Amplitude
            : WaveformLuminance::Uniform;
    if (sampling == Sampling::TwoX) {
      std::vector<std::uint8_t> supersampled(channel_size * 4U, 0U);
      render_channel(pcm, PcmChannel::Left, geometry, block_index, shape,
                     luminance, 2U, supersampled);
      downsample_2x(supersampled, geometry, left);
      std::fill(supersampled.begin(), supersampled.end(), 0U);
      render_channel(pcm, PcmChannel::Right, geometry, block_index, shape,
                     luminance, 2U, supersampled);
      downsample_2x(supersampled, geometry, right);
    } else {
      render_channel(pcm, PcmChannel::Left, geometry, block_index, shape,
                     luminance, 1U, left);
      render_channel(pcm, PcmChannel::Right, geometry, block_index, shape,
                     luminance, 1U, right);
    }
    return WaveformTileResult{WaveformTile{
        block_index,
        geometry.pixels_per_second,
        geometry.channel_width,
        geometry.channel_spacing,
        std::move(left),
        std::move(right),
    }};
  } catch (const std::bad_alloc&) {
    return WaveformTileResult{WaveformError::AllocationFailure};
  }
}

WaveformTileResult generate_signed_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformTile::Variant::SignedUniform,
      WaveformTile::Sampling::OneX);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index,
      WaveformTile::Variant::RectifiedUniform,
      WaveformTile::Sampling::OneX);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index,
      WaveformTile::Variant::SignedAmplitude,
      WaveformTile::Sampling::OneX);
}

WaveformTileResult generate_rectified_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index,
      WaveformTile::Variant::RectifiedAmplitude,
      WaveformTile::Sampling::OneX);
}

WaveformTileResult generate_signed_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformTile::Variant::SignedUniform,
      WaveformTile::Sampling::TwoX);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index,
      WaveformTile::Variant::RectifiedUniform,
      WaveformTile::Sampling::TwoX);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index,
      WaveformTile::Variant::SignedAmplitude,
      WaveformTile::Sampling::TwoX);
}

}  // namespace otgy::core
