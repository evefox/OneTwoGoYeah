// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/audio_export_quality.hpp"
#include "av_core/audio/ogg_vorbis_encoder.hpp"
#include "av_core/audio/pcm_offset.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class AudioExportSnapshotError : std::uint8_t {
  None = 0,
  InvalidSourceInfo = 1,
  UnsupportedSampleRate = 2,
  InvalidQuality = 3,
  ExceedsSourceQuality = 4,
  InvalidOffset = 5,
  RemovesAllFrames = 6,
  SizeOverflow = 7,
  AllocationFailure = 8,
};

[[nodiscard]] constexpr std::string_view audio_export_snapshot_error_message(
    AudioExportSnapshotError error) noexcept {
  switch (error) {
    case AudioExportSnapshotError::None:
      return {};
    case AudioExportSnapshotError::InvalidSourceInfo:
      return "audio export source information is invalid";
    case AudioExportSnapshotError::UnsupportedSampleRate:
      return "audio export sample rate is unsupported";
    case AudioExportSnapshotError::InvalidQuality:
      return "audio export quality is invalid";
    case AudioExportSnapshotError::ExceedsSourceQuality:
      return "audio export quality exceeds the source ceiling";
    case AudioExportSnapshotError::InvalidOffset:
      return "audio export offset is invalid";
    case AudioExportSnapshotError::RemovesAllFrames:
      return "audio export offset removes all PCM frames";
    case AudioExportSnapshotError::SizeOverflow:
      return "audio export snapshot dimensions exceed the representable size";
    case AudioExportSnapshotError::AllocationFailure:
      return "unable to allocate the audio export snapshot";
  }
  return "unknown audio export snapshot error";
}

class AudioExportSnapshot;
class AudioExportSnapshotResult;

[[nodiscard]] AudioExportSnapshotResult create_audio_export_snapshot(
    const Pcm16& pcm, std::uint64_t pcm_identity, double offset_seconds,
    std::uint8_t quality, AudioSourceInfo source_info,
    std::string_view requested_base_name,
    const OggVorbisMetadata& metadata = {}) noexcept;

class AudioExportSnapshot final {
 public:
  AudioExportSnapshot(const AudioExportSnapshot&) = default;
  AudioExportSnapshot& operator=(const AudioExportSnapshot&) = default;
  AudioExportSnapshot(AudioExportSnapshot&&) noexcept = default;
  AudioExportSnapshot& operator=(AudioExportSnapshot&&) noexcept = default;
  ~AudioExportSnapshot() = default;

  [[nodiscard]] const Pcm16& pcm() const noexcept { return pcm_; }
  [[nodiscard]] std::uint64_t pcm_identity() const noexcept {
    return pcm_identity_;
  }
  [[nodiscard]] double offset_seconds() const noexcept {
    return offset_seconds_;
  }
  [[nodiscard]] std::uint8_t quality() const noexcept { return quality_; }
  [[nodiscard]] AudioSourceInfo source_info() const noexcept {
    return source_info_;
  }
  [[nodiscard]] const std::string& base_name() const noexcept {
    return base_name_;
  }
  [[nodiscard]] const OggVorbisMetadata& metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] PcmOffsetPlan offset_plan() const noexcept {
    return offset_plan_;
  }

 private:
  friend AudioExportSnapshotResult create_audio_export_snapshot(
      const Pcm16&, std::uint64_t, double, std::uint8_t, AudioSourceInfo,
      std::string_view, const OggVorbisMetadata&) noexcept;

  AudioExportSnapshot(Pcm16 pcm, std::uint64_t pcm_identity,
                      double offset_seconds, std::uint8_t quality,
                      AudioSourceInfo source_info, std::string base_name,
                      OggVorbisMetadata metadata,
                      PcmOffsetPlan offset_plan) noexcept;

  Pcm16 pcm_;
  std::uint64_t pcm_identity_;
  double offset_seconds_;
  std::uint8_t quality_;
  AudioSourceInfo source_info_;
  std::string base_name_;
  OggVorbisMetadata metadata_;
  PcmOffsetPlan offset_plan_;
};

class AudioExportSnapshotResult final {
 public:
  explicit AudioExportSnapshotResult(AudioExportSnapshot snapshot) noexcept;
  explicit AudioExportSnapshotResult(AudioExportSnapshotError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const AudioExportSnapshot* snapshot() const noexcept;
  [[nodiscard]] AudioExportSnapshotError error() const noexcept;
  [[nodiscard]] AudioExportSnapshot take_snapshot() &&;

 private:
  std::variant<AudioExportSnapshot, AudioExportSnapshotError> storage_;
};

enum class AudioExportStage : std::uint8_t {
  PreparingAudio = 1,
  Encoding = 2,
  Saving = 3,
};

struct AudioExportProgress final {
  AudioExportStage stage;
  std::size_t completed_units;
  std::size_t total_units;
};

using AudioExportProgressCallback = void (*)(AudioExportProgress progress,
                                              void* context) noexcept;
using AudioExportCancelCallback = bool (*)(void* context) noexcept;

enum class AudioExportTaskError : std::uint8_t {
  None = 0,
  Cancelled = 1,
  OffsetTransformFailed = 2,
  OggEncodingFailed = 3,
  SizeOverflow = 4,
  AllocationFailure = 5,
};

[[nodiscard]] constexpr std::string_view audio_export_task_error_message(
    AudioExportTaskError error) noexcept {
  switch (error) {
    case AudioExportTaskError::None:
      return {};
    case AudioExportTaskError::Cancelled:
      return "audio export was cancelled";
    case AudioExportTaskError::OffsetTransformFailed:
      return "audio export PCM offset transform failed";
    case AudioExportTaskError::OggEncodingFailed:
      return "audio export Ogg encoding failed";
    case AudioExportTaskError::SizeOverflow:
      return "audio export output exceeds the representable size";
    case AudioExportTaskError::AllocationFailure:
      return "unable to allocate audio export task state";
  }
  return "unknown audio export task error";
}

struct AudioExportBytes final {
  std::vector<std::uint8_t> bytes;
  std::int64_t signed_shift_frames;
  std::size_t output_frame_count;
};

class AudioExportTaskResult final {
 public:
  explicit AudioExportTaskResult(AudioExportBytes output) noexcept;
  explicit AudioExportTaskResult(AudioExportTaskError error,
                                 PcmOffsetError offset_error,
                                 OggVorbisEncodeError encode_error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const AudioExportBytes* output() const noexcept;
  [[nodiscard]] AudioExportTaskError error() const noexcept;
  [[nodiscard]] PcmOffsetError offset_error() const noexcept;
  [[nodiscard]] OggVorbisEncodeError encode_error() const noexcept;
  [[nodiscard]] AudioExportBytes take_output() &&;

 private:
  std::variant<AudioExportBytes, AudioExportTaskError> storage_;
  PcmOffsetError offset_error_;
  OggVorbisEncodeError encode_error_;
};

/// Applies the captured Offset and encodes only the immutable snapshot.
[[nodiscard]] AudioExportTaskResult encode_audio_export_snapshot(
    const AudioExportSnapshot& snapshot,
    AudioExportProgressCallback progress_callback = nullptr,
    AudioExportCancelCallback cancel_callback = nullptr,
    void* callback_context = nullptr) noexcept;

}  // namespace otgy::core
