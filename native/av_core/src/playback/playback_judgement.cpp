// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_judgement.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>
#include <utility>

namespace otgy::core {

std::optional<JudgementWindowsMs> judgement_windows_ms(
    const JudgementConfig& config) noexcept {
  if (!std::isfinite(config.timing.bpm) || config.timing.bpm <= 0.0) {
    return std::nullopt;
  }
  if (!std::isfinite(config.offset_ms)) {
    return std::nullopt;
  }
  if (config.basis != JudgementWindowBasis::SixteenthNote &&
      config.basis != JudgementWindowBasis::HalfBeat) {
    return std::nullopt;
  }
  const auto valid_fraction = [](double value) {
    return std::isfinite(value) && value > 0.0;
  };
  if (!valid_fraction(config.perfect_fraction) ||
      !valid_fraction(config.great_fraction) ||
      !valid_fraction(config.cool_fraction) ||
      !valid_fraction(config.bad_fraction)) {
    return std::nullopt;
  }
  if (!(config.perfect_fraction < config.great_fraction &&
        config.great_fraction < config.cool_fraction &&
        config.cool_fraction < config.bad_fraction)) {
    return std::nullopt;
  }

  const double beat_ms = 60000.0 / config.timing.bpm;
  const double unit = config.basis == JudgementWindowBasis::SixteenthNote
                          ? beat_ms / 4.0
                          : beat_ms / 2.0;
  if (!std::isfinite(unit) || unit <= 0.0) {
    return std::nullopt;
  }
  JudgementWindowsMs windows{
      unit,
      unit * config.perfect_fraction,
      unit * config.great_fraction,
      unit * config.cool_fraction,
      unit * config.bad_fraction,
  };
  if (!std::isfinite(windows.perfect) || !std::isfinite(windows.great) ||
      !std::isfinite(windows.cool) || !std::isfinite(windows.bad)) {
    return std::nullopt;
  }
  return windows;
}

JudgementGrade grade_for_delta(double delta_ms,
                               const JudgementWindowsMs& windows) noexcept {
  if (!std::isfinite(delta_ms)) {
    return JudgementGrade::Miss;
  }
  const double value = std::fabs(delta_ms);
  if (value <= windows.perfect) {
    return JudgementGrade::Perfect;
  }
  if (value <= windows.great) {
    return JudgementGrade::Great;
  }
  if (value <= windows.cool) {
    return JudgementGrade::Cool;
  }
  if (value <= windows.bad) {
    return JudgementGrade::Bad;
  }
  return JudgementGrade::Miss;
}

JudgementError JudgementSession::reset(const PlaybackPlan& plan,
                                       const JudgementConfig& config) noexcept {
  notes_.clear();
  initialized_ = false;
  const auto windows = judgement_windows_ms(config);
  if (!windows) {
    return JudgementError::InvalidConfiguration;
  }

  try {
    notes_.reserve(plan.player_notes.size());
    for (const auto& note : plan.player_notes) {
      const auto seconds = sixteenth_row_to_seconds(
          config.timing, static_cast<double>(note.row));
      if (!seconds) {
        notes_.clear();
        return JudgementError::InvalidConfiguration;
      }
      notes_.push_back({note, *seconds.value(), false});
    }
    std::sort(notes_.begin(), notes_.end(),
              [](const NoteState& left, const NoteState& right) {
                if (left.note.row != right.note.row) {
                  return left.note.row < right.note.row;
                }
                return left.note.order < right.note.order;
              });
  } catch (const std::bad_alloc&) {
    notes_.clear();
    return JudgementError::AllocationFailure;
  } catch (const std::length_error&) {
    notes_.clear();
    return JudgementError::AllocationFailure;
  }

  config_ = config;
  windows_ = *windows;
  cursor_ = 0U;
  initialized_ = true;
  return JudgementError::None;
}

std::optional<JudgementResult> JudgementSession::press(
    double press_seconds, PlaybackSoundId sound) noexcept {
  if (!initialized_ || !std::isfinite(press_seconds)) {
    return std::nullopt;
  }
  const double bad_seconds = windows_.bad / 1000.0;
  const double latest = press_seconds + bad_seconds;

  NoteState* best = nullptr;
  double best_abs = 0.0;
  for (std::size_t index = cursor_; index < notes_.size(); ++index) {
    NoteState& state = notes_[index];
    if (state.judged) {
      continue;
    }
    if (state.seconds > latest) {
      break;
    }
    const double delta_ms =
        (press_seconds - state.seconds) * 1000.0 - config_.offset_ms;
    const double absolute = std::fabs(delta_ms);
    if (absolute > windows_.bad) {
      continue;
    }
    const bool better =
        best == nullptr || absolute < best_abs ||
        (absolute == best_abs && state.seconds < best->seconds) ||
        (absolute == best_abs && state.seconds == best->seconds &&
         state.note.order < best->note.order);
    if (better) {
      best = &state;
      best_abs = absolute;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }

  best->judged = true;
  while (cursor_ < notes_.size() && notes_[cursor_].judged) {
    ++cursor_;
  }

  JudgementResult result{
      grade_for_delta((press_seconds - best->seconds) * 1000.0 -
                          config_.offset_ms,
                      windows_),
      (press_seconds - best->seconds) * 1000.0 - config_.offset_ms,
      best->note.event_id,
      best->note.expected_sound,
      sound,
      best->seconds,
      press_seconds,
  };
  if (best->note.expected_sound != sound) {
    result.grade = JudgementGrade::Miss;
  }
  return result;
}

std::size_t JudgementSession::advance(double now_seconds,
                                      JudgementResult* output,
                                      std::size_t capacity) noexcept {
  if (!initialized_ || !std::isfinite(now_seconds)) {
    return 0U;
  }
  const double bad_seconds = windows_.bad / 1000.0;
  std::size_t emitted = 0U;
  while (cursor_ < notes_.size()) {
    NoteState& state = notes_[cursor_];
    if (state.judged) {
      ++cursor_;
      continue;
    }
    if (!(now_seconds > state.seconds + bad_seconds)) {
      break;
    }
    if (output == nullptr || capacity == 0U) {
      state.judged = true;
      ++cursor_;
      continue;
    }
    if (emitted >= capacity) {
      break;
    }
    output[emitted++] = JudgementResult{
        JudgementGrade::Miss,
        (now_seconds - state.seconds) * 1000.0,
        state.note.event_id,
        state.note.expected_sound,
        state.note.expected_sound,
        state.seconds,
        now_seconds,
    };
    state.judged = true;
    ++cursor_;
  }
  return emitted;
}

}  // namespace otgy::core
