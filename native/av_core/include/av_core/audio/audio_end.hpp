// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

/// Chooses where the exported audio ends.
///
/// `SourceEnd` keeps the decoded source length after the Offset head shift.
/// `Beat` ends at a chart beat, so the exported duration is
/// `(sixteenth_row / 4) * (60 / bpm)` seconds and does not subtract Offset.
enum class AudioEndMode : std::uint8_t {
  SourceEnd = 0,
  Beat = 1,
};

struct AudioEndSpec final {
  AudioEndMode mode;
  std::uint64_t sixteenth_row;
};

enum class AudioEndError : std::uint8_t {
  None = 0,
  InvalidBpm = 1,
  InvalidBeatRow = 2,
  EmptyOutput = 3,
  SizeOverflow = 4,
  AllocationFailure = 5,
  InvalidSampleRate = 6,
};

[[nodiscard]] constexpr std::string_view audio_end_error_message(
    AudioEndError error) noexcept {
  switch (error) {
    case AudioEndError::None:
      return {};
    case AudioEndError::InvalidBpm:
      return "audio end requires a finite BPM between 0 and 100000";
    case AudioEndError::InvalidBeatRow:
      return "audio end beat row must be a non-negative multiple of four";
    case AudioEndError::EmptyOutput:
      return "audio end position leaves no source audio";
    case AudioEndError::SizeOverflow:
      return "audio end output exceeds the representable size";
    case AudioEndError::AllocationFailure:
      return "unable to allocate the audio end result";
    case AudioEndError::InvalidSampleRate:
      return "audio end sample rate must be greater than zero";
  }
  return "unknown audio end error";
}

struct AudioEndPlan final {
  /// Final exported frame count after the head shift and tail adjustment.
  std::size_t output_frame_count;
};

class AudioEndPlanResult final {
 public:
  explicit AudioEndPlanResult(AudioEndPlan plan) noexcept;
  explicit AudioEndPlanResult(AudioEndError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const AudioEndPlan* plan() const noexcept;
  [[nodiscard]] AudioEndError error() const noexcept;

 private:
  std::variant<AudioEndPlan, AudioEndError> storage_;
};

class AudioEndSecondsResult final {
 public:
  explicit AudioEndSecondsResult(double seconds) noexcept;
  explicit AudioEndSecondsResult(AudioEndError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const double* seconds() const noexcept;
  [[nodiscard]] AudioEndError error() const noexcept;

 private:
  std::variant<double, AudioEndError> storage_;
};

/// Converts a custom beat row to the exported duration in seconds.
///
/// The result is the exact chart projection used by the exporter:
/// `(row / 4) * (60 / bpm)`. It deliberately does not subtract Offset.
[[nodiscard]] AudioEndSecondsResult calculate_audio_end_export_seconds(
    std::uint64_t sixteenth_row, double bpm) noexcept;

/// Resolves the export length for a source or beat end.
///
/// `after_offset_frame_count` is the length produced by the Offset head
/// transform. A custom beat end must preserve at least one source frame after
/// the head shift; otherwise the result is `EmptyOutput`.
[[nodiscard]] AudioEndPlanResult calculate_audio_end_plan(
    std::uint32_t sample_rate, std::size_t source_frame_count,
    std::int64_t signed_shift_frames,
    std::size_t after_offset_frame_count, double bpm,
    AudioEndSpec spec) noexcept;

/// Builds the final exported PCM in one allocation.
///
/// Output frame `i` copies source frame `i - signed_shift_frames` when it is
/// inside the source, and writes silence otherwise. This is the Offset head
/// transform and the custom tail length in a single pass.
[[nodiscard]] PcmResult apply_audio_end_window(
    const Pcm16& pcm, std::int64_t signed_shift_frames,
    std::size_t output_frame_count) noexcept;

}  // namespace otgy::core
