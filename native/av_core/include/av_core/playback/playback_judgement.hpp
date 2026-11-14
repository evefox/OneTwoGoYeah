// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/playback/playback_plan.hpp"
#include "av_core/timing/timing_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace otgy::core {

enum class JudgementGrade : std::uint8_t {
  Perfect = 0,
  Great = 1,
  Cool = 2,
  Bad = 3,
  Miss = 4,
};

/// The window unit is either one 1/16 note (four per beat) or half a beat.
/// The application default is the half-beat unit.
enum class JudgementWindowBasis : std::uint8_t {
  SixteenthNote = 0,
  HalfBeat = 1,
};

struct JudgementConfig final {
  TimingParameters timing;
  JudgementWindowBasis basis;
  double perfect_fraction;
  double great_fraction;
  double cool_fraction;
  double bad_fraction;
  /// Positive values make a press appear earlier by this many milliseconds.
  /// This is an internal calibration value, not a user-facing preference.
  double offset_ms;
};

[[nodiscard]] constexpr JudgementConfig default_judgement_config(
    TimingParameters timing) noexcept {
  return JudgementConfig{timing,
                         JudgementWindowBasis::HalfBeat,
                         0.125,
                         0.25,
                         0.375,
                         0.5,
                         0.0};
}

struct JudgementWindowsMs final {
  double unit;
  double perfect;
  double great;
  double cool;
  double bad;
};

struct JudgementResult final {
  JudgementGrade grade;
  /// Positive means the press was late relative to the note.
  double delta_ms;
  ChartEventId note_id;
  PlaybackSoundId expected_sound;
  PlaybackSoundId pressed_sound;
  /// Song timeline of the matched note, used by the tuning log.
  double note_seconds;
  /// Song timeline at which the press was judged.
  double press_seconds;
  /// Time between the key entering the lock-free queue and the audio thread
  /// consuming it, in milliseconds. Diagnostic only, never used for grading.
  double queue_delay_ms;
};

enum class JudgementError : std::uint8_t {
  None = 0,
  InvalidConfiguration = 1,
  AllocationFailure = 2,
};

/// Computes the four symmetric timing windows in milliseconds. Returns nullopt
/// when the BPM, basis, fractions or offset are invalid.
[[nodiscard]] std::optional<JudgementWindowsMs> judgement_windows_ms(
    const JudgementConfig& config) noexcept;

/// Grades one press against the windows. Values outside `bad` return Miss.
[[nodiscard]] JudgementGrade grade_for_delta(
    double delta_ms, const JudgementWindowsMs& windows) noexcept;

/// Allocation-free after reset. The session owns a sorted copy of the plan's
/// player notes and keeps one cursor into the first unjudged note.
class JudgementSession final {
 public:
  JudgementSession() noexcept = default;

  /// Copies and sorts the plan's player notes and precomputes note times.
  /// Must be called before press/advance; it may allocate.
  [[nodiscard]] JudgementError reset(const PlaybackPlan& plan,
                                     const JudgementConfig& config) noexcept;

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] const JudgementWindowsMs& windows() const noexcept {
    return windows_;
  }

  /// Matches a press against the nearest unjudged note whose absolute delta is
  /// within the Bad window. Returns nullopt when no note is pending in range;
  /// in that case the caller must not play a sound or create a judgement.
  [[nodiscard]] std::optional<JudgementResult> press(
      double press_seconds, PlaybackSoundId sound) noexcept;

  /// Marks notes whose late Bad deadline has passed as Miss. Writes up to
  /// `capacity` results into `output` and returns how many were written.
  /// Passing a null output (or zero capacity) advances silently.
  [[nodiscard]] std::size_t advance(double now_seconds,
                                    JudgementResult* output,
                                    std::size_t capacity) noexcept;

 private:
  struct NoteState final {
    PlaybackPlayerNote note;
    double seconds;
    bool judged;
  };

  std::vector<NoteState> notes_;
  JudgementConfig config_{};
  JudgementWindowsMs windows_{};
  std::size_t cursor_ = 0U;
  bool initialized_ = false;
};

}  // namespace otgy::core
