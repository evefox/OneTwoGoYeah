// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

#include <array>

using namespace otgy::c_api;

extern "C" {

std::uint32_t otgy_c_api_version(void) { return OTGY_C_API_VERSION; }

otgy_string_view otgy_core_version(void) {
  const auto value = otgy::core::version_string();
  return {value.data(), static_cast<std::uint64_t>(value.size())};
}

void otgy_error_destroy(otgy_error* error) { delete error; }

otgy_error_domain otgy_error_get_domain(const otgy_error* error) {
  return error == nullptr ? 0U : error->domain;
}

std::uint32_t otgy_error_get_code(const otgy_error* error) {
  return error == nullptr ? 0U : error->code;
}

otgy_string_view otgy_error_get_message(const otgy_error* error) {
  if (error == nullptr) {
    return {nullptr, 0U};
  }
  return {error->message.data(),
          static_cast<std::uint64_t>(error->message.size())};
}

void otgy_bytes_destroy(otgy_bytes* bytes) { delete bytes; }

otgy_byte_view otgy_bytes_get_view(const otgy_bytes* bytes) {
  if (bytes == nullptr) {
    return {nullptr, 0U};
  }
  return {bytes->value.empty() ? nullptr : bytes->value.data(),
          static_cast<std::uint64_t>(bytes->value.size())};
}

otgy_status otgy_pcm_copy_from_channels(
    std::uint32_t sample_rate, const std::int16_t* left,
    std::uint64_t left_frame_count, const std::int16_t* right,
    std::uint64_t right_frame_count, otgy_pcm** out_pcm,
    otgy_error** out_error) {
  if (out_pcm != nullptr) {
    *out_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    std::size_t left_count = 0U;
    std::size_t right_count = 0U;
    if (!to_size(left_frame_count, left_count) ||
        !to_size(right_frame_count, right_count)) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE,
                              kLengthOutOfRange);
    }
    auto result = otgy::core::Pcm16::copy_from_channels(
        sample_rate, left, left_count, right, right_count);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_PCM,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::pcm_error_message(result.error()));
    }
    return make_handle<otgy_pcm>(
        otgy_pcm{std::move(result).take_pcm(), false,
                 {otgy::core::AudioSourceCodec::Mp3, 0U, 0U}},
        out_pcm, out_error);
  });
}

void otgy_pcm_destroy(otgy_pcm* pcm) { delete pcm; }

std::uint32_t otgy_pcm_get_sample_rate(const otgy_pcm* pcm) {
  return pcm == nullptr ? 0U : pcm->value.sample_rate();
}

std::uint64_t otgy_pcm_get_frame_count(const otgy_pcm* pcm) {
  return pcm == nullptr ? 0U
                        : static_cast<std::uint64_t>(pcm->value.frame_count());
}

const std::int16_t* otgy_pcm_get_left(const otgy_pcm* pcm) {
  return pcm == nullptr
             ? nullptr
             : pcm->value.channel_data(otgy::core::PcmChannel::Left);
}

const std::int16_t* otgy_pcm_get_right(const otgy_pcm* pcm) {
  return pcm == nullptr
             ? nullptr
             : pcm->value.channel_data(otgy::core::PcmChannel::Right);
}

std::uint8_t otgy_pcm_get_source_info(
    const otgy_pcm* pcm, otgy_audio_source_info* out_source_info) {
  if (pcm == nullptr || out_source_info == nullptr || !pcm->has_source_info) {
    return 0U;
  }
  *out_source_info = from_core(pcm->source_info);
  return 1U;
}

otgy_status otgy_pcm_sha256(const otgy_pcm* pcm, otgy_bytes** out_hash,
                            otgy_error** out_error) {
  if (out_hash != nullptr) {
    *out_hash = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_hash == nullptr) {
      return invalid_argument(out_error);
    }
    const auto hash = otgy::core::pcm16le_sha256_hex(pcm->value);
    std::vector<std::uint8_t> bytes(hash.begin(), hash.end());
    return make_bytes(std::move(bytes), out_hash, out_error);
  });
}

}  // extern "C"
