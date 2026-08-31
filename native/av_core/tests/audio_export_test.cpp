// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using otgy::core::AudioExportProgress;
using otgy::core::AudioExportSnapshotError;
using otgy::core::AudioExportStage;
using otgy::core::AudioExportTaskError;
using otgy::core::AudioSourceCodec;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;

[[nodiscard]] Pcm16 make_pcm() {
  auto result = Pcm16::allocate(8'000U, 4'096U);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  for (std::size_t frame = 0U; frame < pcm.frame_count(); ++frame) {
    pcm.mutable_channel_data(PcmChannel::Left)[frame] =
        static_cast<Pcm16::Sample>(frame % 2U == 0U ? 10'000 : -10'000);
    pcm.mutable_channel_data(PcmChannel::Right)[frame] =
        static_cast<Pcm16::Sample>(frame % 3U == 0U ? 8'000 : -8'000);
  }
  return pcm;
}

void test_snapshot_owns_all_inputs() {
  auto pcm = make_pcm();
  otgy::core::OggVorbisMetadata metadata{"Title", "Artist", "Album"};
  auto result = otgy::core::create_audio_export_snapshot(
      pcm, 42U, 0.0000625, 3U,
      {AudioSourceCodec::Mp3, 0U, 0U}, " song ", metadata);
  assert(result);
  assert(result.snapshot()->pcm_identity() == 42U);
  assert(result.snapshot()->quality() == 3U);
  assert(result.snapshot()->base_name() == "song");
  assert(result.snapshot()->offset_plan().signed_shift_frames == 1);
  assert(result.snapshot()->offset_plan().output_frame_count == 4'097U);

  pcm.mutable_channel_data(PcmChannel::Left)[0U] = 123;
  metadata.title = "Changed";
  assert(result.snapshot()->pcm().channel_data(PcmChannel::Left)[0U] ==
         10'000);
  assert(result.snapshot()->metadata().title == "Title");

  const auto copy = *result.snapshot();
  auto moved = std::move(result).take_snapshot();
  assert(copy.pcm().channel_data(PcmChannel::Left) !=
         moved.pcm().channel_data(PcmChannel::Left));
  assert(copy.metadata().title == moved.metadata().title);
}

void test_snapshot_validation() {
  const auto pcm = make_pcm();
  assert(otgy::core::create_audio_export_snapshot(
             pcm, 1U, 0.0, 0U, {AudioSourceCodec::Mp3, 0U, 0U}, "x")
             .error() == AudioExportSnapshotError::InvalidQuality);

  const auto baseline = otgy::core::calculate_audio_export_quality(
      pcm.sample_rate(), {AudioSourceCodec::Mp3, 0U, 0U});
  assert(baseline);
  const auto low_source = baseline.policy()->nominal_bitrates_bps[0U];
  assert(otgy::core::create_audio_export_snapshot(
             pcm, 1U, 0.0, 2U,
             {AudioSourceCodec::Mp3, low_source, 0U}, "x")
             .error() == AudioExportSnapshotError::ExceedsSourceQuality);

  assert(otgy::core::create_audio_export_snapshot(
             pcm, 1U, -1.0, 3U, {AudioSourceCodec::Mp3, 0U, 0U}, "x")
             .error() == AudioExportSnapshotError::RemovesAllFrames);
  assert(otgy::core::create_audio_export_snapshot(
             pcm, 1U, 30.001, 3U, {AudioSourceCodec::Mp3, 0U, 0U}, "x")
             .error() == AudioExportSnapshotError::InvalidOffset);
}

struct ProgressState final {
  std::array<AudioExportProgress, 16> values{};
  std::size_t count = 0U;
  bool cancel = false;
};

void capture_progress(AudioExportProgress progress, void* context) noexcept {
  auto& state = *static_cast<ProgressState*>(context);
  assert(state.count < state.values.size());
  state.values[state.count++] = progress;
  if (progress.stage == AudioExportStage::Encoding &&
      progress.completed_units >= 1'024U) {
    state.cancel = true;
  }
}

bool query_cancel(void* context) noexcept {
  return static_cast<const ProgressState*>(context)->cancel;
}

void test_task_progress_cancel_and_output() {
  const auto pcm = make_pcm();
  const auto snapshot_result = otgy::core::create_audio_export_snapshot(
      pcm, 99U, -0.0000625, 3U,
      {AudioSourceCodec::OggVorbis, 0U, 0U}, "audio");
  assert(snapshot_result);

  ProgressState completed{};
  auto completed_result = otgy::core::encode_audio_export_snapshot(
      *snapshot_result.snapshot(), capture_progress, nullptr, &completed);
  assert(completed_result);
  assert(completed_result.output()->signed_shift_frames == -1);
  assert(completed_result.output()->output_frame_count == 4'095U);
  assert(completed.count >= 4U);
  assert(completed.values[0U].stage == AudioExportStage::PreparingAudio);
  assert(completed.values[1U].stage == AudioExportStage::PreparingAudio);
  assert(completed.values[2U].stage == AudioExportStage::Encoding);

  const auto decoded = otgy::core::decode_ogg_vorbis(
      completed_result.output()->bytes.data(),
      completed_result.output()->bytes.size());
  assert(decoded);
  assert(decoded.pcm()->frame_count() == 4'095U);

  ProgressState cancelled{};
  const auto cancelled_result = otgy::core::encode_audio_export_snapshot(
      *snapshot_result.snapshot(), capture_progress, query_cancel, &cancelled);
  assert(!cancelled_result);
  assert(cancelled_result.output() == nullptr);
  assert(cancelled_result.error() == AudioExportTaskError::Cancelled);
}

void test_error_messages() {
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  AudioExportSnapshotError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::audio_export_snapshot_error_message(
        static_cast<AudioExportSnapshotError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  AudioExportTaskError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::audio_export_task_error_message(
        static_cast<AudioExportTaskError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
}

}  // namespace

int main() {
  test_snapshot_owns_all_inputs();
  test_snapshot_validation();
  test_task_progress_cancel_and_output();
  test_error_messages();
  return 0;
}
