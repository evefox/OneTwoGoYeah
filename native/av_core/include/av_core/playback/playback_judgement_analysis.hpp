// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/playback/playback_judgement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace otgy::core {

constexpr std::size_t kJudgementAnalysisHistogramBins = 601U;
constexpr double kJudgementAnalysisHistogramMinMs = -300.0;
constexpr double kJudgementAnalysisHistogramStepMs = 1.0;
/// The analysis only recommends offsets the tuning menu can apply.
constexpr double kJudgementAnalysisOffsetLimitMs = 100.0;

/// One matched press of the current run. Raw timing is derived from the song
/// timelines so the analysis does not depend on the offset used to judge it.
struct JudgementSample final {
  double note_seconds;
  double press_seconds;
  bool correct_key;
};

struct JudgementAnalysis final {
  std::size_t sample_count;
  std::size_t correct_key_count;
  std::size_t wrong_key_count;
  std::size_t auto_miss_count;
  /// P, G, C, B, M at the offset currently in use.
  std::array<std::size_t, 5> grade_counts;
  /// P, G, C, B, M at [best_perfect_offset_ms].
  std::array<std::size_t, 5> predicted_grade_counts;
  double mean_ms;
  double median_ms;
  double stddev_ms;
  double perfect_ms;
  double great_ms;
  double cool_ms;
  double bad_ms;
  double current_offset_ms;
  /// Offset that maximises the Perfect count within the tunable range.
  double best_perfect_offset_ms;
  std::size_t best_perfect_count;
  /// Contiguous candidate range that reaches the same Perfect count.
  double best_perfect_low_ms;
  double best_perfect_high_ms;
  std::array<std::uint32_t, kJudgementAnalysisHistogramBins> histogram_counts;
};

/// Analyses one simulation run. The caller supplies the matched presses plus
/// the timed-out note count. Invalid windows return an empty analysis.
/// Pure and allocation-free: it is safe to call from the UI thread.
[[nodiscard]] JudgementAnalysis analyse_judgements(
    const JudgementSample* samples, std::size_t sample_count,
    std::size_t auto_miss_count, const JudgementConfig& config) noexcept;

}  // namespace otgy::core
