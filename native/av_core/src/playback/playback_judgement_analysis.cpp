// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_judgement_analysis.hpp"

#include <algorithm>
#include <cmath>

namespace otgy::core {
namespace {

std::size_t histogram_bin(double value) noexcept {
  const auto index =
      (value - kJudgementAnalysisHistogramMinMs) /
      kJudgementAnalysisHistogramStepMs;
  if (!std::isfinite(index) || index <= 0.0) {
    return 0U;
  }
  const auto last = kJudgementAnalysisHistogramBins - 1U;
  if (index >= static_cast<double>(last)) {
    return last;
  }
  return static_cast<std::size_t>(index);
}

double bin_center(std::size_t index) noexcept {
  return kJudgementAnalysisHistogramMinMs +
         (static_cast<double>(index) + 0.5) *
             kJudgementAnalysisHistogramStepMs;
}

double median_from_histogram(
    const std::array<std::uint32_t,
                     kJudgementAnalysisHistogramBins>& histogram,
    std::size_t count) noexcept {
  if (count == 0U) {
    return 0.0;
  }
  const auto target = count / 2U;
  std::size_t cumulative = 0U;
  for (std::size_t index = 0U; index < histogram.size(); ++index) {
    cumulative += histogram[index];
    if (cumulative > target) {
      return bin_center(index);
    }
  }
  return bin_center(histogram.size() - 1U);
}

std::size_t perfect_count(const JudgementSample* samples,
                          std::size_t sample_count, double perfect_ms,
                          int candidate) noexcept {
  std::size_t count = 0U;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    const auto& sample = samples[index];
    if (!sample.correct_key || !std::isfinite(sample.note_seconds) ||
        !std::isfinite(sample.press_seconds)) {
      continue;
    }
    const auto raw = (sample.press_seconds - sample.note_seconds) * 1000.0;
    if (std::fabs(raw - static_cast<double>(candidate)) <= perfect_ms) {
      ++count;
    }
  }
  return count;
}

}  // namespace

JudgementAnalysis analyse_judgements(
    const JudgementSample* samples, std::size_t sample_count,
    std::size_t auto_miss_count, const JudgementConfig& config) noexcept {
  JudgementAnalysis analysis{};
  analysis.current_offset_ms = config.offset_ms;
  const auto windows = judgement_windows_ms(config);
  if (!windows) {
    return analysis;
  }
  analysis.perfect_ms = windows->perfect;
  analysis.great_ms = windows->great;
  analysis.cool_ms = windows->cool;
  analysis.bad_ms = windows->bad;

  if (samples == nullptr) {
    sample_count = 0U;
  }

  double sum = 0.0;
  double sum_squares = 0.0;
  double correct_sum = 0.0;
  for (std::size_t index = 0U; index < sample_count; ++index) {
    const auto& sample = samples[index];
    if (!std::isfinite(sample.note_seconds) ||
        !std::isfinite(sample.press_seconds)) {
      continue;
    }
    const auto raw = (sample.press_seconds - sample.note_seconds) * 1000.0;
    if (!std::isfinite(raw)) {
      continue;
    }
    ++analysis.sample_count;
    sum += raw;
    sum_squares += raw * raw;
    const auto bin = histogram_bin(raw);
    ++analysis.histogram_counts[bin];
    if (sample.correct_key) {
      ++analysis.correct_key_count;
      correct_sum += raw;
      const auto grade =
          grade_for_delta(raw - config.offset_ms, *windows);
      ++analysis.grade_counts[static_cast<std::size_t>(grade)];
    } else {
      ++analysis.wrong_key_count;
      ++analysis.grade_counts[static_cast<std::size_t>(
          JudgementGrade::Miss)];
    }
  }
  analysis.auto_miss_count = auto_miss_count;
  analysis.grade_counts[static_cast<std::size_t>(JudgementGrade::Miss)] +=
      auto_miss_count;

  if (analysis.sample_count > 0U) {
    const auto count = static_cast<double>(analysis.sample_count);
    analysis.mean_ms = sum / count;
    const auto variance =
        std::max(0.0,
                 sum_squares / count - analysis.mean_ms * analysis.mean_ms);
    analysis.stddev_ms = std::sqrt(variance);
    analysis.median_ms =
        median_from_histogram(analysis.histogram_counts, analysis.sample_count);
  }

  analysis.best_perfect_offset_ms = config.offset_ms;
  analysis.best_perfect_low_ms = config.offset_ms;
  analysis.best_perfect_high_ms = config.offset_ms;
  if (analysis.correct_key_count == 0U) {
    analysis.predicted_grade_counts[static_cast<std::size_t>(
        JudgementGrade::Miss)] = auto_miss_count;
    return analysis;
  }

  // The Perfect count as a function of the candidate offset is a boxcar
  // convolution over the press distribution. Evaluate every millisecond in
  // the tunable range; the sample is a single run, so this is cheap.
  const auto low = -static_cast<int>(kJudgementAnalysisOffsetLimitMs);
  const auto high = static_cast<int>(kJudgementAnalysisOffsetLimitMs);
  std::size_t best_count = 0U;
  for (auto candidate = low; candidate <= high; ++candidate) {
    best_count = std::max(
        best_count,
        perfect_count(samples, sample_count, analysis.perfect_ms, candidate));
  }

  auto plateau_low = high;
  auto plateau_high = low;
  for (auto candidate = low; candidate <= high; ++candidate) {
    if (perfect_count(samples, sample_count, analysis.perfect_ms, candidate) ==
        best_count) {
      plateau_low = std::min(plateau_low, candidate);
      plateau_high = std::max(plateau_high, candidate);
    }
  }
  if (plateau_low > plateau_high) {
    plateau_low = low;
    plateau_high = high;
    best_count = 0U;
  }

  const auto correct_mean =
      correct_sum / static_cast<double>(analysis.correct_key_count);
  const auto preferred =
      std::clamp(std::round(correct_mean),
                 static_cast<double>(plateau_low),
                 static_cast<double>(plateau_high));
  analysis.best_perfect_offset_ms = preferred;
  analysis.best_perfect_count = best_count;
  analysis.best_perfect_low_ms = static_cast<double>(plateau_low);
  analysis.best_perfect_high_ms = static_cast<double>(plateau_high);

  for (std::size_t index = 0U; index < sample_count; ++index) {
    const auto& sample = samples[index];
    if (!std::isfinite(sample.note_seconds) ||
        !std::isfinite(sample.press_seconds)) {
      continue;
    }
    if (!sample.correct_key) {
      ++analysis.predicted_grade_counts[static_cast<std::size_t>(
          JudgementGrade::Miss)];
      continue;
    }
    const auto raw = (sample.press_seconds - sample.note_seconds) * 1000.0;
    const auto grade = grade_for_delta(raw - preferred, *windows);
    ++analysis.predicted_grade_counts[static_cast<std::size_t>(grade)];
  }
  analysis.predicted_grade_counts[static_cast<std::size_t>(
      JudgementGrade::Miss)] += auto_miss_count;

  return analysis;
}

}  // namespace otgy::core
