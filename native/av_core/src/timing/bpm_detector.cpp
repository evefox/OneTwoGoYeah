// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/bpm_detector.hpp"

#include "av_core/timing/onset_detector.hpp"
#include "bpm_detector_internal.hpp"
#include "pcm_mix.hpp"
#include "tempo_constants.hpp"
#include "tempo_gap.hpp"

#include <Core/Polyfit.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

using Real = double;

constexpr int kIntervalDelta = 10;
constexpr int kIntervalDownsample = 3;
constexpr int kDefaultBpm = 120;

struct IntervalTester final {
  explicit IntervalTester(int sample_rate)
      : min_interval(static_cast<int>(
            sample_rate * 60.0 / detail::kMaximumDetectedBpm + 0.5)),
        max_interval(static_cast<int>(
            sample_rate * 60.0 / detail::kMinimumDetectedBpm + 0.5)),
        num_intervals(max_interval - min_interval),
        sample_rate(sample_rate),
        fitness(static_cast<std::size_t>(num_intervals)) {}

  int min_interval;
  int max_interval;
  int num_intervals;
  int sample_rate;
  std::vector<Real> fitness;
  Real coefficients[4]{};
};

void normalize_fitness(Real& fitness, const Real* coefficients,
                       Real interval) noexcept {
  const Real interval_squared = interval * interval;
  const Real interval_cubed = interval_squared * interval;
  fitness -= coefficients[0] + coefficients[1] * interval +
             coefficients[2] * interval_squared +
             coefficients[3] * interval_cubed;
}

[[nodiscard]] Real confidence_for_interval(detail::TempoGapData& data,
                                           int interval) noexcept {
  std::fill(data.wrapped_onsets.begin(), data.wrapped_onsets.end(), 0.0);
  const int reduced_interval = interval >> data.downsample;
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position =
        (static_cast<int>(data.onsets[index].frame_index) % interval) >>
        data.downsample;
    data.wrapped_positions[index] = position;
    data.wrapped_onsets[static_cast<std::size_t>(position)] +=
        data.onsets[index].strength;
  }

  Real highest_confidence = 0.0;
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position = data.wrapped_positions[index];
    Real confidence =
        detail::tempo_gap_confidence(data, position, reduced_interval);
    const int offbeat_position =
        (position + reduced_interval / 2) % reduced_interval;
    confidence +=
        detail::tempo_gap_confidence(data, offbeat_position,
                                     reduced_interval) *
        0.5;
    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }
  return highest_confidence;
}

[[nodiscard]] Real confidence_for_bpm(detail::TempoGapData& data,
                                      IntervalTester& tester,
                                      Real bpm) noexcept {
  std::fill(data.wrapped_onsets.begin(), data.wrapped_onsets.end(), 0.0);
  const Real interval_float = tester.sample_rate * 60.0 / bpm;
  const int interval = static_cast<int>(interval_float + 0.5);
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position = static_cast<int>(std::fmod(
        static_cast<Real>(data.onsets[index].frame_index), interval_float));
    data.wrapped_positions[index] = position;
    data.wrapped_onsets[static_cast<std::size_t>(position)] +=
        data.onsets[index].strength;
  }

  Real highest_confidence = 0.0;
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position = data.wrapped_positions[index];
    Real confidence = detail::tempo_gap_confidence(data, position, interval);
    const int offbeat_position = (position + interval / 2) % interval;
    confidence +=
        detail::tempo_gap_confidence(data, offbeat_position, interval) * 0.5;
    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }
  normalize_fitness(highest_confidence, tester.coefficients, interval_float);
  return highest_confidence;
}

void fill_coarse_intervals(IntervalTester& tester,
                           detail::TempoGapData& data) noexcept {
  const int count =
      (tester.num_intervals + kIntervalDelta - 1) / kIntervalDelta;
  for (int item = 0; item < count; ++item) {
    const int index = item * kIntervalDelta;
    const int interval = tester.min_interval + index;
    tester.fitness[static_cast<std::size_t>(index)] =
        std::max(0.001, confidence_for_interval(data, interval));
  }
}

struct IntervalRange final {
  int begin;
  int end;
};

[[nodiscard]] IntervalRange fill_interval_range(IntervalTester& tester,
                                                detail::TempoGapData& data,
                                                int begin,
                                                int end) noexcept {
  begin = std::max(begin, 0);
  end = std::min(end, tester.num_intervals);
  for (int index = begin, interval = tester.min_interval + begin;
       index < end; ++index, ++interval) {
    auto& fitness = tester.fitness[static_cast<std::size_t>(index)];
    if (fitness == 0.0) {
      fitness = confidence_for_interval(data, interval);
      normalize_fitness(fitness, tester.coefficients,
                        static_cast<Real>(interval));
      fitness = std::max(fitness, 0.1);
    }
  }
  return {begin, end};
}

[[nodiscard]] int find_best_interval(const std::vector<Real>& fitness,
                                     int begin, int end) noexcept {
  int best_interval = 0;
  Real highest_fitness = 0.0;
  for (int index = begin; index < end; ++index) {
    if (fitness[static_cast<std::size_t>(index)] > highest_fitness) {
      highest_fitness = fitness[static_cast<std::size_t>(index)];
      best_interval = index;
    }
  }
  return best_interval;
}

[[nodiscard]] Real interval_to_bpm(const IntervalTester& tester,
                                   int interval_index) noexcept {
  return tester.sample_rate * 60.0 /
         (interval_index + tester.min_interval);
}

void remove_duplicates(std::vector<BpmCandidate>& candidates) {
  for (int index = 0; index < static_cast<int>(candidates.size()); ++index) {
    const Real bpm = candidates[static_cast<std::size_t>(index)].bpm;
    const Real doubled = bpm * 2.0;
    const Real halved = bpm * 0.5;
    for (int other = static_cast<int>(candidates.size()) - 1;
         other > index; --other) {
      const Real value = candidates[static_cast<std::size_t>(other)].bpm;
      if (std::min(std::min(std::abs(value - bpm),
                            std::abs(value - doubled)),
                   std::abs(value - halved)) < 0.1) {
        candidates.erase(candidates.begin() + other);
      }
    }
  }
}

void round_bpm_values(IntervalTester& tester, detail::TempoGapData& data,
                      std::vector<BpmCandidate>& candidates) noexcept {
  for (auto& candidate : candidates) {
    const Real rounded_bpm = std::round(candidate.bpm);
    const Real difference = std::abs(candidate.bpm - rounded_bpm);
    if (difference < 0.01) {
      candidate.bpm = rounded_bpm;
    } else if (difference < 0.05) {
      const Real old_confidence =
          confidence_for_bpm(data, tester, candidate.bpm);
      const Real rounded_confidence =
          confidence_for_bpm(data, tester, rounded_bpm);
      if (rounded_confidence > old_confidence * 0.99) {
        candidate.bpm = rounded_bpm;
      }
    }
  }
}

struct CandidateFitnessDescending final {
  [[nodiscard]] bool operator()(const BpmCandidate& left,
                                const BpmCandidate& right) const noexcept {
    return left.fitness > right.fitness;
  }
};

[[nodiscard]] std::vector<BpmCandidate> calculate_bpm_candidates(
    int sample_rate, std::vector<Onset>& onsets,
    detail::BpmCalculationStageCallback stage_callback,
    void* stage_context) {
  std::vector<BpmCandidate> candidates;
  if (onsets.size() < 2U) {
    if (stage_callback != nullptr) {
      stage_callback(detail::BpmCalculationStage::RefiningCandidates,
                     stage_context);
      stage_callback(detail::BpmCalculationStage::SelectingBpm,
                     stage_context);
    }
    candidates.push_back(
        {static_cast<Real>(kDefaultBpm), static_cast<Real>(1.0)});
    return candidates;
  }

  IntervalTester tester(sample_rate);
  detail::TempoGapData coarse_data(tester.max_interval,
                                   kIntervalDownsample, onsets);
  std::fill(tester.fitness.begin(), tester.fitness.end(), 0.0);
  fill_coarse_intervals(tester, coarse_data);
  if (stage_callback != nullptr) {
    stage_callback(detail::BpmCalculationStage::RefiningCandidates,
                   stage_context);
  }
  const int coarse_count =
      (tester.num_intervals + kIntervalDelta - 1) / kIntervalDelta;

  mathalgo::polyfit(3, tester.coefficients, tester.fitness.data(),
                    coarse_count, tester.min_interval);
  Real max_fitness = 0.001;
  for (int index = 0; index < tester.num_intervals;
       index += kIntervalDelta) {
    auto& fitness = tester.fitness[static_cast<std::size_t>(index)];
    normalize_fitness(fitness, tester.coefficients,
                      static_cast<Real>(tester.min_interval + index));
    max_fitness = std::max(max_fitness, fitness);
  }

  const Real fitness_threshold = max_fitness * 0.4;
  for (int index = 0; index < tester.num_intervals;
       index += kIntervalDelta) {
    if (tester.fitness[static_cast<std::size_t>(index)] >
        fitness_threshold) {
      const auto range = fill_interval_range(
          tester, coarse_data, index - kIntervalDelta,
          index + kIntervalDelta);
      const int best = find_best_interval(tester.fitness, range.begin,
                                          range.end);
      candidates.push_back({interval_to_bpm(tester, best),
                            tester.fitness[static_cast<std::size_t>(best)]});
    }
  }

  if (stage_callback != nullptr) {
    stage_callback(detail::BpmCalculationStage::SelectingBpm,
                   stage_context);
  }

  detail::TempoGapData fine_data(tester.max_interval, 0, onsets);
  std::stable_sort(candidates.begin(), candidates.end(),
                   CandidateFitnessDescending{});
  remove_duplicates(candidates);
  round_bpm_values(tester, fine_data, candidates);

  if (candidates.size() >= 2U &&
      candidates[0].fitness / candidates[1].fitness < 1.05) {
    for (auto& candidate : candidates) {
      candidate.fitness =
          confidence_for_bpm(fine_data, tester, candidate.bpm);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     CandidateFitnessDescending{});
  }
  if (candidates.size() > 3U) {
    candidates.resize(3U);
  }
  return candidates;
}

[[nodiscard]] BpmDetectionError map_onset_error(
    OnsetDetectionError error) noexcept {
  switch (error) {
    case OnsetDetectionError::InputTooLarge:
      return BpmDetectionError::InputTooLarge;
    case OnsetDetectionError::AllocationFailure:
      return BpmDetectionError::AllocationFailure;
    case OnsetDetectionError::None:
      return BpmDetectionError::None;
  }
  return BpmDetectionError::AllocationFailure;
}

}  // namespace

BpmDetectionResult::BpmDetectionResult(
    std::vector<BpmCandidate> candidates) noexcept
    : storage_(std::move(candidates)) {}

BpmDetectionResult::BpmDetectionResult(BpmDetectionError error) noexcept
    : storage_(error) {}

bool BpmDetectionResult::has_value() const noexcept {
  return std::holds_alternative<std::vector<BpmCandidate>>(storage_);
}

const std::vector<BpmCandidate>* BpmDetectionResult::candidates()
    const noexcept {
  return std::get_if<std::vector<BpmCandidate>>(&storage_);
}

std::vector<BpmCandidate>* BpmDetectionResult::candidates() noexcept {
  return std::get_if<std::vector<BpmCandidate>>(&storage_);
}

BpmDetectionError BpmDetectionResult::error() const noexcept {
  if (const auto* error = std::get_if<BpmDetectionError>(&storage_)) {
    return *error;
  }
  return BpmDetectionError::None;
}

std::vector<BpmCandidate> BpmDetectionResult::take_candidates() && {
  return std::move(std::get<std::vector<BpmCandidate>>(storage_));
}

BpmDetectionResult detect_bpm_candidates(const Pcm16& pcm) noexcept {
  if (!detail::supports_detected_tempo_sample_rate(pcm.sample_rate())) {
    return BpmDetectionResult{BpmDetectionError::UnsupportedSampleRate};
  }

  auto onset_result = detect_onsets(pcm);
  if (!onset_result) {
    return BpmDetectionResult{map_onset_error(onset_result.error())};
  }
  return detail::detect_bpm_candidates_from_onsets(
      pcm, pcm.frame_count(), *onset_result.onsets());
}

BpmDetectionResult detail::detect_bpm_candidates_from_onsets(
    const Pcm16& pcm, std::size_t frame_count,
    const std::vector<Onset>& detected_onsets,
    BpmCalculationStageCallback stage_callback,
    void* stage_context) noexcept {
  constexpr auto kReferenceIntMax =
      static_cast<std::uint64_t>(std::numeric_limits<int>::max());
  if (static_cast<std::uint64_t>(pcm.sample_rate()) > kReferenceIntMax ||
      frame_count > kReferenceIntMax || frame_count > pcm.frame_count()) {
    return BpmDetectionResult{BpmDetectionError::InputTooLarge};
  }
  if (!supports_detected_tempo_sample_rate(pcm.sample_rate())) {
    return BpmDetectionResult{BpmDetectionError::UnsupportedSampleRate};
  }

  try {
    auto onsets = detected_onsets;

    const auto* left = pcm.channel_data(PcmChannel::Left);
    const auto* right = pcm.channel_data(PcmChannel::Right);
    const int reference_frame_count = static_cast<int>(frame_count);
    const int strength_count =
        std::min(static_cast<int>(onsets.size()), 100);
    for (int index = 0; index < strength_count; ++index) {
      const int position =
          static_cast<int>(onsets[static_cast<std::size_t>(index)].frame_index);
      const int begin = std::max(0, position - 100);
      const int end = std::min(reference_frame_count, position + 100);
      float strength = 0.0F;
      for (int frame = begin; frame < end; ++frame) {
        strength += std::abs(detail::mix_pcm16_frame(
            left[static_cast<std::size_t>(frame)],
            right[static_cast<std::size_t>(frame)]));
      }
      strength /= static_cast<float>(std::max(1, end - begin));
      onsets[static_cast<std::size_t>(index)].strength = strength;
    }

    return BpmDetectionResult{calculate_bpm_candidates(
        static_cast<int>(pcm.sample_rate()), onsets, stage_callback,
        stage_context)};
  } catch (const std::length_error&) {
    return BpmDetectionResult{BpmDetectionError::InputTooLarge};
  } catch (const std::bad_alloc&) {
    return BpmDetectionResult{BpmDetectionError::AllocationFailure};
  }
}

}  // namespace otgy::core
