// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/bpm_detector.hpp"

#include "av_core/timing/onset_detector.hpp"
#include "pcm_mix.hpp"

#include <Core/Polyfit.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

using Real = double;

constexpr Real kMinimumBpm = 89.0;
constexpr Real kMaximumBpm = 205.0;
constexpr int kIntervalDelta = 10;
constexpr int kIntervalDownsample = 3;
constexpr int kFineWindowSize = 2'048;
constexpr int kDefaultBpm = 120;

struct GapData final {
  GapData(int buffer_size, int downsample,
          const std::vector<Onset>& onsets)
      : onsets(onsets),
        wrapped_positions(onsets.size()),
        wrapped_onsets(static_cast<std::size_t>(buffer_size)),
        window(static_cast<std::size_t>(kFineWindowSize >> downsample)),
        window_size(kFineWindowSize >> downsample),
        downsample(downsample) {
    const Real step = 6.2831853071795864 /
                      static_cast<Real>(window_size - 1);
    for (int index = 0; index < window_size; ++index) {
      window[static_cast<std::size_t>(index)] =
          0.54 - 0.46 * std::cos(static_cast<Real>(index) * step);
    }
  }

  const std::vector<Onset>& onsets;
  std::vector<int> wrapped_positions;
  std::vector<Real> wrapped_onsets;
  std::vector<Real> window;
  int window_size;
  int downsample;
};

struct IntervalTester final {
  explicit IntervalTester(int sample_rate)
      : min_interval(static_cast<int>(sample_rate * 60.0 / kMaximumBpm +
                                      0.5)),
        max_interval(static_cast<int>(sample_rate * 60.0 / kMinimumBpm +
                                      0.5)),
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

[[nodiscard]] Real gap_confidence(const GapData& data, int gap_position,
                                  int interval) noexcept {
  const int half_window_size = data.window_size / 2;
  Real area = 0.0;
  int begin_onset = gap_position - half_window_size;
  int end_onset = gap_position + half_window_size;

  if (begin_onset < 0) {
    const int wrapped_begin = begin_onset + interval;
    for (int index = wrapped_begin; index < interval; ++index) {
      const int window_index = index - wrapped_begin;
      area += data.wrapped_onsets[static_cast<std::size_t>(index)] *
              data.window[static_cast<std::size_t>(window_index)];
    }
    begin_onset = 0;
  }
  if (end_onset > interval) {
    const int wrapped_end = end_onset - interval;
    const int index_offset = data.window_size - wrapped_end;
    for (int index = 0; index < wrapped_end; ++index) {
      const int window_index = index + index_offset;
      area += data.wrapped_onsets[static_cast<std::size_t>(index)] *
              data.window[static_cast<std::size_t>(window_index)];
    }
    end_onset = interval;
  }
  for (int index = begin_onset; index < end_onset; ++index) {
    const int window_index = index - begin_onset;
    area += data.wrapped_onsets[static_cast<std::size_t>(index)] *
            data.window[static_cast<std::size_t>(window_index)];
  }
  return area;
}

[[nodiscard]] Real confidence_for_interval(GapData& data,
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
    Real confidence = gap_confidence(data, position, reduced_interval);
    const int offbeat_position =
        (position + reduced_interval / 2) % reduced_interval;
    confidence +=
        gap_confidence(data, offbeat_position, reduced_interval) * 0.5;
    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }
  return highest_confidence;
}

[[nodiscard]] Real confidence_for_bpm(GapData& data,
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
    Real confidence = gap_confidence(data, position, interval);
    const int offbeat_position = (position + interval / 2) % interval;
    confidence += gap_confidence(data, offbeat_position, interval) * 0.5;
    if (confidence > highest_confidence) {
      highest_confidence = confidence;
    }
  }
  normalize_fitness(highest_confidence, tester.coefficients, interval_float);
  return highest_confidence;
}

void fill_coarse_intervals(IntervalTester& tester, GapData& data) noexcept {
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
                                                GapData& data, int begin,
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

void round_bpm_values(IntervalTester& tester, GapData& data,
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
    int sample_rate, std::vector<Onset>& onsets) {
  std::vector<BpmCandidate> candidates;
  if (onsets.size() < 2U) {
    candidates.push_back(
        {static_cast<Real>(kDefaultBpm), static_cast<Real>(1.0)});
    return candidates;
  }

  IntervalTester tester(sample_rate);
  GapData coarse_data(tester.max_interval, kIntervalDownsample, onsets);
  std::fill(tester.fitness.begin(), tester.fitness.end(), 0.0);
  fill_coarse_intervals(tester, coarse_data);
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

  GapData fine_data(tester.max_interval, 0, onsets);
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
  constexpr int kMinimumSupportedInterval = kFineWindowSize / 2;
  const int minimum_interval = static_cast<int>(
      static_cast<double>(pcm.sample_rate()) * 60.0 / kMaximumBpm + 0.5);
  if (minimum_interval < kMinimumSupportedInterval) {
    return BpmDetectionResult{BpmDetectionError::UnsupportedSampleRate};
  }

  try {
    auto onset_result = detect_onsets(pcm);
    if (!onset_result) {
      return BpmDetectionResult{map_onset_error(onset_result.error())};
    }
    auto onsets = std::move(onset_result).take_onsets();

    const auto* left = pcm.channel_data(PcmChannel::Left);
    const auto* right = pcm.channel_data(PcmChannel::Right);
    const int frame_count = static_cast<int>(pcm.frame_count());
    const int strength_count =
        std::min(static_cast<int>(onsets.size()), 100);
    for (int index = 0; index < strength_count; ++index) {
      const int position =
          static_cast<int>(onsets[static_cast<std::size_t>(index)].frame_index);
      const int begin = std::max(0, position - 100);
      const int end = std::min(frame_count, position + 100);
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
        static_cast<int>(pcm.sample_rate()), onsets)};
  } catch (const std::length_error&) {
    return BpmDetectionResult{BpmDetectionError::InputTooLarge};
  } catch (const std::bad_alloc&) {
    return BpmDetectionResult{BpmDetectionError::AllocationFailure};
  }
}

}  // namespace otgy::core
