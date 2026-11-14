// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_judgement_analysis.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {

using otgy::core::analyse_judgements;
using otgy::core::default_judgement_config;
using otgy::core::JudgementAnalysis;
using otgy::core::JudgementConfig;
using otgy::core::JudgementGrade;
using otgy::core::JudgementSample;
using otgy::core::judgement_windows_ms;

JudgementConfig config_for(double bpm) {
  return default_judgement_config({bpm, 0.0});
}

std::size_t grade(const JudgementAnalysis& analysis, JudgementGrade value) {
  return analysis.grade_counts[static_cast<std::size_t>(value)];
}

std::size_t predicted(const JudgementAnalysis& analysis,
                      JudgementGrade value) {
  return analysis.predicted_grade_counts[static_cast<std::size_t>(value)];
}

void test_centered_presses_recommend_their_mean() {
  const auto config = config_for(160.0);
  const auto windows = judgement_windows_ms(config);
  assert(windows);
  // Half-beat unit at 160 BPM is 187.5 ms.
  assert(std::fabs(windows->perfect - 23.4375) < 1e-9);

  JudgementSample samples[10]{};
  for (auto& sample : samples) {
    sample = {1.0, 1.030, true};  // +30 ms raw
  }
  const auto analysis = analyse_judgements(samples, 10U, 2U, config);
  assert(analysis.sample_count == 10U);
  assert(analysis.correct_key_count == 10U);
  assert(analysis.wrong_key_count == 0U);
  assert(analysis.auto_miss_count == 2U);
  assert(std::fabs(analysis.mean_ms - 30.0) < 1e-6);
  assert(std::fabs(analysis.median_ms - 30.5) < 1e-6);
  // At offset 0 the +30 ms presses are Great; the two missed notes are Miss.
  assert(grade(analysis, JudgementGrade::Great) == 10U);
  assert(grade(analysis, JudgementGrade::Miss) == 2U);
  // The Perfect plateau is the 23.4375 ms window around +30 ms.
  assert(analysis.best_perfect_count == 10U);
  assert(std::fabs(analysis.best_perfect_offset_ms - 30.0) < 1e-6);
  assert(std::fabs(analysis.best_perfect_low_ms - 7.0) < 1e-6);
  assert(std::fabs(analysis.best_perfect_high_ms - 53.0) < 1e-6);
  assert(predicted(analysis, JudgementGrade::Perfect) == 10U);
  assert(predicted(analysis, JudgementGrade::Miss) == 2U);
}

void test_wrong_keys_never_count_as_perfect() {
  const auto config = config_for(160.0);
  JudgementSample samples[6]{};
  for (auto& sample : samples) {
    sample = {2.0, 2.0, true};  // exact
  }
  samples[5] = {2.0, 2.0, false};
  const auto analysis = analyse_judgements(samples, 6U, 3U, config);
  assert(analysis.sample_count == 6U);
  assert(analysis.correct_key_count == 5U);
  assert(analysis.wrong_key_count == 1U);
  assert(grade(analysis, JudgementGrade::Perfect) == 5U);
  assert(grade(analysis, JudgementGrade::Miss) == 4U);  // wrong + 3 timed out
  assert(analysis.best_perfect_count == 5U);
  assert(std::fabs(analysis.best_perfect_offset_ms) < 1e-6);
  assert(predicted(analysis, JudgementGrade::Perfect) == 5U);
  assert(predicted(analysis, JudgementGrade::Miss) == 4U);
}

void test_skewed_presses_follow_the_plateau_and_mean() {
  const auto config = config_for(160.0);
  JudgementSample samples[11]{};
  for (std::size_t index = 0U; index < 10U; ++index) {
    samples[index] = {3.0, 3.040, true};  // +40 ms
  }
  samples[10] = {3.0, 2.960, true};  // -40 ms
  const auto analysis = analyse_judgements(samples, 11U, 0U, config);
  // Ten samples share the +40 ms cluster, so the plateau covers it and the
  // mean picks a point inside it.
  assert(analysis.best_perfect_count == 10U);
  assert(analysis.best_perfect_low_ms <= analysis.best_perfect_offset_ms);
  assert(analysis.best_perfect_offset_ms <= analysis.best_perfect_high_ms);
  assert(std::fabs(analysis.best_perfect_offset_ms - 33.0) < 1e-6);
  assert(predicted(analysis, JudgementGrade::Perfect) == 10U);
  // -73 ms at the chosen offset is still inside the 93.75 ms Bad window.
  assert(predicted(analysis, JudgementGrade::Bad) == 1U);
  assert(predicted(analysis, JudgementGrade::Miss) == 0U);
}

void test_empty_and_invalid_input_are_safe() {
  const auto empty = analyse_judgements(nullptr, 0U, 0U, config_for(120.0));
  assert(empty.sample_count == 0U);
  assert(empty.best_perfect_count == 0U);
  assert(std::fabs(empty.best_perfect_offset_ms) < 1e-6);

  auto invalid = config_for(160.0);
  invalid.timing.bpm = 0.0;
  const JudgementSample sample{0.0, 0.01, true};
  const auto rejected = analyse_judgements(&sample, 1U, 0U, invalid);
  assert(rejected.sample_count == 0U);
  assert(rejected.correct_key_count == 0U);
}

}  // namespace

int main() {
  test_centered_presses_recommend_their_mean();
  test_wrong_keys_never_count_as_perfect();
  test_skewed_presses_follow_the_plateau_and_mean();
  test_empty_and_invalid_input_are_safe();
  return 0;
}
