// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include "av_core/audio/audio_export.hpp"
#include "av_core/audio/audio_export_name.hpp"
#include "av_core/audio/audio_export_quality.hpp"
#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"
#include "av_core/playback/playback_engine.hpp"
#include "av_core/playback/playback_pcm.hpp"
#include "av_core/playback/playback_position.hpp"
#include "av_core/playback/playback_sound.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/offset_detector.hpp"
#include "av_core/timing/onset_detector.hpp"
#include "av_core/timing/sync_analyzer.hpp"
#include "av_core/version.hpp"
#include "av_core/waveform/waveform_tile.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  expect(stream.good(), "unable to open fixture");
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

otgy_byte_view view(const std::vector<std::uint8_t>& bytes) {
  return {bytes.empty() ? nullptr : bytes.data(),
          static_cast<std::uint64_t>(bytes.size())};
}

otgy_string_view view(std::string_view value) {
  return {value.data(), static_cast<std::uint64_t>(value.size())};
}

template <typename T, void (*Destroy)(T*)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

template <typename T, void (*Destroy)(T*)>
Handle<T, Destroy> handle(T* value) {
  return Handle<T, Destroy>(value, Destroy);
}

bool same_double(double left, double right) {
  return std::memcmp(&left, &right, sizeof(double)) == 0;
}

void expect_error(const otgy_error* error, otgy_error_domain domain,
                  std::uint32_t code, std::string_view message) {
  expect(error != nullptr, "missing C ABI error");
  expect(otgy_error_get_domain(error) == domain,
         "C ABI error domain differs");
  expect(otgy_error_get_code(error) == code, "C ABI error code differs");
  const auto actual = otgy_error_get_message(error);
  expect(std::string_view(actual.data, static_cast<std::size_t>(actual.length)) ==
             message,
         "C ABI error message differs");
}

void compare_pcm(const otgy::core::Pcm16& direct, const otgy_pcm* abi) {
  expect(abi != nullptr, "missing C ABI PCM");
  expect(otgy_pcm_get_sample_rate(abi) == direct.sample_rate(),
         "PCM sample rate differs");
  expect(otgy_pcm_get_frame_count(abi) == direct.frame_count(),
         "PCM frame count differs");
  const auto bytes = direct.frame_count() * sizeof(std::int16_t);
  expect(std::memcmp(otgy_pcm_get_left(abi),
                     direct.channel_data(otgy::core::PcmChannel::Left),
                     bytes) == 0,
         "left PCM bytes differ");
  expect(std::memcmp(otgy_pcm_get_right(abi),
                     direct.channel_data(otgy::core::PcmChannel::Right),
                     bytes) == 0,
         "right PCM bytes differ");
}

struct ExportProgress final {
  std::vector<std::uint32_t> stages;
};

void export_progress(std::uint32_t stage, std::uint64_t, std::uint64_t,
                     void* context) {
  static_cast<ExportProgress*>(context)->stages.push_back(stage);
}

void direct_export_progress(otgy::core::AudioExportProgress progress,
                            void* context) noexcept {
  static_cast<ExportProgress*>(context)->stages.push_back(
      static_cast<std::uint32_t>(progress.stage));
}

std::uint8_t never_cancel(void*) { return 0U; }

std::uint8_t cancel_flag_requested(void* context) {
  return otgy_audio_export_cancel_flag_is_requested(
      static_cast<otgy_audio_export_cancel_flag*>(context));
}

struct SyncStages final {
  std::vector<std::uint32_t> values;
};

void sync_stage(std::uint32_t stage, void* context) {
  static_cast<SyncStages*>(context)->values.push_back(stage);
}

using DirectWaveform = otgy::core::WaveformTileResult (*)(
    const otgy::core::Pcm16&, double, double, std::uint64_t) noexcept;
using AbiWaveform = otgy_status (*)(const otgy_pcm*, double, double,
                                    std::uint64_t, otgy_waveform_tile**,
                                    otgy_error**);

void test_pcm_decode_hash_and_errors(
    const std::vector<std::uint8_t>& mp3,
    const std::vector<std::uint8_t>& ogg,
    otgy::core::Pcm16& direct_pcm,
    Handle<otgy_pcm, otgy_pcm_destroy>& abi_pcm,
    otgy::core::AudioSourceInfo& source_info) {
  auto direct_mp3 = otgy::core::decode_mp3(mp3.data(), mp3.size());
  expect(direct_mp3.has_value(), "direct MP3 decode failed");
  otgy_pcm* raw_mp3 = nullptr;
  otgy_error* raw_error = nullptr;
  expect(otgy_decode_mp3(view(mp3), &raw_mp3, &raw_error) == OTGY_STATUS_OK,
         "C ABI MP3 decode failed");
  auto abi_mp3 = handle<otgy_pcm, otgy_pcm_destroy>(raw_mp3);
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(error == nullptr, "successful MP3 decode returned an error");
  compare_pcm(*direct_mp3.pcm(), abi_mp3.get());
  otgy_audio_source_info abi_source{};
  expect(otgy_pcm_get_source_info(abi_mp3.get(), &abi_source) == 1U,
         "decoded source info is absent");
  expect(abi_source.codec ==
             static_cast<std::uint32_t>(direct_mp3.source_info()->codec) &&
             abi_source.measured_bitrate_bps ==
                 direct_mp3.source_info()->measured_bitrate_bps &&
             abi_source.nominal_bitrate_bps ==
                 direct_mp3.source_info()->nominal_bitrate_bps,
         "decoded source info differs");

  auto direct_ogg = otgy::core::decode_ogg_vorbis(ogg.data(), ogg.size());
  expect(direct_ogg.has_value(), "direct Ogg decode failed");
  auto abi_ogg_input = ogg;
  otgy_pcm* raw_ogg = nullptr;
  expect(otgy_decode_ogg_vorbis(view(abi_ogg_input), &raw_ogg, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Ogg decode failed");
  abi_pcm.reset(raw_ogg);
  std::fill(abi_ogg_input.begin(), abi_ogg_input.end(), 0U);
  abi_ogg_input.clear();
  compare_pcm(*direct_ogg.pcm(), abi_pcm.get());
  source_info = *direct_ogg.source_info();
  direct_pcm = std::move(direct_ogg).take_pcm();

  const auto direct_hash = otgy::core::pcm16le_sha256_hex(direct_pcm);
  otgy_bytes* raw_hash = nullptr;
  expect(otgy_pcm_sha256(abi_pcm.get(), &raw_hash, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI PCM hash failed");
  auto hash = handle<otgy_bytes, otgy_bytes_destroy>(raw_hash);
  const auto hash_view = otgy_bytes_get_view(hash.get());
  expect(hash_view.length == direct_hash.size() &&
             std::memcmp(hash_view.data, direct_hash.data(),
                         direct_hash.size()) == 0,
         "PCM hash differs");

  otgy_pcm* copied = nullptr;
  expect(otgy_pcm_copy_from_channels(
             direct_pcm.sample_rate(),
             direct_pcm.channel_data(otgy::core::PcmChannel::Left),
             direct_pcm.frame_count(),
             direct_pcm.channel_data(otgy::core::PcmChannel::Right),
             direct_pcm.frame_count(), &copied, &raw_error) == OTGY_STATUS_OK,
         "C ABI PCM copy failed");
  auto copied_pcm = handle<otgy_pcm, otgy_pcm_destroy>(copied);
  compare_pcm(direct_pcm, copied_pcm.get());
  expect(otgy_pcm_get_source_info(copied_pcm.get(), &abi_source) == 0U,
         "caller PCM unexpectedly has source info");

  auto direct_null = otgy::core::decode_mp3(nullptr, 1U);
  expect(!direct_null, "direct null MP3 unexpectedly succeeded");
  otgy_pcm* failed_pcm = reinterpret_cast<otgy_pcm*>(1);
  expect(otgy_decode_mp3({nullptr, 1U}, &failed_pcm, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "C ABI null MP3 status differs");
  error.reset(raw_error);
  expect(failed_pcm == nullptr, "failed decode exposed partial PCM");
  expect_error(error.get(), OTGY_ERROR_DOMAIN_MP3_DECODE,
               static_cast<std::uint32_t>(direct_null.error()),
               otgy::core::mp3_decode_error_message(direct_null.error()));

  expect(otgy_pcm_sha256(nullptr, nullptr, &raw_error) ==
             OTGY_STATUS_INVALID_ARGUMENT,
         "null ABI output was not rejected");
  error.reset(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
}

void test_offset_names_quality_and_export(
    const otgy::core::Pcm16& direct_pcm, const otgy_pcm* abi_pcm,
    otgy::core::AudioSourceInfo source_info) {
  otgy_error* raw_error = nullptr;
  auto direct_plan = otgy::core::calculate_pcm_offset_plan(
      direct_pcm.sample_rate(), direct_pcm.frame_count(),
      0.5 / static_cast<double>(direct_pcm.sample_rate()));
  expect(static_cast<bool>(direct_plan), "direct offset plan failed");
  otgy_pcm_offset_plan abi_plan{};
  expect(otgy_calculate_pcm_offset_plan(
             direct_pcm.sample_rate(), direct_pcm.frame_count(),
             0.5 / static_cast<double>(direct_pcm.sample_rate()), &abi_plan,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI offset plan failed");
  expect(abi_plan.signed_shift_frames ==
                 direct_plan.plan()->signed_shift_frames &&
             abi_plan.output_frame_count ==
                 direct_plan.plan()->output_frame_count,
         "offset plan differs");

  auto direct_offset = otgy::core::apply_pcm_offset(direct_pcm, -0.25);
  expect(static_cast<bool>(direct_offset), "direct PCM offset failed");
  otgy_pcm* raw_offset = nullptr;
  expect(otgy_apply_pcm_offset(abi_pcm, -0.25, &raw_offset, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI PCM offset failed");
  auto abi_offset = handle<otgy_pcm, otgy_pcm_destroy>(raw_offset);
  compare_pcm(*direct_offset.pcm(), abi_offset.get());

  const std::string source_name = "  CON?.ogg. ";
  auto direct_name =
      otgy::core::default_audio_export_base_name(source_name);
  otgy_bytes* raw_name = nullptr;
  expect(otgy_default_audio_export_base_name(view(source_name), &raw_name,
                                             &raw_error) == OTGY_STATUS_OK,
         "C ABI default export name failed");
  auto abi_name = handle<otgy_bytes, otgy_bytes_destroy>(raw_name);
  auto name_view = otgy_bytes_get_view(abi_name.get());
  expect(std::string_view(reinterpret_cast<const char*>(name_view.data),
                          static_cast<std::size_t>(name_view.length)) ==
             *direct_name.name(),
         "default export name differs");

  const std::string raw_base = "  A/B  ";
  const auto direct_normalized =
      otgy::core::normalize_audio_export_base_name(raw_base);
  expect(otgy_normalize_audio_export_base_name(view(raw_base), &raw_name,
                                               &raw_error) == OTGY_STATUS_OK,
         "C ABI export name normalization failed");
  abi_name.reset(raw_name);
  name_view = otgy_bytes_get_view(abi_name.get());
  expect(std::string_view(reinterpret_cast<const char*>(name_view.data),
                          static_cast<std::size_t>(name_view.length)) ==
             *direct_normalized.name(),
         "export name normalization differs");

  const otgy::core::AudioExportLocalTime direct_time{2026, 8, 31, 9, 7, 5};
  auto direct_file = otgy::core::make_audio_export_file_name(
      *direct_name.name(), direct_time, 3U);
  expect(otgy_make_audio_export_file_name(
             view(*direct_name.name()), {2026, 8, 31, 9, 7, 5}, 3U,
             &raw_name, &raw_error) == OTGY_STATUS_OK,
         "C ABI final export name failed");
  abi_name.reset(raw_name);
  name_view = otgy_bytes_get_view(abi_name.get());
  expect(std::string_view(reinterpret_cast<const char*>(name_view.data),
                          static_cast<std::size_t>(name_view.length)) ==
             *direct_file.name(),
         "final export name differs");

  const auto direct_policy = otgy::core::calculate_audio_export_quality(
      direct_pcm.sample_rate(), source_info);
  expect(static_cast<bool>(direct_policy), "direct export quality failed");
  otgy_audio_export_quality_policy abi_policy{};
  const otgy_audio_source_info abi_source{
      static_cast<std::uint32_t>(source_info.codec),
      source_info.measured_bitrate_bps, source_info.nominal_bitrate_bps};
  expect(otgy_calculate_audio_export_quality(direct_pcm.sample_rate(),
                                             abi_source, &abi_policy,
                                             &raw_error) == OTGY_STATUS_OK,
         "C ABI export quality failed");
  expect(abi_policy.source_bitrate_bps ==
                 direct_policy.policy()->source_bitrate_bps &&
             abi_policy.quality_ceiling ==
                 direct_policy.policy()->quality_ceiling &&
             abi_policy.default_quality ==
                 direct_policy.policy()->default_quality,
         "export quality policy differs");
  for (std::size_t index = 0U; index < 5U; ++index) {
    expect(abi_policy.nominal_bitrates_bps[index] ==
               direct_policy.policy()->nominal_bitrates_bps[index],
           "quality bitrate table differs");
  }
  expect(otgy_validate_audio_export_quality(
             &abi_policy, direct_policy.policy()->default_quality,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI valid quality rejected");
  const auto direct_quality_error = otgy::core::validate_audio_export_quality(
      *direct_policy.policy(), 0U);
  expect(otgy_validate_audio_export_quality(&abi_policy, 0U, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "C ABI invalid quality status differs");
  auto quality_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(quality_error.get(), OTGY_ERROR_DOMAIN_AUDIO_EXPORT_QUALITY,
               static_cast<std::uint32_t>(direct_quality_error),
               otgy::core::audio_export_quality_error_message(
                   direct_quality_error));

  const otgy::core::OggVorbisMetadata direct_metadata{"Title", "Artist",
                                                       "Album"};
  const auto quality = direct_policy.policy()->default_quality;
  auto direct_snapshot = otgy::core::create_audio_export_snapshot(
      direct_pcm, 77U, 0.0, quality, source_info, "abi-test",
      direct_metadata);
  expect(static_cast<bool>(direct_snapshot), "direct export snapshot failed");
  otgy_audio_export_snapshot* raw_snapshot = nullptr;
  const otgy_audio_export_metadata abi_metadata{
      view("Title"), view("Artist"), view("Album")};
  otgy_pcm* raw_snapshot_pcm = nullptr;
  expect(otgy_pcm_copy_from_channels(
             direct_pcm.sample_rate(),
             direct_pcm.channel_data(otgy::core::PcmChannel::Left),
             direct_pcm.frame_count(),
             direct_pcm.channel_data(otgy::core::PcmChannel::Right),
             direct_pcm.frame_count(), &raw_snapshot_pcm, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI snapshot PCM copy failed");
  auto snapshot_pcm = handle<otgy_pcm, otgy_pcm_destroy>(raw_snapshot_pcm);
  expect(otgy_audio_export_snapshot_create(
             snapshot_pcm.get(), 77U, 0.0, quality, abi_source,
             view("abi-test"),
             abi_metadata, &raw_snapshot, &raw_error) == OTGY_STATUS_OK,
         "C ABI export snapshot failed");
  auto abi_snapshot =
      handle<otgy_audio_export_snapshot, otgy_audio_export_snapshot_destroy>(
          raw_snapshot);
  snapshot_pcm.reset();
  ExportProgress direct_progress;
  const auto direct_output = otgy::core::encode_audio_export_snapshot(
      *direct_snapshot.snapshot(), direct_export_progress, nullptr,
      &direct_progress);
  expect(static_cast<bool>(direct_output), "direct audio export failed");
  ExportProgress progress;
  otgy_audio_export_output* raw_output = nullptr;
  expect(otgy_audio_export_snapshot_encode(
             abi_snapshot.get(), export_progress, never_cancel, &progress,
             &raw_output, &raw_error) == OTGY_STATUS_OK,
         "C ABI audio export failed");
  auto abi_output =
      handle<otgy_audio_export_output, otgy_audio_export_output_destroy>(
          raw_output);
  const auto bytes = otgy_audio_export_output_get_bytes(abi_output.get());
  expect(bytes.length == direct_output.output()->bytes.size() &&
             std::memcmp(bytes.data, direct_output.output()->bytes.data(),
                         direct_output.output()->bytes.size()) == 0,
         "audio export bytes differ");
  expect(otgy_audio_export_output_get_signed_shift_frames(abi_output.get()) ==
                 direct_output.output()->signed_shift_frames &&
             otgy_audio_export_output_get_frame_count(abi_output.get()) ==
                 direct_output.output()->output_frame_count,
         "audio export metadata differs");
  expect(progress.stages == direct_progress.stages,
         "audio export stages differ");

  otgy_audio_export_cancel_flag* raw_cancel_flag = nullptr;
  expect(otgy_audio_export_cancel_flag_create(&raw_cancel_flag, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI export cancel flag creation failed");
  auto cancel_flag =
      handle<otgy_audio_export_cancel_flag,
             otgy_audio_export_cancel_flag_destroy>(raw_cancel_flag);
  expect(otgy_audio_export_cancel_flag_is_requested(cancel_flag.get()) == 0U,
         "new C ABI export cancel flag was requested");
  otgy_audio_export_cancel_flag_request(cancel_flag.get());
  expect(otgy_audio_export_cancel_flag_is_requested(cancel_flag.get()) == 1U,
         "C ABI export cancel request was not retained");
  raw_output = reinterpret_cast<otgy_audio_export_output*>(1);
  expect(otgy_audio_export_snapshot_encode(
             abi_snapshot.get(), nullptr, cancel_flag_requested,
             cancel_flag.get(), &raw_output, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "C ABI requested export cancellation was not returned");
  auto cancel_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(raw_output == nullptr,
         "cancelled C ABI export exposed partial ownership");
  expect_error(cancel_error.get(), OTGY_ERROR_DOMAIN_AUDIO_EXPORT_TASK,
               static_cast<std::uint32_t>(
                   otgy::core::AudioExportTaskError::Cancelled),
               otgy::core::audio_export_task_error_message(
                   otgy::core::AudioExportTaskError::Cancelled));

  const auto direct_bad_snapshot = otgy::core::create_audio_export_snapshot(
      direct_pcm, 77U, 31.0, quality, source_info, "abi-test",
      direct_metadata);
  otgy_audio_export_snapshot* failed_snapshot =
      reinterpret_cast<otgy_audio_export_snapshot*>(1);
  expect(otgy_audio_export_snapshot_create(
             abi_pcm, 77U, 31.0, quality, abi_source, view("abi-test"),
             abi_metadata, &failed_snapshot, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "C ABI invalid export snapshot status differs");
  auto snapshot_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(failed_snapshot == nullptr,
         "failed export snapshot exposed partial ownership");
  expect_error(snapshot_error.get(), OTGY_ERROR_DOMAIN_AUDIO_EXPORT_SNAPSHOT,
               static_cast<std::uint32_t>(direct_bad_snapshot.error()),
               otgy::core::audio_export_snapshot_error_message(
                   direct_bad_snapshot.error()));

  otgy_audio_export_snapshot_destroy(nullptr);
  otgy_audio_export_output_destroy(nullptr);
  otgy_audio_export_cancel_flag_destroy(nullptr);
}

void test_audio_end(const otgy::core::Pcm16& direct_pcm,
                    const otgy_pcm* abi_pcm,
                    const otgy::core::AudioSourceInfo& source_info) {
  otgy_error* raw_error = nullptr;
  const otgy_audio_source_info abi_source{
      static_cast<std::uint32_t>(source_info.codec),
      source_info.measured_bitrate_bps, source_info.nominal_bitrate_bps};
  const auto policy = otgy::core::calculate_audio_export_quality(
      direct_pcm.sample_rate(), source_info);
  expect(static_cast<bool>(policy), "end test quality failed");
  const auto quality = policy.policy()->default_quality;

  const auto direct_beat_seconds =
      otgy::core::calculate_audio_end_export_seconds(4U, 100.0);
  expect(static_cast<bool>(direct_beat_seconds),
         "direct end seconds failed");
  double abi_seconds = 0.0;
  expect(otgy_calculate_audio_end_export_seconds(
             OTGY_AUDIO_END_MODE_BEAT, 4U, 100.0, 0.0, 0.0, &abi_seconds,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI end seconds failed");
  expect(same_double(abi_seconds, *direct_beat_seconds.seconds()),
         "end seconds differ");

  const double audio_duration =
      static_cast<double>(direct_pcm.frame_count()) /
      static_cast<double>(direct_pcm.sample_rate());
  expect(otgy_calculate_audio_end_export_seconds(
             OTGY_AUDIO_END_MODE_SOURCE_END, 0U, 100.0, 0.25, audio_duration,
             &abi_seconds, &raw_error) == OTGY_STATUS_OK,
         "C ABI source end seconds failed");
  expect(same_double(abi_seconds, audio_duration + 0.25),
         "source end seconds differ");

  auto direct_snapshot = otgy::core::create_audio_export_snapshot_with_end(
      direct_pcm, 91U, 0.0, 100.0,
      otgy::core::AudioEndSpec{otgy::core::AudioEndMode::Beat, 4U}, quality,
      source_info, "end-test");
  expect(static_cast<bool>(direct_snapshot),
         "direct custom end snapshot failed");
  auto direct_output = otgy::core::encode_audio_export_snapshot(
      *direct_snapshot.snapshot());
  expect(static_cast<bool>(direct_output), "direct custom end export failed");

  otgy_audio_export_snapshot* raw_snapshot = nullptr;
  expect(otgy_audio_export_snapshot_create_with_end(
             abi_pcm, 91U, 0.0, 100.0, OTGY_AUDIO_END_MODE_BEAT, 4U, quality,
             abi_source, view("end-test"), otgy_audio_export_metadata{},
             &raw_snapshot, &raw_error) == OTGY_STATUS_OK,
         "C ABI custom end snapshot failed");
  auto abi_snapshot =
      handle<otgy_audio_export_snapshot, otgy_audio_export_snapshot_destroy>(
          raw_snapshot);
  otgy_audio_export_output* raw_output = nullptr;
  expect(otgy_audio_export_snapshot_encode(abi_snapshot.get(), nullptr,
                                           nullptr, nullptr, &raw_output,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI custom end export failed");
  auto abi_output =
      handle<otgy_audio_export_output, otgy_audio_export_output_destroy>(
          raw_output);
  const auto abi_bytes = otgy_audio_export_output_get_bytes(abi_output.get());
  expect(abi_bytes.length == direct_output.output()->bytes.size() &&
             std::memcmp(abi_bytes.data, direct_output.output()->bytes.data(),
                         direct_output.output()->bytes.size()) == 0,
         "custom end export bytes differ");
  expect(otgy_audio_export_output_get_frame_count(abi_output.get()) ==
                 direct_output.output()->output_frame_count &&
             otgy_audio_export_output_get_signed_shift_frames(
                 abi_output.get()) ==
                 direct_output.output()->signed_shift_frames,
         "custom end export metadata differs");

  const auto direct_limit =
      otgy::core::calculate_playback_frame_limit(11'025U, 0.6);
  expect(static_cast<bool>(direct_limit), "direct playback limit failed");
  std::uint64_t abi_limit = 0U;
  expect(otgy_calculate_playback_frame_limit(11'025U, 0.6, &abi_limit,
                                             &raw_error) == OTGY_STATUS_OK,
         "C ABI playback limit failed");
  expect(abi_limit == *direct_limit.frame_count(),
         "playback frame limit differs");

  otgy_audio_export_snapshot* failed_snapshot =
      reinterpret_cast<otgy_audio_export_snapshot*>(1);
  expect(otgy_audio_export_snapshot_create_with_end(
             abi_pcm, 0U, 0.0, 100.0, OTGY_AUDIO_END_MODE_BEAT, 0U, quality,
             abi_source, view("end-test"), otgy_audio_export_metadata{},
             &failed_snapshot, &raw_error) == OTGY_STATUS_CORE_ERROR,
         "C ABI empty end status differs");
  auto end_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(failed_snapshot == nullptr,
         "failed custom end exposed partial ownership");
  expect_error(
      end_error.get(), OTGY_ERROR_DOMAIN_AUDIO_EXPORT_SNAPSHOT,
      static_cast<std::uint32_t>(
          otgy::core::AudioExportSnapshotError::EmptyOutputAfterEnd),
      otgy::core::audio_export_snapshot_error_message(
          otgy::core::AudioExportSnapshotError::EmptyOutputAfterEnd));
}

void test_waveforms(const otgy::core::Pcm16& direct_pcm,
                    const otgy_pcm* abi_pcm) {
  const std::array<DirectWaveform, 16> direct_functions{
      otgy::core::generate_signed_uniform_waveform_tile_1x,
      otgy::core::generate_rectified_uniform_waveform_tile_1x,
      otgy::core::generate_signed_amplitude_waveform_tile_1x,
      otgy::core::generate_rectified_amplitude_waveform_tile_1x,
      otgy::core::generate_signed_uniform_waveform_tile_2x,
      otgy::core::generate_rectified_uniform_waveform_tile_2x,
      otgy::core::generate_signed_amplitude_waveform_tile_2x,
      otgy::core::generate_rectified_amplitude_waveform_tile_2x,
      otgy::core::generate_signed_uniform_waveform_tile_3x,
      otgy::core::generate_rectified_uniform_waveform_tile_3x,
      otgy::core::generate_signed_amplitude_waveform_tile_3x,
      otgy::core::generate_rectified_amplitude_waveform_tile_3x,
      otgy::core::generate_signed_uniform_waveform_tile_4x,
      otgy::core::generate_rectified_uniform_waveform_tile_4x,
      otgy::core::generate_signed_amplitude_waveform_tile_4x,
      otgy::core::generate_rectified_amplitude_waveform_tile_4x};
  const std::array<AbiWaveform, 16> abi_functions{
      otgy_generate_signed_uniform_1x_waveform_tile,
      otgy_generate_rectified_uniform_1x_waveform_tile,
      otgy_generate_signed_amplitude_1x_waveform_tile,
      otgy_generate_rectified_amplitude_1x_waveform_tile,
      otgy_generate_signed_uniform_2x_waveform_tile,
      otgy_generate_rectified_uniform_2x_waveform_tile,
      otgy_generate_signed_amplitude_2x_waveform_tile,
      otgy_generate_rectified_amplitude_2x_waveform_tile,
      otgy_generate_signed_uniform_3x_waveform_tile,
      otgy_generate_rectified_uniform_3x_waveform_tile,
      otgy_generate_signed_amplitude_3x_waveform_tile,
      otgy_generate_rectified_amplitude_3x_waveform_tile,
      otgy_generate_signed_uniform_4x_waveform_tile,
      otgy_generate_rectified_uniform_4x_waveform_tile,
      otgy_generate_signed_amplitude_4x_waveform_tile,
      otgy_generate_rectified_amplitude_4x_waveform_tile};
  for (std::size_t index = 0U; index < direct_functions.size(); ++index) {
    const auto direct = direct_functions[index](direct_pcm, 11.0, 4.0, 1U);
    expect(static_cast<bool>(direct), "direct waveform failed");
    otgy_waveform_tile* raw_tile = nullptr;
    otgy_error* raw_error = nullptr;
    expect(abi_functions[index](abi_pcm, 11.0, 4.0, 1U, &raw_tile,
                                &raw_error) == OTGY_STATUS_OK,
           "C ABI waveform failed");
    auto tile = handle<otgy_waveform_tile, otgy_waveform_tile_destroy>(raw_tile);
    auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
    expect(error == nullptr, "waveform returned an error");
    expect(otgy_waveform_tile_get_block_index(tile.get()) ==
                   direct.tile()->block_index() &&
               otgy_waveform_tile_get_pixels_per_second(tile.get()) ==
                   direct.tile()->pixels_per_second() &&
               otgy_waveform_tile_get_channel_width(tile.get()) ==
                   direct.tile()->channel_width() &&
               otgy_waveform_tile_get_channel_spacing(tile.get()) ==
                   direct.tile()->channel_spacing(),
           "waveform geometry differs");
    const auto left = otgy_waveform_tile_get_left(tile.get());
    const auto right = otgy_waveform_tile_get_right(tile.get());
    expect(left.length == direct.tile()->channel_byte_size() &&
               right.length == direct.tile()->channel_byte_size() &&
               std::memcmp(
                   left.data,
                   direct.tile()->channel_alpha(otgy::core::PcmChannel::Left),
                   static_cast<std::size_t>(left.length)) == 0 &&
               std::memcmp(
                   right.data,
                   direct.tile()->channel_alpha(otgy::core::PcmChannel::Right),
                   static_cast<std::size_t>(right.length)) == 0,
           "waveform Alpha differs");
  }

  const auto direct_generic = otgy::core::generate_waveform_tile(
      direct_pcm, 11.0, 4.0, 1U, otgy::core::WaveformShape::Signed,
      otgy::core::WaveformLuminance::Amplitude,
      otgy::core::WaveformSampling::TwoX, otgy::core::WaveformFilter::None,
      0.75);
  expect(static_cast<bool>(direct_generic), "direct generic waveform failed");
  otgy_waveform_tile* generic_raw_tile = nullptr;
  otgy_error* generic_raw_error = nullptr;
  expect(otgy_generate_waveform_tile(
             abi_pcm, 11.0, 4.0, 1U, OTGY_WAVEFORM_SHAPE_SIGNED,
             OTGY_WAVEFORM_LUMINANCE_AMPLITUDE, OTGY_WAVEFORM_ANTIALIAS_2X,
             OTGY_WAVEFORM_FILTER_NONE, 0.75, &generic_raw_tile,
             &generic_raw_error) == OTGY_STATUS_OK,
         "C ABI generic waveform failed");
  auto generic_tile =
      handle<otgy_waveform_tile, otgy_waveform_tile_destroy>(generic_raw_tile);
  auto generic_error =
      handle<otgy_error, otgy_error_destroy>(generic_raw_error);
  expect(generic_error == nullptr, "generic waveform returned an error");
  const auto generic_left = otgy_waveform_tile_get_left(generic_tile.get());
  const auto generic_right = otgy_waveform_tile_get_right(generic_tile.get());
  expect(generic_left.length == direct_generic.tile()->channel_byte_size() &&
             std::memcmp(generic_left.data,
                         direct_generic.tile()->channel_alpha(
                             otgy::core::PcmChannel::Left),
                         static_cast<std::size_t>(generic_left.length)) == 0 &&
             generic_right.length ==
                 direct_generic.tile()->channel_byte_size() &&
             std::memcmp(generic_right.data,
                         direct_generic.tile()->channel_alpha(
                             otgy::core::PcmChannel::Right),
                         static_cast<std::size_t>(generic_right.length)) == 0,
         "generic waveform Alpha differs");

  otgy_waveform_tile* invalid_option_tile = nullptr;
  otgy_error* invalid_option_raw_error = nullptr;
  expect(otgy_generate_waveform_tile(
             abi_pcm, 11.0, 4.0, 1U,
             std::numeric_limits<std::uint32_t>::max(),
             OTGY_WAVEFORM_LUMINANCE_UNIFORM, OTGY_WAVEFORM_ANTIALIAS_2X,
             OTGY_WAVEFORM_FILTER_NONE, 0.75, &invalid_option_tile,
             &invalid_option_raw_error) == OTGY_STATUS_CORE_ERROR,
         "C ABI accepted an invalid waveform option");
  auto invalid_option_error =
      handle<otgy_error, otgy_error_destroy>(invalid_option_raw_error);
  expect(invalid_option_tile == nullptr,
         "invalid waveform option exposed a tile");
  expect_error(invalid_option_error.get(), OTGY_ERROR_DOMAIN_WAVEFORM,
               static_cast<std::uint32_t>(
                   otgy::core::WaveformError::InvalidOption),
               otgy::core::waveform_error_message(
                   otgy::core::WaveformError::InvalidOption));

  const auto expect_filtered_tile =
      [&](otgy::core::WaveformFilter direct_filter, std::uint32_t abi_filter,
          std::uint64_t block_index) {
        const auto direct = otgy::core::generate_waveform_tile(
            direct_pcm, 11.0, 4.0, block_index,
            otgy::core::WaveformShape::Signed,
            otgy::core::WaveformLuminance::Amplitude,
            otgy::core::WaveformSampling::TwoX, direct_filter, 0.75);
        expect(static_cast<bool>(direct), "direct filtered waveform failed");

        otgy_waveform_tile* raw_filtered_tile = nullptr;
        otgy_error* raw_filtered_error = nullptr;
        expect(otgy_generate_waveform_tile(
                   abi_pcm, 11.0, 4.0, block_index, OTGY_WAVEFORM_SHAPE_SIGNED,
                   OTGY_WAVEFORM_LUMINANCE_AMPLITUDE,
                   OTGY_WAVEFORM_ANTIALIAS_2X, abi_filter, 0.75,
                   &raw_filtered_tile, &raw_filtered_error) == OTGY_STATUS_OK,
               "C ABI filtered waveform failed");
        auto filtered_tile =
            handle<otgy_waveform_tile, otgy_waveform_tile_destroy>(
                raw_filtered_tile);
        auto filtered_error =
            handle<otgy_error, otgy_error_destroy>(raw_filtered_error);
        expect(filtered_error == nullptr,
               "filtered waveform returned an error");

        const auto left = otgy_waveform_tile_get_left(filtered_tile.get());
        const auto right = otgy_waveform_tile_get_right(filtered_tile.get());
        expect(left.length == direct.tile()->channel_byte_size() &&
                   right.length == direct.tile()->channel_byte_size() &&
                   std::memcmp(left.data,
                               direct.tile()->channel_alpha(
                                   otgy::core::PcmChannel::Left),
                               static_cast<std::size_t>(left.length)) == 0 &&
                   std::memcmp(right.data,
                               direct.tile()->channel_alpha(
                                   otgy::core::PcmChannel::Right),
                               static_cast<std::size_t>(right.length)) == 0,
               "filtered waveform Alpha differs");
      };

  // The second adjacent tile for each filter reuses the C ABI's complete-PCM
  // cache and must remain byte-identical to a direct av_core call.
  for (const auto block_index : {1U, 2U}) {
    expect_filtered_tile(otgy::core::WaveformFilter::HighPass,
                         OTGY_WAVEFORM_FILTER_HIGH_PASS, block_index);
  }
  for (const auto block_index : {1U, 2U}) {
    expect_filtered_tile(otgy::core::WaveformFilter::LowPass,
                         OTGY_WAVEFORM_FILTER_LOW_PASS, block_index);
  }

  const auto direct_error = direct_functions[0](direct_pcm, 0.0, 4.0, 0U);
  otgy_waveform_tile* raw_tile = nullptr;
  otgy_error* raw_error = nullptr;
  expect(abi_functions[0](abi_pcm, 0.0, 4.0, 0U, &raw_tile, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "waveform error status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(raw_tile == nullptr, "failed waveform exposed a tile");
  expect_error(error.get(), OTGY_ERROR_DOMAIN_WAVEFORM,
               static_cast<std::uint32_t>(direct_error.error()),
               otgy::core::waveform_error_message(direct_error.error()));
}

void test_analysis(const otgy::core::Pcm16& direct_pcm,
                   const otgy_pcm* abi_pcm) {
  otgy_error* raw_error = nullptr;
  const auto direct_onsets = otgy::core::detect_onsets(direct_pcm);
  otgy_onsets* raw_onsets = nullptr;
  expect(otgy_detect_onsets(abi_pcm, &raw_onsets, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI onset detection failed");
  auto onsets = handle<otgy_onsets, otgy_onsets_destroy>(raw_onsets);
  expect(otgy_onsets_get_count(onsets.get()) == direct_onsets.onsets()->size(),
         "onset count differs");
  const auto* abi_onsets = otgy_onsets_get_data(onsets.get());
  for (std::size_t index = 0U; index < direct_onsets.onsets()->size(); ++index) {
    expect(abi_onsets[index].frame_index ==
                   direct_onsets.onsets()->at(index).frame_index &&
               same_double(abi_onsets[index].strength,
                           direct_onsets.onsets()->at(index).strength),
           "onset value differs");
  }

  const auto direct_bpm = otgy::core::detect_bpm_candidates(direct_pcm);
  otgy_bpm_candidates* raw_bpm = nullptr;
  expect(otgy_detect_bpm_candidates(abi_pcm, &raw_bpm, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI BPM detection failed");
  auto bpm = handle<otgy_bpm_candidates, otgy_bpm_candidates_destroy>(raw_bpm);
  expect(otgy_bpm_candidates_get_count(bpm.get()) ==
             direct_bpm.candidates()->size(),
         "BPM count differs");
  const auto* abi_bpm = otgy_bpm_candidates_get_data(bpm.get());
  for (std::size_t index = 0U; index < direct_bpm.candidates()->size(); ++index) {
    expect(same_double(abi_bpm[index].bpm,
                       direct_bpm.candidates()->at(index).bpm) &&
               same_double(abi_bpm[index].fitness,
                           direct_bpm.candidates()->at(index).fitness),
           "BPM candidate differs");
  }

  const auto direct_offsets =
      otgy::core::detect_offsets(direct_pcm, *direct_bpm.candidates());
  otgy_offset_candidates* raw_offsets = nullptr;
  expect(otgy_detect_offsets(abi_pcm, abi_bpm,
                             otgy_bpm_candidates_get_count(bpm.get()),
                             &raw_offsets, &raw_error) == OTGY_STATUS_OK,
         "C ABI Offset detection failed");
  auto offsets =
      handle<otgy_offset_candidates, otgy_offset_candidates_destroy>(
          raw_offsets);
  const auto* abi_offsets = otgy_offset_candidates_get_data(offsets.get());
  expect(otgy_offset_candidates_get_count(offsets.get()) ==
             direct_offsets.candidates()->size(),
         "Offset count differs");
  for (std::size_t index = 0U;
       index < direct_offsets.candidates()->size(); ++index) {
    const auto& direct = direct_offsets.candidates()->at(index);
    expect(same_double(abi_offsets[index].bpm, direct.bpm) &&
               same_double(abi_offsets[index].detector_offset_seconds,
                           direct.detector_offset_seconds) &&
               same_double(abi_offsets[index].app_offset_seconds,
                           direct.app_offset_seconds) &&
               same_double(abi_offsets[index].fitness, direct.fitness),
           "Offset candidate differs");
  }

  const auto direct_sync = otgy::core::analyze_sync(direct_pcm);
  SyncStages stages;
  otgy_sync_analysis* raw_sync = nullptr;
  expect(otgy_analyze_sync(abi_pcm, sync_stage, &stages, &raw_sync,
                           &raw_error) == OTGY_STATUS_OK,
         "C ABI sync analysis failed");
  auto sync =
      handle<otgy_sync_analysis, otgy_sync_analysis_destroy>(raw_sync);
  expect(otgy_sync_analysis_get_analyzed_frame_count(sync.get()) ==
             direct_sync.analysis()->analyzed_frame_count(),
         "sync frame count differs");
  expect(stages.values ==
             std::vector<std::uint32_t>({0U, 1U, 2U, 3U, 4U, 5U}),
         "sync stage order differs");
  const auto* abi_sync = otgy_sync_analysis_get_candidates(sync.get());
  expect(otgy_sync_analysis_get_candidate_count(sync.get()) ==
             direct_sync.analysis()->candidates().size(),
         "sync candidate count differs");
  for (std::size_t index = 0U;
       index < direct_sync.analysis()->candidates().size(); ++index) {
    expect(same_double(abi_sync[index].app_offset_seconds,
                       direct_sync.analysis()
                           ->candidates()[index]
                           .app_offset_seconds),
           "sync candidate differs");
  }

  otgy_onsets_destroy(nullptr);
  otgy_bpm_candidates_destroy(nullptr);
  otgy_offset_candidates_destroy(nullptr);
  otgy_sync_analysis_destroy(nullptr);
}

void test_playback(const otgy::core::Pcm16& direct_pcm,
                   const otgy_pcm* abi_pcm) {
  constexpr double kStart = -0.125;
  const auto direct = otgy::core::render_playback_pcm(direct_pcm, kStart);
  expect(static_cast<bool>(direct), "direct playback PCM failed");
  otgy_playback_pcm* raw_pcm = nullptr;
  otgy_error* raw_error = nullptr;
  expect(otgy_render_playback_pcm(abi_pcm, kStart, &raw_pcm, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI playback PCM failed");
  auto pcm = handle<otgy_playback_pcm, otgy_playback_pcm_destroy>(raw_pcm);
  expect(otgy_playback_pcm_get_sample_rate(pcm.get()) ==
                 direct.pcm()->sample_rate() &&
             otgy_playback_pcm_get_frame_count(pcm.get()) ==
                 direct.pcm()->frame_count(),
         "playback PCM geometry differs");
  const auto bytes = otgy_playback_pcm_get_bytes(pcm.get());
  expect(bytes.length == direct.pcm()->bytes().size() &&
             std::memcmp(bytes.data, direct.pcm()->bytes().data(),
                         direct.pcm()->bytes().size()) == 0,
         "playback PCM bytes differ");

  const std::array<std::uint8_t, 46U> click_wav{{
      'R', 'I', 'F', 'F', 38, 0, 0, 0, 'W', 'A', 'V', 'E',
      'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
      4, 0, 0, 0, 8, 0, 0, 0, 2, 0, 16, 0,
      'd', 'a', 't', 'a', 2, 0, 0, 0, 100, 0,
  }};
  const otgy::core::PlaybackSoundAsset direct_asset{
      otgy::core::PlaybackSoundLanguage::Chinese,
      otgy::core::PlaybackSoundId::SpaceBar, click_wav.data(),
      click_wav.size()};
  auto direct_bank_result =
      otgy::core::decode_playback_sound_bank(&direct_asset, 1U);
  expect(static_cast<bool>(direct_bank_result),
         "direct playback sound bank failed");
  auto direct_bank = std::move(direct_bank_result).take_bank();
  const otgy::core::ChartDraft empty_chart{{}, {}, {}, {}, {}, {}};
  const otgy::core::PlaybackSoundConfig direct_config{
      otgy::core::PlaybackSoundLanguage::Chinese,
      otgy::core::PlaybackDanceMode::FourKey, false, 1U, {100.0, 0.0}};
  const auto direct_chart_pcm = otgy::core::render_chart_playback_pcm(
      direct_pcm, empty_chart, direct_bank, direct_config, kStart);
  expect(static_cast<bool>(direct_chart_pcm),
         "direct chart playback PCM failed");
  otgy_playback_sound_bank* raw_bank = nullptr;
  const otgy_playback_sound_asset abi_asset{
      OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE, OTGY_PLAYBACK_SOUND_SPACE_BAR,
      {click_wav.data(), click_wav.size()}};
  expect(otgy_playback_sound_bank_create(&abi_asset, 1U, &raw_bank,
                                         &raw_error) == OTGY_STATUS_OK,
         "C ABI playback sound bank failed");
  auto bank = handle<otgy_playback_sound_bank,
                     otgy_playback_sound_bank_destroy>(raw_bank);
  otgy_chart* raw_chart = nullptr;
  expect(otgy_chart_create_empty(&raw_chart, &raw_error) == OTGY_STATUS_OK,
         "C ABI empty chart failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  const otgy_playback_sound_config abi_config{
      OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE, OTGY_PLAYBACK_DANCE_MODE_4K, 0U,
      1U, 100.0, 0.0};
  raw_pcm = nullptr;
  expect(otgy_render_chart_playback_pcm(
             abi_pcm, chart.get(), bank.get(), abi_config, kStart, &raw_pcm,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI chart playback PCM failed");
  auto chart_pcm =
      handle<otgy_playback_pcm, otgy_playback_pcm_destroy>(raw_pcm);
  const auto chart_bytes = otgy_playback_pcm_get_bytes(chart_pcm.get());
  expect(chart_bytes.length == direct_chart_pcm.pcm()->bytes().size() &&
             std::memcmp(chart_bytes.data,
                         direct_chart_pcm.pcm()->bytes().data(),
                         direct_chart_pcm.pcm()->bytes().size()) == 0,
         "C ABI chart playback PCM bytes differ");

  const otgy::core::RhythmTestSoundConfig direct_rhythm_config{
      otgy::core::PlaybackSoundLanguage::Chinese, {100.0, 0.0}};
  const auto direct_rhythm_pcm =
      otgy::core::render_rhythm_test_playback_pcm(
          direct_pcm, direct_bank, direct_rhythm_config, kStart);
  expect(static_cast<bool>(direct_rhythm_pcm),
         "direct rhythm test playback PCM failed");
  const otgy_rhythm_test_sound_config abi_rhythm_config{
      OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE, 100.0, 0.0};
  raw_pcm = nullptr;
  expect(otgy_render_rhythm_test_playback_pcm(
             abi_pcm, bank.get(), abi_rhythm_config, kStart, &raw_pcm,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI rhythm test playback PCM failed");
  auto rhythm_pcm =
      handle<otgy_playback_pcm, otgy_playback_pcm_destroy>(raw_pcm);
  const auto rhythm_bytes = otgy_playback_pcm_get_bytes(rhythm_pcm.get());
  expect(rhythm_bytes.length == direct_rhythm_pcm.pcm()->bytes().size() &&
             std::memcmp(rhythm_bytes.data,
                         direct_rhythm_pcm.pcm()->bytes().data(),
                         direct_rhythm_pcm.pcm()->bytes().size()) == 0,
         "C ABI rhythm test playback PCM bytes differ");

  const auto duration_seconds =
      static_cast<double>(direct_pcm.frame_count()) /
      static_cast<double>(direct_pcm.sample_rate());
  const otgy::core::PlaybackEngineConfig direct_engine_config{
      otgy::core::PlaybackSoundLanguage::Chinese,
      otgy::core::PlaybackDanceMode::FourKey, false, 1U, {100.0, 0.0}, 0.0,
      duration_seconds,
      otgy::core::default_judgement_config({100.0, 0.0}), true};
  otgy::core::PlaybackEngine direct_engine;
  expect(direct_engine.initialize(direct_pcm, empty_chart, direct_bank,
                                  direct_engine_config) ==
             otgy::core::PlaybackEngineError::None,
         "direct playback engine failed");

  const otgy_playback_engine_config abi_engine_config{
      OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE, OTGY_PLAYBACK_DANCE_MODE_4K, 0U,
      1U, 100.0, 0.0, 0.0, duration_seconds,
      OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE, 0.125, 0.25, 0.375, 0.5,
      0.0, 1U};
  otgy_playback_engine* raw_engine = nullptr;
  expect(otgy_playback_engine_create(abi_pcm, chart.get(), bank.get(),
                                     abi_engine_config, &raw_engine,
                                     &raw_error) == OTGY_STATUS_OK,
         "C ABI playback engine failed");
  auto engine = handle<otgy_playback_engine,
                       otgy_playback_engine_destroy>(raw_engine);
  expect(otgy_playback_engine_get_sample_rate(engine.get()) ==
             direct_pcm.sample_rate(),
         "C ABI playback engine sample rate differs");

  const auto engine_reference =
      otgy::core::render_playback_pcm(direct_pcm, 0.0);
  expect(static_cast<bool>(engine_reference),
         "direct engine reference PCM failed");
  const auto* engine_reference_pcm = engine_reference.pcm();
  const auto& engine_reference_bytes = engine_reference_pcm->bytes();
  std::uint64_t rendered = 0U;
  while (rendered < engine_reference_pcm->frame_count()) {
    const auto remaining = engine_reference_pcm->frame_count() - rendered;
    const auto block = static_cast<std::uint32_t>(
        remaining < 17U ? remaining : 17U);
    std::vector<std::uint8_t> direct_block(
        static_cast<std::size_t>(block) * 4U);
    direct_engine.render(direct_block.data(), block);
    std::vector<std::uint8_t> abi_block(
        static_cast<std::size_t>(block) * 4U);
    otgy_playback_engine_render(engine.get(), abi_block.data(), block);
    expect(direct_block == abi_block,
           "C ABI playback engine block differs from core");
    const auto byte_offset = static_cast<std::size_t>(rendered) * 4U;
    expect(std::memcmp(direct_block.data(),
                       engine_reference_bytes.data() + byte_offset,
                       direct_block.size()) == 0,
           "playback engine output differs from offline PCM");
    rendered += block;
  }
  otgy_playback_engine_state engine_state{};
  otgy_playback_engine_get_state(engine.get(), &engine_state);
  expect(engine_state.finished != 0U &&
             engine_state.rendered_frames ==
                 engine_reference_pcm->frame_count(),
         "C ABI playback engine state differs");
  otgy_playback_judgement judgements[4]{};
  std::uint32_t judgement_count = 0U;
  otgy_playback_engine_drain_judgements(engine.get(), judgements, 4U,
                                        &judgement_count);
  expect(judgement_count == 0U,
         "empty chart produced a judgement");

  const otgy_playback_engine_config free_engine_config{
      OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE, OTGY_PLAYBACK_DANCE_MODE_4K, 0U,
      1U, 100.0, 0.0, 0.0, duration_seconds,
      OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE, 0.125, 0.25, 0.375, 0.5,
      0.0, 0U};
  otgy_playback_engine* raw_free_engine = nullptr;
  expect(otgy_playback_engine_create(abi_pcm, chart.get(), bank.get(),
                                     free_engine_config, &raw_free_engine,
                                     &raw_error) == OTGY_STATUS_OK,
         "C ABI free-play engine failed");
  auto free_engine = handle<otgy_playback_engine,
                            otgy_playback_engine_destroy>(raw_free_engine);
  otgy_playback_engine_push_trigger(free_engine.get(),
                                    OTGY_PLAYBACK_SOUND_SPACE_BAR);
  std::vector<std::uint8_t> free_block(17U * 4U);
  otgy_playback_engine_render(free_engine.get(), free_block.data(), 17U);
  // The empty chart has no pending note, so the press must still sound when
  // the judgement switch is off.
  expect(std::memcmp(free_block.data(), engine_reference_bytes.data(),
                     free_block.size()) != 0,
         "C ABI free-play press was silent");
  otgy_playback_judgement free_judgements[1]{};
  std::uint32_t free_judgement_count = 0U;
  otgy_playback_engine_drain_judgements(
      free_engine.get(), free_judgements, 1U, &free_judgement_count);
  expect(free_judgement_count == 0U,
         "C ABI free-play press produced a judgement");

  auto invalid_free_config = free_engine_config;
  invalid_free_config.judgement_enabled = 2U;
  otgy_playback_engine* invalid_engine = nullptr;
  expect(otgy_playback_engine_create(abi_pcm, chart.get(), bank.get(),
                                     invalid_free_config, &invalid_engine,
                                     &raw_error) ==
             OTGY_STATUS_INVALID_ARGUMENT &&
             invalid_engine == nullptr,
         "C ABI invalid judgement switch was accepted");
  otgy_error_destroy(raw_error);
  raw_error = nullptr;

  otgy_playback_engine_push_trigger(engine.get(),
                                    OTGY_PLAYBACK_SOUND_F1_2);
  otgy_playback_sound_bank_destroy(nullptr);

  const auto direct_position = otgy::core::start_playback(
      static_cast<double>(direct_pcm.frame_count()) /
          direct_pcm.sample_rate(),
      kStart);
  expect(static_cast<bool>(direct_position), "direct playback start failed");
  const auto direct_advanced = otgy::core::advance_playback_frames(
      *direct_position.position(), 17U, direct_pcm.sample_rate());
  otgy_playback_position abi_advanced{};
  const otgy_playback_position abi_start{
      static_cast<std::uint32_t>(direct_position.position()->phase()),
      direct_position.position()->timeline_seconds(),
      direct_position.position()->audio_duration_seconds()};
  expect(otgy_advance_playback_frames(abi_start, 17U,
                                      direct_pcm.sample_rate(),
                                      &abi_advanced, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI frame playback advance failed");
  expect(abi_advanced.phase ==
                 static_cast<std::uint32_t>(
                     direct_advanced.position()->phase()) &&
             same_double(abi_advanced.timeline_seconds,
                         direct_advanced.position()->timeline_seconds()),
         "frame playback position differs");

  const auto direct_error = otgy::core::render_playback_pcm(
      direct_pcm, std::numeric_limits<double>::quiet_NaN());
  raw_pcm = reinterpret_cast<otgy_playback_pcm*>(1);
  expect(otgy_render_playback_pcm(
             abi_pcm, std::numeric_limits<double>::quiet_NaN(), &raw_pcm,
             &raw_error) == OTGY_STATUS_CORE_ERROR,
         "C ABI invalid playback PCM status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(raw_pcm == nullptr, "failed playback PCM exposed ownership");
  expect_error(error.get(), OTGY_ERROR_DOMAIN_PLAYBACK_PCM,
               static_cast<std::uint32_t>(direct_error.error()),
               otgy::core::playback_pcm_error_message(direct_error.error()));
  otgy_playback_pcm_destroy(nullptr);
}

void test_pcm_resample_abi(const otgy_pcm* pcm) {
  otgy_error* raw_error = nullptr;
  otgy_pcm* raw_output = nullptr;
  const auto source_frames = otgy_pcm_get_frame_count(pcm);
  const auto source_rate = otgy_pcm_get_sample_rate(pcm);
  expect(source_frames > 0U && source_rate > 0U,
         "missing PCM for resampling");
  expect(otgy_pcm_resample(pcm, 48000U, &raw_output, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI PCM resample failed");
  auto output = handle<otgy_pcm, otgy_pcm_destroy>(raw_output);
  expect(output != nullptr, "C ABI PCM resample exposed no handle");
  expect(otgy_pcm_get_sample_rate(output.get()) == 48000U,
         "C ABI PCM resample rate differs");
  const auto expected_frames =
      (source_frames * 48000U + source_rate / 2U) / source_rate;
  expect(otgy_pcm_get_frame_count(output.get()) == expected_frames,
         "C ABI PCM resample frame count differs");

  raw_output = nullptr;
  expect(otgy_pcm_resample(pcm, 0U, &raw_output, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "C ABI invalid PCM resample status differs");
  expect(raw_output == nullptr, "failed PCM resample exposed ownership");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_PCM,
               static_cast<std::uint32_t>(otgy::core::PcmError::InvalidSampleRate),
               otgy::core::pcm_error_message(
                   otgy::core::PcmError::InvalidSampleRate));
}

void test_judgement_windows_abi() {
  otgy_error* raw_error = nullptr;
  const auto check_config = [&](double bpm, std::uint32_t basis,
                                double perfect, double great, double cool,
                                double bad) {
    otgy_playback_judgement_windows abi{};
    expect(otgy_playback_judgement_windows_for(
               bpm, basis, perfect, great, cool, bad, &abi, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI judgement windows failed");
    const auto direct = otgy::core::judgement_windows_ms(
        {{bpm, 0.0},
         static_cast<otgy::core::JudgementWindowBasis>(basis),
         perfect,
         great,
         cool,
         bad,
         0.0});
    expect(direct.has_value(), "direct judgement windows failed");
    expect(same_double(abi.unit_ms, direct->unit) &&
               same_double(abi.perfect_ms, direct->perfect) &&
               same_double(abi.great_ms, direct->great) &&
               same_double(abi.cool_ms, direct->cool) &&
               same_double(abi.bad_ms, direct->bad),
           "C ABI judgement windows differ");
  };
  check_config(80.0, OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE, 0.125,
               0.25, 0.375, 0.5);
  check_config(160.0, OTGY_PLAYBACK_JUDGEMENT_BASIS_HALF_BEAT, 0.125, 0.25,
               0.375, 0.5);

  otgy_playback_judgement_windows invalid{};
  invalid.unit_ms = 1.0;
  expect(otgy_playback_judgement_windows_for(
             120.0, OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE, 0.5, 0.25,
             0.375, 0.5, &invalid, &raw_error) == OTGY_STATUS_CORE_ERROR,
         "C ABI invalid judgement windows status differs");
  expect(invalid.unit_ms == 0.0 && invalid.bad_ms == 0.0,
         "C ABI invalid judgement windows were not cleared");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(
      error.get(), OTGY_ERROR_DOMAIN_PLAYBACK_ENGINE,
      static_cast<std::uint32_t>(
          otgy::core::PlaybackEngineError::InvalidConfiguration),
      otgy::core::playback_engine_error_message(
          otgy::core::PlaybackEngineError::InvalidConfiguration));
  raw_error = nullptr;
  expect(otgy_playback_judgement_windows_for(
             120.0, OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE, 0.125, 0.25,
             0.375, 0.5, nullptr, &raw_error) == OTGY_STATUS_INVALID_ARGUMENT,
         "C ABI null judgement windows status differs");
  otgy_error_destroy(raw_error);
}

}  // namespace

int main() {
  try {
    expect(otgy_c_api_version() == OTGY_C_API_VERSION,
           "C ABI version differs");
    const auto core_version = otgy_core_version();
    expect(std::string_view(core_version.data,
                            static_cast<std::size_t>(core_version.length)) ==
               otgy::core::version_string(),
           "core version differs");
    const auto mp3 = read_file(OTGY_C_API_MP3_FIXTURE);
    const auto ogg = read_file(OTGY_C_API_OGG_FIXTURE);
    auto initial_pcm = otgy::core::Pcm16::allocate(1U, 0U);
    expect(static_cast<bool>(initial_pcm), "unable to make initial PCM");
    auto direct_pcm = std::move(initial_pcm).take_pcm();
    auto abi_pcm = handle<otgy_pcm, otgy_pcm_destroy>(nullptr);
    otgy::core::AudioSourceInfo source_info{
        otgy::core::AudioSourceCodec::OggVorbis, 0U, 0U};
    test_pcm_decode_hash_and_errors(mp3, ogg, direct_pcm, abi_pcm,
                                    source_info);
    test_offset_names_quality_and_export(direct_pcm, abi_pcm.get(),
                                         source_info);
    test_audio_end(direct_pcm, abi_pcm.get(), source_info);
    test_waveforms(direct_pcm, abi_pcm.get());
    test_analysis(direct_pcm, abi_pcm.get());
    test_playback(direct_pcm, abi_pcm.get());
    test_pcm_resample_abi(abi_pcm.get());
    test_judgement_windows_abi();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
