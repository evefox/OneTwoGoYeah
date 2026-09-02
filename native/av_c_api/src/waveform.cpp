// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

using namespace otgy::c_api;

namespace {

using Generator = otgy::core::WaveformTileResult (*)(
    const otgy::core::Pcm16&, double, double, std::uint64_t) noexcept;

otgy_status generate_waveform(const otgy_pcm* pcm, double ui_zoom,
                              double scale, std::uint64_t block_index,
                              otgy_waveform_tile** out_tile,
                              otgy_error** out_error, Generator generator) {
  if (out_tile != nullptr) {
    *out_tile = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_tile == nullptr) {
      return invalid_argument(out_error);
    }
    auto result = generator(pcm->value, ui_zoom, scale, block_index);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_WAVEFORM,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::waveform_error_message(result.error()));
    }
    return make_handle<otgy_waveform_tile>(
        otgy_waveform_tile{std::move(result).take_tile()}, out_tile,
        out_error);
  });
}

}  // namespace

extern "C" {

void otgy_waveform_tile_destroy(otgy_waveform_tile* tile) { delete tile; }

std::uint64_t otgy_waveform_tile_get_block_index(
    const otgy_waveform_tile* tile) {
  return tile == nullptr ? 0U : tile->value.block_index();
}

std::uint32_t otgy_waveform_tile_get_pixels_per_second(
    const otgy_waveform_tile* tile) {
  return tile == nullptr ? 0U : tile->value.pixels_per_second();
}

std::uint16_t otgy_waveform_tile_get_channel_width(
    const otgy_waveform_tile* tile) {
  return tile == nullptr ? 0U : tile->value.channel_width();
}

std::uint16_t otgy_waveform_tile_get_channel_spacing(
    const otgy_waveform_tile* tile) {
  return tile == nullptr ? 0U : tile->value.channel_spacing();
}

otgy_byte_view otgy_waveform_tile_get_left(const otgy_waveform_tile* tile) {
  if (tile == nullptr) {
    return {nullptr, 0U};
  }
  return {tile->value.channel_alpha(otgy::core::PcmChannel::Left),
          static_cast<std::uint64_t>(tile->value.channel_byte_size())};
}

otgy_byte_view otgy_waveform_tile_get_right(const otgy_waveform_tile* tile) {
  if (tile == nullptr) {
    return {nullptr, 0U};
  }
  return {tile->value.channel_alpha(otgy::core::PcmChannel::Right),
          static_cast<std::uint64_t>(tile->value.channel_byte_size())};
}

#define OTGY_DEFINE_WAVEFORM(c_name, core_name)                               \
  otgy_status otgy_generate_##c_name##_waveform_tile(                         \
      const otgy_pcm* pcm, double ui_zoom, double scale,                      \
      std::uint64_t block_index, otgy_waveform_tile** out_tile,               \
      otgy_error** out_error) {                                               \
    return generate_waveform(                                                 \
        pcm, ui_zoom, scale, block_index, out_tile, out_error,                 \
        otgy::core::core_name);                                               \
  }

OTGY_DEFINE_WAVEFORM(signed_uniform_1x,
                     generate_signed_uniform_waveform_tile_1x)
OTGY_DEFINE_WAVEFORM(rectified_uniform_1x,
                     generate_rectified_uniform_waveform_tile_1x)
OTGY_DEFINE_WAVEFORM(signed_amplitude_1x,
                     generate_signed_amplitude_waveform_tile_1x)
OTGY_DEFINE_WAVEFORM(rectified_amplitude_1x,
                     generate_rectified_amplitude_waveform_tile_1x)
OTGY_DEFINE_WAVEFORM(signed_uniform_2x,
                     generate_signed_uniform_waveform_tile_2x)
OTGY_DEFINE_WAVEFORM(rectified_uniform_2x,
                     generate_rectified_uniform_waveform_tile_2x)
OTGY_DEFINE_WAVEFORM(signed_amplitude_2x,
                     generate_signed_amplitude_waveform_tile_2x)
OTGY_DEFINE_WAVEFORM(rectified_amplitude_2x,
                     generate_rectified_amplitude_waveform_tile_2x)
OTGY_DEFINE_WAVEFORM(signed_uniform_3x,
                     generate_signed_uniform_waveform_tile_3x)
OTGY_DEFINE_WAVEFORM(rectified_uniform_3x,
                     generate_rectified_uniform_waveform_tile_3x)
OTGY_DEFINE_WAVEFORM(signed_amplitude_3x,
                     generate_signed_amplitude_waveform_tile_3x)
OTGY_DEFINE_WAVEFORM(rectified_amplitude_3x,
                     generate_rectified_amplitude_waveform_tile_3x)
OTGY_DEFINE_WAVEFORM(signed_uniform_4x,
                     generate_signed_uniform_waveform_tile_4x)
OTGY_DEFINE_WAVEFORM(rectified_uniform_4x,
                     generate_rectified_uniform_waveform_tile_4x)
OTGY_DEFINE_WAVEFORM(signed_amplitude_4x,
                     generate_signed_amplitude_waveform_tile_4x)
OTGY_DEFINE_WAVEFORM(rectified_amplitude_4x,
                     generate_rectified_amplitude_waveform_tile_4x)

#undef OTGY_DEFINE_WAVEFORM

otgy_status otgy_generate_waveform_tile(
    const otgy_pcm* pcm, double ui_zoom, double scale,
    std::uint64_t block_index, std::uint32_t shape, std::uint32_t luminance,
    std::uint32_t anti_aliasing, std::uint32_t filter, double filter_strength,
    otgy_waveform_tile** out_tile, otgy_error** out_error) {
  if (out_tile != nullptr) {
    *out_tile = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_tile == nullptr) {
      return invalid_argument(out_error);
    }
    const auto core_shape = static_cast<otgy::core::WaveformShape>(shape);
    const auto core_luminance =
        static_cast<otgy::core::WaveformLuminance>(luminance);
    const auto core_sampling =
        static_cast<otgy::core::WaveformSampling>(anti_aliasing);
    const auto core_filter = static_cast<otgy::core::WaveformFilter>(filter);
    const auto configuration_error =
        otgy::core::validate_waveform_configuration(
            ui_zoom, scale, core_shape, core_luminance, core_sampling,
            core_filter, filter_strength);
    if (configuration_error != otgy::core::WaveformError::None) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_WAVEFORM,
          static_cast<std::uint32_t>(configuration_error),
          otgy::core::waveform_error_message(configuration_error));
    }

    const otgy::core::Pcm16* waveform_pcm = &pcm->value;
    if (core_filter != otgy::core::WaveformFilter::None) {
      auto& cache = pcm->waveform_filter_cache;
      if (!cache.has_value() || cache->filter != core_filter ||
          cache->strength != filter_strength) {
        auto filtered = otgy::core::filter_waveform_pcm(pcm->value, core_filter,
                                                        filter_strength);
        if (!filtered) {
          return core_error(
              out_error, OTGY_ERROR_DOMAIN_WAVEFORM,
              static_cast<std::uint32_t>(filtered.error()),
              otgy::core::waveform_error_message(filtered.error()));
        }
        cache.emplace(otgy_waveform_filter_cache{
            core_filter, filter_strength, std::move(filtered).take_pcm()});
      }
      waveform_pcm = &cache->pcm;
    }

    auto result = otgy::core::generate_waveform_tile(
        *waveform_pcm, ui_zoom, scale, block_index, core_shape, core_luminance,
        core_sampling, otgy::core::WaveformFilter::None, filter_strength);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_WAVEFORM,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::waveform_error_message(result.error()));
    }
    return make_handle<otgy_waveform_tile>(
        otgy_waveform_tile{std::move(result).take_tile()}, out_tile, out_error);
  });
}

}  // extern "C"
