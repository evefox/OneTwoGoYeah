// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_judgement.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using otgy::core::ChartEventId;
using otgy::core::JudgementConfig;
using otgy::core::JudgementError;
using otgy::core::JudgementGrade;
using otgy::core::JudgementResult;
using otgy::core::JudgementSession;
using otgy::core::JudgementWindowBasis;
using otgy::core::JudgementWindowsMs;
using otgy::core::PlaybackPlan;
using otgy::core::PlaybackPlayerNote;
using otgy::core::PlaybackSoundId;

bool near(double left, double right) {
  return std::fabs(left - right) < 1e-9;
}

JudgementConfig config(double bpm,
                       JudgementWindowBasis basis =
                           JudgementWindowBasis::SixteenthNote,
                       double offset_ms = 0.0) {
  auto result = otgy::core::default_judgement_config({bpm, 0.0});
  result.basis = basis;
  result.offset_ms = offset_ms;
  return result;
}

PlaybackPlayerNote note(std::uint64_t row, PlaybackSoundId sound,
                        std::uint64_t id, std::size_t order) {
  return PlaybackPlayerNote{row, sound, ChartEventId{id}, order};
}

PlaybackPlan single_note_plan(std::uint64_t row, PlaybackSoundId sound) {
  PlaybackPlan plan;
  plan.player_notes.push_back(note(row, sound, 101U, 0U));
  return plan;
}

void test_windows() {
  const auto sixteenth = otgy::core::judgement_windows_ms(
      config(160.0, JudgementWindowBasis::SixteenthNote));
  assert(sixteenth);
  assert(near(sixteenth->unit, 93.75));
  assert(near(sixteenth->perfect, 11.71875));
  assert(near(sixteenth->great, 23.4375));
  assert(near(sixteenth->cool, 35.15625));
  assert(near(sixteenth->bad, 46.875));

  const auto half = otgy::core::judgement_windows_ms(
      config(160.0, JudgementWindowBasis::HalfBeat));
  assert(half);
  assert(near(half->unit, 187.5));
  assert(near(half->perfect, 23.4375));
  assert(near(half->bad, 93.75));

  const auto slow = otgy::core::judgement_windows_ms(
      config(80.0, JudgementWindowBasis::SixteenthNote));
  assert(slow);
  assert(near(slow->unit, 187.5));
  assert(near(slow->perfect, 23.4375));
  assert(near(slow->bad, 93.75));

  auto invalid = config(0.0);
  assert(!otgy::core::judgement_windows_ms(invalid));
  invalid = config(160.0);
  invalid.perfect_fraction = 0.3;
  assert(!otgy::core::judgement_windows_ms(invalid));
  invalid = config(160.0);
  invalid.offset_ms = std::nan("");
  assert(!otgy::core::judgement_windows_ms(invalid));
}

void test_grade_boundaries() {
  const auto windows = otgy::core::judgement_windows_ms(
      config(160.0, JudgementWindowBasis::SixteenthNote));
  assert(windows);
  assert(otgy::core::grade_for_delta(0.0, *windows) ==
         JudgementGrade::Perfect);
  assert(otgy::core::grade_for_delta(windows->perfect, *windows) ==
         JudgementGrade::Perfect);
  assert(otgy::core::grade_for_delta(-windows->perfect, *windows) ==
         JudgementGrade::Perfect);
  assert(otgy::core::grade_for_delta(windows->perfect + 0.001, *windows) ==
         JudgementGrade::Great);
  assert(otgy::core::grade_for_delta(windows->great, *windows) ==
         JudgementGrade::Great);
  assert(otgy::core::grade_for_delta(windows->great + 0.001, *windows) ==
         JudgementGrade::Cool);
  assert(otgy::core::grade_for_delta(windows->cool, *windows) ==
         JudgementGrade::Cool);
  assert(otgy::core::grade_for_delta(windows->cool + 0.001, *windows) ==
         JudgementGrade::Bad);
  assert(otgy::core::grade_for_delta(windows->bad, *windows) ==
         JudgementGrade::Bad);
  assert(otgy::core::grade_for_delta(windows->bad + 0.001, *windows) ==
         JudgementGrade::Miss);
  assert(otgy::core::grade_for_delta(std::nan(""), *windows) ==
         JudgementGrade::Miss);
}

void test_press_matching_and_wrong_key() {
  const auto plan = single_note_plan(16U, PlaybackSoundId::F1_2);
  const auto settings = config(160.0);
  JudgementSession session;
  assert(session.reset(plan, settings) == JudgementError::None);
  assert(session.initialized());

  // 160 BPM: row 16 is four beats = 1.5 seconds.
  auto result = session.press(1.5, PlaybackSoundId::F1_2);
  assert(result);
  assert(result->grade == JudgementGrade::Perfect);
  assert(result->note_id.value == 101U);
  assert(result->expected_sound == PlaybackSoundId::F1_2);
  assert(result->pressed_sound == PlaybackSoundId::F1_2);
  assert(near(result->delta_ms, 0.0));
  assert(!session.press(1.5, PlaybackSoundId::F1_2));

  assert(session.reset(plan, settings) == JudgementError::None);
  result = session.press(1.51, PlaybackSoundId::F1_4);
  assert(result);
  assert(result->grade == JudgementGrade::Miss);
  assert(result->pressed_sound == PlaybackSoundId::F1_4);
  assert(near(result->delta_ms, 10.0));
  assert(!session.press(1.5, PlaybackSoundId::F1_2));

  assert(session.reset(plan, settings) == JudgementError::None);
  assert(!session.press(1.6, PlaybackSoundId::F1_2));
  JudgementResult output[2]{};
  assert(session.advance(1.6, output, 2U) == 1U);
  assert(output[0].grade == JudgementGrade::Miss);
  assert(output[0].note_id.value == 101U);
  assert(!session.press(1.5, PlaybackSoundId::F1_2));
}

void test_nearest_and_tie_break() {
  PlaybackPlan tie_plan;
  tie_plan.player_notes.push_back(note(0U, PlaybackSoundId::F1_2, 1U, 0U));
  tie_plan.player_notes.push_back(note(1U, PlaybackSoundId::F1_4, 2U, 1U));
  JudgementSession session;
  assert(session.reset(tie_plan, config(80.0)) == JudgementError::None);
  // 80 BPM: row 0 = 0.0s, row 1 = 0.1875s. The midpoint is a tie at 93.75ms.
  auto result = session.press(0.09375, PlaybackSoundId::F1_2);
  assert(result);
  assert(result->note_id.value == 1U);
  assert(result->grade == JudgementGrade::Bad);

  PlaybackPlan near_plan;
  near_plan.player_notes.push_back(note(0U, PlaybackSoundId::F1_2, 3U, 0U));
  near_plan.player_notes.push_back(note(2U, PlaybackSoundId::F1_6, 4U, 1U));
  assert(session.reset(near_plan, config(80.0)) == JudgementError::None);
  // 80 BPM: row 0 = 0.0s, row 2 = 0.375s. The press at 0.3s is nearest to row 2.
  result = session.press(0.3, PlaybackSoundId::F1_6);
  assert(result);
  assert(result->note_id.value == 4U);
  assert(result->grade == JudgementGrade::Bad);
}

void test_advance_and_capacity() {
  PlaybackPlan plan;
  plan.player_notes.push_back(note(0U, PlaybackSoundId::F1_2, 1U, 0U));
  plan.player_notes.push_back(note(1U, PlaybackSoundId::F1_4, 2U, 1U));
  JudgementSession session;
  assert(session.reset(plan, config(80.0)) == JudgementError::None);

  JudgementResult output[2]{};
  assert(session.advance(0.05, output, 2U) == 0U);
  assert(session.advance(0.3, output, 1U) == 1U);
  assert(output[0].note_id.value == 1U);
  assert(output[0].grade == JudgementGrade::Miss);
  assert(session.advance(0.3, output, 1U) == 1U);
  assert(output[0].note_id.value == 2U);
  assert(session.advance(0.3, output, 2U) == 0U);
  assert(!session.press(0.3, PlaybackSoundId::F1_2));
}

void test_offset_and_half_beat() {
  const auto plan = single_note_plan(16U, PlaybackSoundId::F1_2);
  const auto offset = config(160.0,
                             JudgementWindowBasis::SixteenthNote, 20.0);
  JudgementSession session;
  assert(session.reset(plan, offset) == JudgementError::None);
  auto result = session.press(1.51, PlaybackSoundId::F1_2);
  assert(result);
  assert(near(result->delta_ms, -10.0));
  assert(result->grade == JudgementGrade::Perfect);

  const auto half = config(160.0, JudgementWindowBasis::HalfBeat);
  assert(session.reset(plan, half) == JudgementError::None);
  result = session.press(1.52, PlaybackSoundId::F1_2);
  assert(result && result->grade == JudgementGrade::Perfect);
  assert(session.reset(plan, half) == JudgementError::None);
  result = session.press(1.53, PlaybackSoundId::F1_2);
  assert(result && result->grade == JudgementGrade::Great);
  assert(session.reset(plan, half) == JudgementError::None);
  result = session.press(1.55, PlaybackSoundId::F1_2);
  assert(result && result->grade == JudgementGrade::Cool);
  assert(session.reset(plan, half) == JudgementError::None);
  result = session.press(1.58, PlaybackSoundId::F1_2);
  assert(result && result->grade == JudgementGrade::Bad);
  assert(session.reset(plan, half) == JudgementError::None);
  assert(!session.press(1.6, PlaybackSoundId::F1_2));
}

void test_reset_sorting_and_invalid_configuration() {
  PlaybackPlan plan;
  plan.player_notes.push_back(note(8U, PlaybackSoundId::F1_8, 3U, 0U));
  plan.player_notes.push_back(note(0U, PlaybackSoundId::F1_2, 1U, 1U));
  plan.player_notes.push_back(note(4U, PlaybackSoundId::F1_4, 2U, 2U));
  JudgementSession session;
  assert(session.reset(plan, config(160.0)) == JudgementError::None);
  auto result = session.press(0.0, PlaybackSoundId::F1_2);
  assert(result);
  assert(result->note_id.value == 1U);

  auto invalid = config(0.0);
  assert(session.reset(plan, invalid) ==
         JudgementError::InvalidConfiguration);
  assert(!session.initialized());
  assert(!session.press(0.0, PlaybackSoundId::F1_2));

  invalid = config(160.0);
  invalid.great_fraction = 0.5;
  assert(session.reset(plan, invalid) ==
         JudgementError::InvalidConfiguration);

  PlaybackPlan empty;
  assert(session.reset(empty, config(160.0)) == JudgementError::None);
  assert(session.initialized());
  assert(!session.press(0.0, PlaybackSoundId::F1_2));
  JudgementResult output[1]{};
  assert(session.advance(10.0, output, 1U) == 0U);
}

void test_empty_and_finished_session() {
  JudgementSession session;
  assert(!session.initialized());
  assert(!session.press(0.0, PlaybackSoundId::F1_2));
  JudgementResult output[1]{};
  assert(session.advance(0.0, output, 1U) == 0U);

  const auto plan = single_note_plan(0U, PlaybackSoundId::F1_2);
  assert(session.reset(plan, config(80.0)) == JudgementError::None);
  auto result = session.press(0.0, PlaybackSoundId::F1_2);
  assert(result && result->grade == JudgementGrade::Perfect);
  assert(!session.press(0.0, PlaybackSoundId::F1_2));
  assert(session.advance(10.0, output, 1U) == 0U);
}

}  // namespace

int main() {
  test_windows();
  test_grade_boundaries();
  test_press_matching_and_wrong_key();
  test_nearest_and_tie_break();
  test_advance_and_capacity();
  test_offset_and_half_beat();
  test_reset_sorting_and_invalid_configuration();
  test_empty_and_finished_session();
  return 0;
}
