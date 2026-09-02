// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

#include <array>

using namespace otgy::c_api;

namespace {

template <typename Result, typename Decode, typename Message>
otgy_status decode_audio(otgy_byte_view input, otgy_pcm** out_pcm,
                         otgy_error** out_error,
                         otgy_error_domain error_domain, Decode&& decode,
                         Message&& message) {
  if (out_pcm != nullptr) {
    *out_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    std::size_t size = 0U;
    if (!valid_bytes(input, size)) {
      const bool length_error =
          input.length > static_cast<std::uint64_t>(
                             std::numeric_limits<std::size_t>::max());
      return invalid_argument(
          out_error,
          length_error ? OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE
                       : OTGY_ABI_ERROR_NULL_ARGUMENT,
          length_error ? kLengthOutOfRange : kNullArgument);
    }
    Result result = decode(input.data, size);
    if (!result) {
      return core_error(out_error, error_domain,
                        static_cast<std::uint32_t>(result.error()),
                        message(result.error()));
    }
    const auto source_info = *result.source_info();
    return make_handle<otgy_pcm>(
        otgy_pcm{std::move(result).take_pcm(), true, source_info, {}}, out_pcm,
        out_error);
  });
}

otgy::core::AudioExportQualityPolicy to_core(
    const otgy_audio_export_quality_policy& value) noexcept {
  otgy::core::AudioExportQualityPolicy result{};
  for (std::size_t index = 0U; index < result.nominal_bitrates_bps.size();
       ++index) {
    result.nominal_bitrates_bps[index] = value.nominal_bitrates_bps[index];
  }
  result.source_bitrate_bps = value.source_bitrate_bps;
  result.quality_ceiling = value.quality_ceiling;
  result.default_quality = value.default_quality;
  return result;
}

struct AudioExportCallbackBridge final {
  otgy_audio_export_progress_callback progress;
  otgy_cancel_callback cancel;
  void* context;
};

void report_audio_export_progress(otgy::core::AudioExportProgress progress,
                                  void* context) noexcept {
  auto* bridge = static_cast<AudioExportCallbackBridge*>(context);
  if (bridge->progress != nullptr) {
    bridge->progress(static_cast<std::uint32_t>(progress.stage),
                     static_cast<std::uint64_t>(progress.completed_units),
                     static_cast<std::uint64_t>(progress.total_units),
                     bridge->context);
  }
}

bool audio_export_cancelled(void* context) noexcept {
  auto* bridge = static_cast<AudioExportCallbackBridge*>(context);
  return bridge->cancel != nullptr && bridge->cancel(bridge->context) != 0U;
}

}  // namespace

extern "C" {

otgy_status otgy_decode_mp3(otgy_byte_view input, otgy_pcm** out_pcm,
                            otgy_error** out_error) {
  return decode_audio<otgy::core::Mp3DecodeResult>(
      input, out_pcm, out_error, OTGY_ERROR_DOMAIN_MP3_DECODE,
      otgy::core::decode_mp3, otgy::core::mp3_decode_error_message);
}

otgy_status otgy_decode_ogg_vorbis(otgy_byte_view input, otgy_pcm** out_pcm,
                                   otgy_error** out_error) {
  return decode_audio<otgy::core::OggVorbisDecodeResult>(
      input, out_pcm, out_error, OTGY_ERROR_DOMAIN_OGG_VORBIS_DECODE,
      otgy::core::decode_ogg_vorbis,
      otgy::core::ogg_vorbis_decode_error_message);
}

otgy_status otgy_calculate_pcm_offset_plan(
    std::uint32_t sample_rate, std::uint64_t frame_count,
    double offset_seconds, otgy_pcm_offset_plan* out_plan,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_plan == nullptr) {
      return invalid_argument(out_error);
    }
    std::size_t native_frame_count = 0U;
    if (!to_size(frame_count, native_frame_count)) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE,
                              kLengthOutOfRange);
    }
    const auto result = otgy::core::calculate_pcm_offset_plan(
        sample_rate, native_frame_count, offset_seconds);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_PCM_OFFSET,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::pcm_offset_error_message(result.error()));
    }
    out_plan->signed_shift_frames = result.plan()->signed_shift_frames;
    out_plan->output_frame_count =
        static_cast<std::uint64_t>(result.plan()->output_frame_count);
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_apply_pcm_offset(const otgy_pcm* pcm, double offset_seconds,
                                  otgy_pcm** out_pcm,
                                  otgy_error** out_error) {
  if (out_pcm != nullptr) {
    *out_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::apply_pcm_offset(pcm->value, offset_seconds);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_PCM_OFFSET,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::pcm_offset_error_message(result.error()));
    }
    return make_handle<otgy_pcm>(
        otgy_pcm{std::move(result).take_pcm(), pcm->has_source_info,
                 pcm->source_info, {}},
        out_pcm, out_error);
  });
}

otgy_status otgy_normalize_audio_export_base_name(
    otgy_string_view input, otgy_bytes** out_utf8, otgy_error** out_error) {
  if (out_utf8 != nullptr) {
    *out_utf8 = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::string_view value;
    if (out_utf8 == nullptr || !valid_string(input, value)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::normalize_audio_export_base_name(value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_NAME,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_name_error_message(result.error()));
    }
    return make_utf8(std::move(result).take_name(), out_utf8, out_error);
  });
}

otgy_status otgy_default_audio_export_base_name(
    otgy_string_view source_file_name, otgy_bytes** out_utf8,
    otgy_error** out_error) {
  if (out_utf8 != nullptr) {
    *out_utf8 = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::string_view value;
    if (out_utf8 == nullptr || !valid_string(source_file_name, value)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::default_audio_export_base_name(value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_NAME,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_name_error_message(result.error()));
    }
    return make_utf8(std::move(result).take_name(), out_utf8, out_error);
  });
}

otgy_status otgy_make_audio_export_file_name(
    otgy_string_view base_name, otgy_audio_export_local_time local_time,
    std::uint32_t collision_index, otgy_bytes** out_utf8,
    otgy_error** out_error) {
  if (out_utf8 != nullptr) {
    *out_utf8 = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::string_view value;
    if (out_utf8 == nullptr || !valid_string(base_name, value)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::make_audio_export_file_name(
        value,
        {local_time.year, local_time.month, local_time.day, local_time.hour,
         local_time.minute, local_time.second},
        collision_index);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_NAME,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_name_error_message(result.error()));
    }
    return make_utf8(std::move(result).take_name(), out_utf8, out_error);
  });
}

otgy_status otgy_calculate_audio_export_quality(
    std::uint32_t sample_rate, otgy_audio_source_info source_info,
    otgy_audio_export_quality_policy* out_policy, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_policy == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::calculate_audio_export_quality(
        sample_rate, otgy::c_api::to_core(source_info));
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_QUALITY,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_quality_error_message(result.error()));
    }
    for (std::size_t index = 0U; index < 5U; ++index) {
      out_policy->nominal_bitrates_bps[index] =
          result.policy()->nominal_bitrates_bps[index];
    }
    out_policy->source_bitrate_bps = result.policy()->source_bitrate_bps;
    out_policy->quality_ceiling = result.policy()->quality_ceiling;
    out_policy->default_quality = result.policy()->default_quality;
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_validate_audio_export_quality(
    const otgy_audio_export_quality_policy* policy, std::uint8_t quality,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (policy == nullptr) {
      return invalid_argument(out_error);
    }
    const auto error =
        otgy::core::validate_audio_export_quality(to_core(*policy), quality);
    if (error != otgy::core::AudioExportQualityError::None) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_QUALITY,
          static_cast<std::uint32_t>(error),
          otgy::core::audio_export_quality_error_message(error));
    }
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_audio_export_snapshot_create(
    const otgy_pcm* pcm, std::uint64_t pcm_identity, double offset_seconds,
    std::uint8_t quality, otgy_audio_source_info source_info,
    otgy_string_view requested_base_name, otgy_audio_export_metadata metadata,
    otgy_audio_export_snapshot** out_snapshot, otgy_error** out_error) {
  if (out_snapshot != nullptr) {
    *out_snapshot = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::string_view base_name;
    std::string_view title;
    std::string_view artist;
    std::string_view album;
    if (pcm == nullptr || out_snapshot == nullptr ||
        !valid_string(requested_base_name, base_name) ||
        !valid_string(metadata.title, title) ||
        !valid_string(metadata.artist, artist) ||
        !valid_string(metadata.album, album)) {
      return invalid_argument(out_error);
    }
    const otgy::core::OggVorbisMetadata core_metadata{
        std::string(title), std::string(artist), std::string(album)};
    auto result = otgy::core::create_audio_export_snapshot(
        pcm->value, pcm_identity, offset_seconds, quality,
        otgy::c_api::to_core(source_info), base_name, core_metadata);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_SNAPSHOT,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_snapshot_error_message(result.error()));
    }
    return make_handle<otgy_audio_export_snapshot>(
        otgy_audio_export_snapshot{std::move(result).take_snapshot()},
        out_snapshot, out_error);
  });
}

otgy_status otgy_audio_export_cancel_flag_create(
    otgy_audio_export_cancel_flag** out_flag, otgy_error** out_error) {
  if (out_flag != nullptr) {
    *out_flag = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_flag == nullptr) {
      return invalid_argument(out_error);
    }
    auto* flag = new (std::nothrow) otgy_audio_export_cancel_flag{};
    if (flag == nullptr) {
      return out_of_memory(out_error);
    }
    *out_flag = flag;
    return OTGY_STATUS_OK;
  });
}

void otgy_audio_export_cancel_flag_destroy(
    otgy_audio_export_cancel_flag* flag) {
  delete flag;
}

void otgy_audio_export_cancel_flag_request(
    otgy_audio_export_cancel_flag* flag) {
  if (flag != nullptr) {
    flag->requested.store(true, std::memory_order_release);
  }
}

std::uint8_t otgy_audio_export_cancel_flag_is_requested(
    const otgy_audio_export_cancel_flag* flag) {
  return flag != nullptr && flag->requested.load(std::memory_order_acquire)
             ? 1U
             : 0U;
}

void otgy_audio_export_snapshot_destroy(otgy_audio_export_snapshot* snapshot) {
  delete snapshot;
}

otgy_status otgy_audio_export_snapshot_encode(
    const otgy_audio_export_snapshot* snapshot,
    otgy_audio_export_progress_callback progress_callback,
    otgy_cancel_callback cancel_callback, void* callback_context,
    otgy_audio_export_output** out_output, otgy_error** out_error) {
  if (out_output != nullptr) {
    *out_output = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (snapshot == nullptr || out_output == nullptr) {
      return invalid_argument(out_error);
    }
    AudioExportCallbackBridge bridge{progress_callback, cancel_callback,
                                     callback_context};
    auto result = otgy::core::encode_audio_export_snapshot(
        snapshot->value,
        progress_callback == nullptr ? nullptr : report_audio_export_progress,
        cancel_callback == nullptr ? nullptr : audio_export_cancelled, &bridge);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_AUDIO_EXPORT_TASK,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::audio_export_task_error_message(result.error()));
    }
    return make_handle<otgy_audio_export_output>(
        otgy_audio_export_output{std::move(result).take_output()}, out_output,
        out_error);
  });
}

void otgy_audio_export_output_destroy(otgy_audio_export_output* output) {
  delete output;
}

otgy_byte_view otgy_audio_export_output_get_bytes(
    const otgy_audio_export_output* output) {
  if (output == nullptr) {
    return {nullptr, 0U};
  }
  const auto& bytes = output->value.bytes;
  return {bytes.empty() ? nullptr : bytes.data(),
          static_cast<std::uint64_t>(bytes.size())};
}

std::int64_t otgy_audio_export_output_get_signed_shift_frames(
    const otgy_audio_export_output* output) {
  return output == nullptr ? 0 : output->value.signed_shift_frames;
}

std::uint64_t otgy_audio_export_output_get_frame_count(
    const otgy_audio_export_output* output) {
  return output == nullptr
             ? 0U
             : static_cast<std::uint64_t>(output->value.output_frame_count);
}

}  // extern "C"
