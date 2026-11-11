// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export.hpp"

#include "av_core/audio/audio_export_name.hpp"

#include <new>
#include <optional>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] AudioExportSnapshotError map_quality_error(
    AudioExportQualityError error) noexcept {
  switch (error) {
    case AudioExportQualityError::InvalidSourceInfo:
      return AudioExportSnapshotError::InvalidSourceInfo;
    case AudioExportQualityError::UnsupportedSampleRate:
    case AudioExportQualityError::EncoderInitializationFailed:
      return AudioExportSnapshotError::UnsupportedSampleRate;
    case AudioExportQualityError::InvalidQuality:
      return AudioExportSnapshotError::InvalidQuality;
    case AudioExportQualityError::ExceedsSourceQuality:
      return AudioExportSnapshotError::ExceedsSourceQuality;
    case AudioExportQualityError::None:
      return AudioExportSnapshotError::None;
  }
  return AudioExportSnapshotError::InvalidSourceInfo;
}

[[nodiscard]] AudioExportSnapshotError map_offset_snapshot_error(
    PcmOffsetError error) noexcept {
  switch (error) {
    case PcmOffsetError::InvalidOffset:
    case PcmOffsetError::InvalidSampleRate:
      return AudioExportSnapshotError::InvalidOffset;
    case PcmOffsetError::RemovesAllFrames:
      return AudioExportSnapshotError::RemovesAllFrames;
    case PcmOffsetError::SizeOverflow:
      return AudioExportSnapshotError::SizeOverflow;
    case PcmOffsetError::AllocationFailure:
      return AudioExportSnapshotError::AllocationFailure;
    case PcmOffsetError::None:
      return AudioExportSnapshotError::None;
  }
  return AudioExportSnapshotError::InvalidOffset;
}

[[nodiscard]] AudioExportSnapshotError map_end_snapshot_error(
    AudioEndError error) noexcept {
  switch (error) {
    case AudioEndError::InvalidBpm:
    case AudioEndError::InvalidBeatRow:
    case AudioEndError::InvalidSampleRate:
      return AudioExportSnapshotError::InvalidEnd;
    case AudioEndError::EmptyOutput:
      return AudioExportSnapshotError::EmptyOutputAfterEnd;
    case AudioEndError::SizeOverflow:
      return AudioExportSnapshotError::SizeOverflow;
    case AudioEndError::AllocationFailure:
      return AudioExportSnapshotError::AllocationFailure;
    case AudioEndError::None:
      return AudioExportSnapshotError::None;
  }
  return AudioExportSnapshotError::InvalidEnd;
}

[[nodiscard]] bool cancelled(AudioExportCancelCallback callback,
                             void* context) noexcept {
  return callback != nullptr && callback(context);
}

void report(AudioExportProgressCallback callback, void* context,
            AudioExportStage stage, std::size_t completed,
            std::size_t total) noexcept {
  if (callback != nullptr) {
    callback({stage, completed, total}, context);
  }
}

struct EncodeCallbackBridge final {
  AudioExportProgressCallback progress_callback;
  AudioExportCancelCallback cancel_callback;
  void* user_context;
};

void bridge_progress(OggVorbisEncodeProgress progress,
                     void* context) noexcept {
  const auto& bridge = *static_cast<const EncodeCallbackBridge*>(context);
  report(bridge.progress_callback, bridge.user_context,
         AudioExportStage::Encoding, progress.completed_frames,
         progress.total_frames);
}

bool bridge_cancel(void* context) noexcept {
  const auto& bridge = *static_cast<const EncodeCallbackBridge*>(context);
  return cancelled(bridge.cancel_callback, bridge.user_context);
}

[[nodiscard]] AudioExportTaskResult task_failure_from_offset(
    PcmOffsetError error) noexcept {
  if (error == PcmOffsetError::AllocationFailure) {
    return AudioExportTaskResult{AudioExportTaskError::AllocationFailure,
                                 error, OggVorbisEncodeError::None};
  }
  if (error == PcmOffsetError::SizeOverflow) {
    return AudioExportTaskResult{AudioExportTaskError::SizeOverflow, error,
                                 OggVorbisEncodeError::None};
  }
  return AudioExportTaskResult{AudioExportTaskError::OffsetTransformFailed,
                               error, OggVorbisEncodeError::None};
}

[[nodiscard]] AudioExportTaskResult task_failure_from_pcm(
    PcmError error) noexcept {
  if (error == PcmError::AllocationFailure) {
    return AudioExportTaskResult{AudioExportTaskError::AllocationFailure,
                                 PcmOffsetError::None,
                                 OggVorbisEncodeError::None};
  }
  if (error == PcmError::SizeOverflow) {
    return AudioExportTaskResult{AudioExportTaskError::SizeOverflow,
                                 PcmOffsetError::None,
                                 OggVorbisEncodeError::None};
  }
  return AudioExportTaskResult{AudioExportTaskError::OffsetTransformFailed,
                               PcmOffsetError::None,
                               OggVorbisEncodeError::None};
}

[[nodiscard]] AudioExportTaskResult task_failure_from_encode(
    OggVorbisEncodeError error) noexcept {
  if (error == OggVorbisEncodeError::Cancelled) {
    return AudioExportTaskResult{AudioExportTaskError::Cancelled,
                                 PcmOffsetError::None, error};
  }
  if (error == OggVorbisEncodeError::AllocationFailure) {
    return AudioExportTaskResult{AudioExportTaskError::AllocationFailure,
                                 PcmOffsetError::None, error};
  }
  if (error == OggVorbisEncodeError::SizeOverflow) {
    return AudioExportTaskResult{AudioExportTaskError::SizeOverflow,
                                 PcmOffsetError::None, error};
  }
  return AudioExportTaskResult{AudioExportTaskError::OggEncodingFailed,
                               PcmOffsetError::None, error};
}

}  // namespace

AudioExportSnapshot::AudioExportSnapshot(
    Pcm16 pcm, std::uint64_t pcm_identity, double offset_seconds,
    std::uint8_t quality, AudioSourceInfo source_info, std::string base_name,
    OggVorbisMetadata metadata, PcmOffsetPlan offset_plan,
    AudioEndPlan end_plan, bool custom_end) noexcept
    : pcm_(std::move(pcm)),
      pcm_identity_(pcm_identity),
      offset_seconds_(offset_seconds),
      quality_(quality),
      source_info_(source_info),
      base_name_(std::move(base_name)),
      metadata_(std::move(metadata)),
      offset_plan_(offset_plan),
      end_plan_(end_plan),
      custom_end_(custom_end) {}

AudioExportSnapshotResult::AudioExportSnapshotResult(
    AudioExportSnapshot snapshot) noexcept
    : storage_(std::move(snapshot)) {}

AudioExportSnapshotResult::AudioExportSnapshotResult(
    AudioExportSnapshotError error) noexcept
    : storage_(error) {}

AudioExportSnapshotResult::operator bool() const noexcept {
  return std::holds_alternative<AudioExportSnapshot>(storage_);
}

const AudioExportSnapshot* AudioExportSnapshotResult::snapshot() const noexcept {
  return std::get_if<AudioExportSnapshot>(&storage_);
}

AudioExportSnapshotError AudioExportSnapshotResult::error() const noexcept {
  const auto* error = std::get_if<AudioExportSnapshotError>(&storage_);
  return error == nullptr ? AudioExportSnapshotError::None : *error;
}

AudioExportSnapshot AudioExportSnapshotResult::take_snapshot() && {
  return std::move(std::get<AudioExportSnapshot>(storage_));
}

AudioExportSnapshotResult create_audio_export_snapshot(
    const Pcm16& pcm, std::uint64_t pcm_identity, double offset_seconds,
    std::uint8_t quality, AudioSourceInfo source_info,
    std::string_view requested_base_name,
    const OggVorbisMetadata& metadata) noexcept {
  return create_audio_export_snapshot_with_end(
      pcm, pcm_identity, offset_seconds, 100.0, AudioEndSpec{
                                                AudioEndMode::SourceEnd, 0U},
      quality, source_info, requested_base_name, metadata);
}

AudioExportSnapshotResult create_audio_export_snapshot_with_end(
    const Pcm16& pcm, std::uint64_t pcm_identity, double offset_seconds,
    double bpm, AudioEndSpec end, std::uint8_t quality,
    AudioSourceInfo source_info, std::string_view requested_base_name,
    const OggVorbisMetadata& metadata) noexcept {
  const auto policy =
      calculate_audio_export_quality(pcm.sample_rate(), source_info);
  if (!policy) {
    return AudioExportSnapshotResult{map_quality_error(policy.error())};
  }
  const auto quality_error =
      validate_audio_export_quality(*policy.policy(), quality);
  if (quality_error != AudioExportQualityError::None) {
    return AudioExportSnapshotResult{map_quality_error(quality_error)};
  }
  const auto offset_plan = calculate_pcm_offset_plan(
      pcm.sample_rate(), pcm.frame_count(), offset_seconds);
  if (!offset_plan) {
    return AudioExportSnapshotResult{
        map_offset_snapshot_error(offset_plan.error())};
  }
  const auto end_plan = calculate_audio_end_plan(
      pcm.sample_rate(), pcm.frame_count(),
      offset_plan.plan()->signed_shift_frames,
      offset_plan.plan()->output_frame_count, bpm, end);
  if (!end_plan) {
    return AudioExportSnapshotResult{map_end_snapshot_error(end_plan.error())};
  }
  auto base_name = normalize_audio_export_base_name(requested_base_name);
  if (!base_name) {
    return AudioExportSnapshotResult{
        AudioExportSnapshotError::AllocationFailure};
  }

  try {
    return AudioExportSnapshotResult{AudioExportSnapshot{
        Pcm16{pcm}, pcm_identity, offset_seconds, quality, source_info,
        std::move(base_name).take_name(), OggVorbisMetadata{metadata},
        *offset_plan.plan(), *end_plan.plan(),
        end.mode == AudioEndMode::Beat}};
  } catch (const std::bad_alloc&) {
    return AudioExportSnapshotResult{
        AudioExportSnapshotError::AllocationFailure};
  }
}

AudioExportTaskResult::AudioExportTaskResult(AudioExportBytes output) noexcept
    : storage_(std::move(output)),
      offset_error_(PcmOffsetError::None),
      encode_error_(OggVorbisEncodeError::None) {}

AudioExportTaskResult::AudioExportTaskResult(
    AudioExportTaskError error, PcmOffsetError offset_error,
    OggVorbisEncodeError encode_error) noexcept
    : storage_(error),
      offset_error_(offset_error),
      encode_error_(encode_error) {}

AudioExportTaskResult::operator bool() const noexcept {
  return std::holds_alternative<AudioExportBytes>(storage_);
}

const AudioExportBytes* AudioExportTaskResult::output() const noexcept {
  return std::get_if<AudioExportBytes>(&storage_);
}

AudioExportTaskError AudioExportTaskResult::error() const noexcept {
  const auto* error = std::get_if<AudioExportTaskError>(&storage_);
  return error == nullptr ? AudioExportTaskError::None : *error;
}

PcmOffsetError AudioExportTaskResult::offset_error() const noexcept {
  return offset_error_;
}

OggVorbisEncodeError AudioExportTaskResult::encode_error() const noexcept {
  return encode_error_;
}

AudioExportBytes AudioExportTaskResult::take_output() && {
  return std::move(std::get<AudioExportBytes>(storage_));
}

AudioExportTaskResult encode_audio_export_snapshot(
    const AudioExportSnapshot& snapshot,
    AudioExportProgressCallback progress_callback,
    AudioExportCancelCallback cancel_callback,
    void* callback_context) noexcept {
  if (cancelled(cancel_callback, callback_context)) {
    return AudioExportTaskResult{AudioExportTaskError::Cancelled,
                                 PcmOffsetError::None,
                                 OggVorbisEncodeError::None};
  }
  report(progress_callback, callback_context,
         AudioExportStage::PreparingAudio, 0U, snapshot.pcm().frame_count());
  std::optional<Pcm16> transformed;
  if (snapshot.has_custom_end()) {
    auto windowed = apply_audio_end_window(
        snapshot.pcm(), snapshot.offset_plan().signed_shift_frames,
        snapshot.end_plan().output_frame_count);
    if (!windowed) {
      return task_failure_from_pcm(windowed.error());
    }
    transformed.emplace(std::move(windowed).take_pcm());
  } else {
    auto shifted = apply_pcm_offset(snapshot.pcm(), snapshot.offset_seconds());
    if (!shifted) {
      return task_failure_from_offset(shifted.error());
    }
    transformed.emplace(std::move(shifted).take_pcm());
  }
  report(progress_callback, callback_context,
         AudioExportStage::PreparingAudio, snapshot.pcm().frame_count(),
         snapshot.pcm().frame_count());
  if (cancelled(cancel_callback, callback_context)) {
    return AudioExportTaskResult{AudioExportTaskError::Cancelled,
                                 PcmOffsetError::None,
                                 OggVorbisEncodeError::None};
  }

  EncodeCallbackBridge bridge{progress_callback, cancel_callback,
                              callback_context};
  auto encoded = encode_ogg_vorbis(
      *transformed, snapshot.quality(), snapshot.metadata(), bridge_progress,
      bridge_cancel, &bridge);
  if (!encoded) {
    return task_failure_from_encode(encoded.error());
  }
  const auto plan = snapshot.offset_plan();
  return AudioExportTaskResult{AudioExportBytes{
      std::move(encoded).take_bytes(), plan.signed_shift_frames,
      snapshot.end_plan().output_frame_count}};
}

}  // namespace otgy::core
