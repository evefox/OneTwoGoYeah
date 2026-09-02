// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/waveform/waveform_tile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
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

[[nodiscard]] int divide_by_32768_floor(int value) noexcept {
  if (value >= 0) {
    return value / 32'768;
  }
  return -((-value + 32'767) / 32'768);
}

[[nodiscard]] std::int64_t divide_by_65536_floor(std::int64_t value) noexcept {
  if (value >= 0) {
    return value / 65'536;
  }
  return -((-value + 65'535) / 65'536);
}

[[nodiscard]] std::int64_t multiply_int32_wrap_shift16(int left,
                                                       int right) noexcept {
  // ArrowVortex performs this multiplication in a 32-bit signed int.  Its
  // supported targets wrap on overflow before the arithmetic right shift.
  // Reproduce those bits explicitly so compatibility remains deterministic
  // without relying on signed-overflow or signed-shift implementation details.
  const auto wrapped =
      static_cast<std::uint32_t>(left) * static_cast<std::uint32_t>(right);
  constexpr std::uint64_t kInt32Range = std::uint64_t{1} << 32U;
  const auto signed_value = (wrapped & 0x8000'0000U) == 0U
                                ? static_cast<std::int64_t>(wrapped)
                                : static_cast<std::int64_t>(wrapped) -
                                      static_cast<std::int64_t>(kInt32Range);
  return divide_by_65536_floor(signed_value);
}

// ArrowVortex uses a third-order Butterworth filter from the small `liir`
// implementation bundled with the reference application.  The helpers below
// are the fixed order-three low/high-pass coefficient path used by that code,
// kept allocation-free so filtering remains a deterministic av_core operation.
void butterworth_coefficients(WaveformFilter filter, double frequency,
                              std::array<double, 4>& b,
                              std::array<double, 4>& a) noexcept {
  constexpr double kPi = 3.14159265358;
  constexpr int order = 3;
  std::array<double, 6> roots{};
  const double theta = kPi * frequency;
  const double st = std::sin(theta);
  const double ct = std::cos(theta);
  for (int k = 0; k < order; ++k) {
    const double parg =
        kPi * static_cast<double>(2 * k + 1) / static_cast<double>(2 * order);
    const double sparg = std::sin(parg);
    const double cparg = std::cos(parg);
    const double denominator = 1.0 + st * sparg;
    roots[2 * k] = -ct / denominator;
    roots[2 * k + 1] = -st * cparg / denominator;
  }

  // binomial_mult(3, roots), retaining only the real coefficients.
  std::array<double, 6> polynomial{};
  for (int i = 0; i < order; ++i) {
    for (int j = i; j > 0; --j) {
      polynomial[2 * j] += roots[2 * i] * polynomial[2 * (j - 1)] -
                           roots[2 * i + 1] * polynomial[2 * (j - 1) + 1];
      polynomial[2 * j + 1] += roots[2 * i] * polynomial[2 * (j - 1) + 1] +
                               roots[2 * i + 1] * polynomial[2 * (j - 1)];
    }
    polynomial[0] += roots[2 * i];
    polynomial[1] += roots[2 * i + 1];
  }
  a[0] = 1.0;
  a[1] = polynomial[0];
  a[2] = polynomial[2];
  a[3] = polynomial[4];

  std::array<int, 4> c{1, 3, 3, 1};
  if (filter == WaveformFilter::HighPass) {
    c[1] = -3;
    c[3] = -1;
  }
  double scale = 1.0;
  const double omega = kPi * frequency;
  const double fomega = std::sin(omega);
  const double parg0 = kPi / static_cast<double>(2 * order);
  for (int k = 0; k < order / 2; ++k) {
    scale *= 1.0 + fomega * std::sin(static_cast<double>(2 * k + 1) * parg0);
  }
  const double half = filter == WaveformFilter::HighPass
                          ? std::cos(omega / 2.0)
                          : std::sin(omega / 2.0);
  if (order % 2 != 0) {
    scale *=
        half + (filter == WaveformFilter::HighPass ? std::sin(omega / 2.0)
                                                   : std::cos(omega / 2.0));
  }
  scale = std::pow(half, order) / scale;
  for (int i = 0; i <= order; ++i) {
    b[static_cast<std::size_t>(i)] = static_cast<double>(c[i]) * scale;
  }
}

void filter_channel(const Pcm16::Sample* input, std::size_t frame_count,
                    WaveformFilter filter, double strength,
                    Pcm16::Sample* output) noexcept {
  if (frame_count == 0U) {
    return;
  }
  const double frequency = filter == WaveformFilter::HighPass
                               ? 0.10 + 0.80 * strength
                               : 0.01 + 0.10 * (1.0 - strength);
  std::array<double, 4> b{};
  std::array<double, 4> a{};
  butterworth_coefficients(filter, frequency, b, a);
  double z0 = 0.0;
  double z1 = 0.0;
  double z2 = 0.0;
  Pcm16::Sample max_amplitude = 1;
  for (std::size_t i = 0U; i < frame_count; ++i) {
    const double xi = static_cast<double>(input[i]);
    const double yi = b[0] * xi + z0;
    z0 = b[1] * xi + z1 - a[1] * yi;
    z1 = b[2] * xi + z2 - a[2] * yi;
    z2 = b[3] * xi - a[3] * yi;
    const auto sample = static_cast<Pcm16::Sample>(std::clamp(
        yi, static_cast<double>(std::numeric_limits<Pcm16::Sample>::min()),
        static_cast<double>(std::numeric_limits<Pcm16::Sample>::max())));
    output[i] = sample;
    // ArrowVortex intentionally normalizes against the largest positive
    // sample, rather than the largest absolute magnitude.
    max_amplitude = std::max(max_amplitude, sample);
  }

  constexpr int kPcmMaximum = std::numeric_limits<Pcm16::Sample>::max();
  constexpr int kPcmMinimum = std::numeric_limits<Pcm16::Sample>::min();
  const int scalar = (kPcmMaximum << 16) / static_cast<int>(max_amplitude);
  for (std::size_t i = 0U; i < frame_count; ++i) {
    const auto scaled = multiply_int32_wrap_shift16(output[i], scalar);
    output[i] = static_cast<Pcm16::Sample>(
        std::clamp(scaled, static_cast<std::int64_t>(kPcmMinimum),
                   static_cast<std::int64_t>(kPcmMaximum)));
  }
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

void render_channel(const Pcm16::Sample* input, std::size_t frame_count,
                    std::uint32_t sample_rate, const WaveformGeometry& geometry,
                    std::uint64_t block_index, WaveformShape shape,
                    WaveformLuminance luminance, std::size_t sampling_factor,
                    std::vector<std::uint8_t>& alpha) noexcept {
  const double samples_per_pixel =
      static_cast<double>(sample_rate) /
      static_cast<double>(geometry.pixels_per_second);
  const double samples_per_block =
      static_cast<double>(WaveformTile::kHeight) * samples_per_pixel;
  const double raw_sample_position =
      samples_per_block * static_cast<double>(block_index);

  if (raw_sample_position >= static_cast<double>(frame_count)) {
    return;
  }

  const auto sample_position =
      static_cast<std::size_t>(raw_sample_position);
  if (sample_position >= frame_count) {
    return;
  }

  const std::size_t available_frames = frame_count - sample_position;
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
  input += sample_position;
  const double advance =
      samples_per_pixel * static_cast<double>(WaveformTile::kHeight) /
      static_cast<double>(height);

  std::array<WaveEdge, WaveformTile::kHeight * 4U> edges{};
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

void downsample_3x(const std::vector<std::uint8_t>& source,
                   const WaveformGeometry& geometry,
                   std::vector<std::uint8_t>& destination) noexcept {
  const auto width = static_cast<std::size_t>(geometry.channel_width);
  const auto source_width = width * 3U;
  for (std::size_t y = 0U; y < WaveformTile::kHeight; ++y) {
    const auto source_row = (y * 3U) * source_width;
    const auto destination_row = y * width;
    for (std::size_t x = 0U; x < width; ++x) {
      const auto top_left = source_row + x * 3U;
      const auto middle_left = top_left + source_width;
      const auto bottom_left = middle_left + source_width;
      const int sum =
          source[top_left] + source[top_left + 1U] + source[top_left + 2U] +
          source[middle_left] + source[middle_left + 1U] +
          source[middle_left + 2U] + source[bottom_left] +
          source[bottom_left + 1U] + source[bottom_left + 2U];
      destination[destination_row + x] =
          static_cast<std::uint8_t>(sum / 9);
    }
  }
}

void downsample_4x(const std::vector<std::uint8_t>& source,
                   const WaveformGeometry& geometry,
                   std::vector<std::uint8_t>& destination) noexcept {
  const auto width = static_cast<std::size_t>(geometry.channel_width);
  const auto source_width = width * 4U;
  for (std::size_t y = 0U; y < WaveformTile::kHeight; ++y) {
    const auto source_row = (y * 4U) * source_width;
    const auto destination_row = y * width;
    for (std::size_t x = 0U; x < width; ++x) {
      const auto top_left = source_row + x * 4U;
      const auto second_left = top_left + source_width;
      const auto third_left = second_left + source_width;
      const auto bottom_left = third_left + source_width;
      const int sum =
          source[top_left] + source[top_left + 1U] + source[top_left + 2U] +
          source[top_left + 3U] + source[second_left] +
          source[second_left + 1U] + source[second_left + 2U] +
          source[second_left + 3U] + source[third_left] +
          source[third_left + 1U] + source[third_left + 2U] +
          source[third_left + 3U] + source[bottom_left] +
          source[bottom_left + 1U] + source[bottom_left + 2U] +
          source[bottom_left + 3U];
      destination[destination_row + x] =
          static_cast<std::uint8_t>(sum / 16);
    }
  }
}

}  // namespace

WaveformError validate_waveform_configuration(double ui_zoom, double scale,
                                              WaveformShape shape,
                                              WaveformLuminance luminance,
                                              WaveformSampling sampling,
                                              WaveformFilter filter,
                                              double filter_strength) noexcept {
  if (!std::isfinite(ui_zoom) || ui_zoom < kMinUiZoom || ui_zoom > kMaxUiZoom) {
    return WaveformError::InvalidUiZoom;
  }
  if (!std::isfinite(scale) || scale < kMinScale || scale > kMaxScale) {
    return WaveformError::InvalidScale;
  }
  if (shape > WaveformShape::Rectified ||
      luminance > WaveformLuminance::Amplitude ||
      sampling > WaveformSampling::FourX || filter > WaveformFilter::LowPass) {
    return WaveformError::InvalidOption;
  }
  if (!std::isfinite(filter_strength) || filter_strength < 0.0 ||
      filter_strength > 1.0) {
    return WaveformError::InvalidFilterStrength;
  }
  return WaveformError::None;
}

WaveformPcmResult::WaveformPcmResult(Pcm16 pcm) noexcept
    : storage_(std::move(pcm)) {}

WaveformPcmResult::WaveformPcmResult(WaveformError error) noexcept
    : storage_(error) {}

bool WaveformPcmResult::has_value() const noexcept {
  return std::holds_alternative<Pcm16>(storage_);
}

const Pcm16* WaveformPcmResult::pcm() const noexcept {
  return std::get_if<Pcm16>(&storage_);
}

Pcm16* WaveformPcmResult::pcm() noexcept {
  return std::get_if<Pcm16>(&storage_);
}

WaveformError WaveformPcmResult::error() const noexcept {
  const auto* error = std::get_if<WaveformError>(&storage_);
  return error == nullptr ? WaveformError::None : *error;
}

Pcm16 WaveformPcmResult::take_pcm() && {
  return std::move(std::get<Pcm16>(storage_));
}

WaveformPcmResult filter_waveform_pcm(const Pcm16& pcm, WaveformFilter filter,
                                      double filter_strength) noexcept {
  if (filter > WaveformFilter::LowPass) {
    return WaveformPcmResult{WaveformError::InvalidOption};
  }
  if (!std::isfinite(filter_strength) || filter_strength < 0.0 ||
      filter_strength > 1.0) {
    return WaveformPcmResult{WaveformError::InvalidFilterStrength};
  }

  auto result = Pcm16::allocate(pcm.sample_rate(), pcm.frame_count());
  if (!result) {
    return WaveformPcmResult{WaveformError::AllocationFailure};
  }
  auto output = std::move(result).take_pcm();
  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    if (filter == WaveformFilter::None) {
      if (pcm.frame_count() != 0U) {
        std::copy_n(pcm.channel_data(channel), pcm.frame_count(),
                    output.mutable_channel_data(channel));
      }
    } else {
      filter_channel(pcm.channel_data(channel), pcm.frame_count(), filter,
                     filter_strength, output.mutable_channel_data(channel));
    }
  }
  return WaveformPcmResult{std::move(output)};
}

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
    std::uint64_t block_index, WaveformShape shape,
    WaveformLuminance luminance, WaveformSampling sampling,
    WaveformFilter filter, double filter_strength) noexcept {
  const auto configuration_error = validate_waveform_configuration(
      ui_zoom, scale, shape, luminance, sampling, filter, filter_strength);
  if (configuration_error != WaveformError::None) {
    return WaveformTileResult{configuration_error};
  }

  const auto geometry = calculate_geometry(ui_zoom, scale);
  const auto channel_size =
      static_cast<std::size_t>(geometry.channel_width) *
      WaveformTile::kHeight;
  try {
    std::vector<std::uint8_t> left(channel_size, 0U);
    std::vector<std::uint8_t> right(channel_size, 0U);
    std::optional<Pcm16> filtered_pcm;
    const Pcm16* render_pcm = &pcm;
    if (filter != WaveformFilter::None) {
      auto filtered = filter_waveform_pcm(pcm, filter, filter_strength);
      if (!filtered) {
        return WaveformTileResult{filtered.error()};
      }
      filtered_pcm.emplace(std::move(filtered).take_pcm());
      render_pcm = &*filtered_pcm;
    }
    const auto* left_input = render_pcm->channel_data(PcmChannel::Left);
    const auto* right_input = render_pcm->channel_data(PcmChannel::Right);
    const auto render_frame_count = render_pcm->frame_count();
    if (sampling == WaveformSampling::TwoX) {
      std::vector<std::uint8_t> supersampled(channel_size * 4U, 0U);
      render_channel(left_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 2U,
                     supersampled);
      downsample_2x(supersampled, geometry, left);
      std::fill(supersampled.begin(), supersampled.end(), 0U);
      render_channel(right_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 2U,
                     supersampled);
      downsample_2x(supersampled, geometry, right);
    } else if (sampling == WaveformSampling::ThreeX) {
      std::vector<std::uint8_t> supersampled(channel_size * 9U, 0U);
      render_channel(left_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 3U,
                     supersampled);
      downsample_3x(supersampled, geometry, left);
      std::fill(supersampled.begin(), supersampled.end(), 0U);
      render_channel(right_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 3U,
                     supersampled);
      downsample_3x(supersampled, geometry, right);
    } else if (sampling == WaveformSampling::FourX) {
      std::vector<std::uint8_t> supersampled(channel_size * 16U, 0U);
      render_channel(left_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 4U,
                     supersampled);
      downsample_4x(supersampled, geometry, left);
      std::fill(supersampled.begin(), supersampled.end(), 0U);
      render_channel(right_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 4U,
                     supersampled);
      downsample_4x(supersampled, geometry, right);
    } else {
      render_channel(left_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 1U, left);
      render_channel(right_input, render_frame_count, pcm.sample_rate(),
                     geometry, block_index, shape, luminance, 1U, right);
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

WaveformTileResult generate_waveform_tile(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index, WaveformShape shape,
    WaveformLuminance luminance, WaveformSampling sampling,
    WaveformFilter filter, double filter_strength) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, shape, luminance, sampling, filter,
      filter_strength);
}

WaveformTileResult generate_signed_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Uniform, WaveformSampling::OneX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Uniform, WaveformSampling::OneX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Amplitude, WaveformSampling::OneX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_rectified_amplitude_waveform_tile_1x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Amplitude, WaveformSampling::OneX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_signed_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Uniform, WaveformSampling::TwoX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Uniform, WaveformSampling::TwoX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Amplitude, WaveformSampling::TwoX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_rectified_amplitude_waveform_tile_2x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Amplitude, WaveformSampling::TwoX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_signed_uniform_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Uniform, WaveformSampling::ThreeX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Uniform, WaveformSampling::ThreeX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Amplitude, WaveformSampling::ThreeX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_rectified_amplitude_waveform_tile_3x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Amplitude, WaveformSampling::ThreeX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_signed_uniform_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Uniform, WaveformSampling::FourX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_rectified_uniform_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Uniform, WaveformSampling::FourX, WaveformFilter::None,
      0.75);
}

WaveformTileResult generate_signed_amplitude_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Signed,
      WaveformLuminance::Amplitude, WaveformSampling::FourX,
      WaveformFilter::None, 0.75);
}

WaveformTileResult generate_rectified_amplitude_waveform_tile_4x(
    const Pcm16& pcm, double ui_zoom, double scale,
    std::uint64_t block_index) noexcept {
  return WaveformTile::generate_waveform_tile(
      pcm, ui_zoom, scale, block_index, WaveformShape::Rectified,
      WaveformLuminance::Amplitude, WaveformSampling::FourX,
      WaveformFilter::None, 0.75);
}

}  // namespace otgy::core
