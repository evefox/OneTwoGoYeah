// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/offset_detector.hpp"

#include "av_core/timing/onset_detector.hpp"
#include "pcm_mix.hpp"
#include "tempo_constants.hpp"
#include "tempo_gap.hpp"

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

[[nodiscard]] OffsetDetectionError map_onset_error(
    OnsetDetectionError error) noexcept {
  switch (error) {
    case OnsetDetectionError::InputTooLarge:
      return OffsetDetectionError::InputTooLarge;
    case OnsetDetectionError::AllocationFailure:
      return OffsetDetectionError::AllocationFailure;
    case OnsetDetectionError::None:
      return OffsetDetectionError::None;
  }
  return OffsetDetectionError::AllocationFailure;
}

[[nodiscard]] bool valid_bpm_candidates(
    const std::vector<BpmCandidate>& candidates) noexcept {
  if (candidates.size() > 3U) {
    return false;
  }
  for (const auto& candidate : candidates) {
    if (!std::isfinite(candidate.bpm) ||
        candidate.bpm < detail::kMinimumDetectedBpm ||
        candidate.bpm > detail::kMaximumDetectedBpm ||
        !std::isfinite(candidate.fitness)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Real base_offset_value(detail::TempoGapData& data,
                                     int sample_rate, Real bpm) noexcept {
  std::fill(data.wrapped_onsets.begin(), data.wrapped_onsets.end(), 0.0);
  const Real interval_float = sample_rate * 60.0 / bpm;
  const int interval = static_cast<int>(interval_float + 0.5);
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position = static_cast<int>(std::fmod(
        static_cast<Real>(data.onsets[index].frame_index), interval_float));
    data.wrapped_positions[index] = position;
    data.wrapped_onsets[static_cast<std::size_t>(position)] += 1.0;
  }

  Real highest_confidence = 0.0;
  int offset_position = 0;
  for (std::size_t index = 0U; index < data.onsets.size(); ++index) {
    const int position = data.wrapped_positions[index];
    Real confidence = detail::tempo_gap_confidence(data, position, interval);
    const int offbeat_position = (position + interval / 2) % interval;
    confidence +=
        detail::tempo_gap_confidence(data, offbeat_position, interval) * 0.5;
    if (confidence > highest_confidence) {
      highest_confidence = confidence;
      offset_position = position;
    }
  }
  return static_cast<Real>(offset_position) /
         static_cast<Real>(sample_rate);
}

void compute_slopes(const Pcm16& pcm, std::vector<Real>& slopes) noexcept {
  std::fill(slopes.begin(), slopes.end(), 0.0);
  const int frame_count = static_cast<int>(pcm.frame_count());
  const int half_window = static_cast<int>(pcm.sample_rate()) / 20;
  if (frame_count < half_window * 2) {
    return;
  }

  const auto* left = pcm.channel_data(PcmChannel::Left);
  const auto* right = pcm.channel_data(PcmChannel::Right);
  const auto sample = [left, right](int frame) noexcept {
    return detail::mix_pcm16_frame(left[static_cast<std::size_t>(frame)],
                                   right[static_cast<std::size_t>(frame)]);
  };

  Real sum_left = 0.0;
  Real sum_right = 0.0;
  for (int left_frame = 0, right_frame = half_window;
       left_frame < half_window; ++left_frame, ++right_frame) {
    sum_left += std::abs(sample(left_frame));
    sum_right += std::abs(sample(right_frame));
  }

  const Real scalar = 1.0 / static_cast<Real>(half_window);
  for (int frame = half_window, end = frame_count - half_window;
       frame < end; ++frame) {
    slopes[static_cast<std::size_t>(frame)] =
        std::max(0.0, (sum_right - sum_left) * scalar);

    const Real current = std::abs(sample(frame));
    sum_left -= std::abs(sample(frame - half_window));
    sum_left += current;
    sum_right -= current;
    sum_right += std::abs(sample(frame + half_window));
  }
}

[[nodiscard]] Real adjust_for_offbeats(const Pcm16& pcm, Real offset,
                                       Real bpm) {
  std::vector<Real> slopes(pcm.frame_count());
  compute_slopes(pcm, slopes);

  const Real seconds_per_beat = 60.0 / bpm;
  Real offbeat = offset + seconds_per_beat * 0.5;
  if (offbeat > seconds_per_beat) {
    offbeat -= seconds_per_beat;
  }

  const Real end = static_cast<Real>(pcm.frame_count());
  const Real interval = seconds_per_beat * pcm.sample_rate();
  Real position_a = offset * pcm.sample_rate();
  Real position_b = offbeat * pcm.sample_rate();
  Real sum_a = 0.0;
  Real sum_b = 0.0;
  for (; position_a < end && position_b < end;
       position_a += interval, position_b += interval) {
    sum_a += slopes[static_cast<std::size_t>(position_a)];
    sum_b += slopes[static_cast<std::size_t>(position_b)];
  }
  return sum_a >= sum_b ? offset : offbeat;
}

}  // namespace

OffsetDetectionResult::OffsetDetectionResult(
    std::vector<OffsetCandidate> candidates) noexcept
    : storage_(std::move(candidates)) {}

OffsetDetectionResult::OffsetDetectionResult(
    OffsetDetectionError error) noexcept
    : storage_(error) {}

bool OffsetDetectionResult::has_value() const noexcept {
  return std::holds_alternative<std::vector<OffsetCandidate>>(storage_);
}

const std::vector<OffsetCandidate>* OffsetDetectionResult::candidates()
    const noexcept {
  return std::get_if<std::vector<OffsetCandidate>>(&storage_);
}

std::vector<OffsetCandidate>* OffsetDetectionResult::candidates() noexcept {
  return std::get_if<std::vector<OffsetCandidate>>(&storage_);
}

OffsetDetectionError OffsetDetectionResult::error() const noexcept {
  if (const auto* error = std::get_if<OffsetDetectionError>(&storage_)) {
    return *error;
  }
  return OffsetDetectionError::None;
}

std::vector<OffsetCandidate> OffsetDetectionResult::take_candidates() && {
  return std::move(std::get<std::vector<OffsetCandidate>>(storage_));
}

OffsetDetectionResult detect_offsets(
    const Pcm16& pcm,
    const std::vector<BpmCandidate>& bpm_candidates) noexcept {
  constexpr auto kReferenceIntMax =
      static_cast<std::uint64_t>(std::numeric_limits<int>::max());
  if (static_cast<std::uint64_t>(pcm.sample_rate()) > kReferenceIntMax ||
      pcm.frame_count() > kReferenceIntMax) {
    return OffsetDetectionResult{OffsetDetectionError::InputTooLarge};
  }
  if (!valid_bpm_candidates(bpm_candidates)) {
    return OffsetDetectionResult{
        OffsetDetectionError::InvalidBpmCandidates};
  }
  if (bpm_candidates.empty()) {
    return OffsetDetectionResult{std::vector<OffsetCandidate>{}};
  }

  const int minimum_interval = static_cast<int>(
      static_cast<double>(pcm.sample_rate()) * 60.0 /
          detail::kMaximumDetectedBpm +
      0.5);
  if (minimum_interval < detail::kMinimumSupportedTempoInterval) {
    return OffsetDetectionResult{
        OffsetDetectionError::UnsupportedSampleRate};
  }

  try {
    auto onset_result = detect_onsets(pcm);
    if (!onset_result) {
      return OffsetDetectionResult{map_onset_error(onset_result.error())};
    }
    auto onsets = std::move(onset_result).take_onsets();

    Real maximum_interval = 0.0;
    for (const auto& candidate : bpm_candidates) {
      maximum_interval = std::max(
          maximum_interval, pcm.sample_rate() * 60.0 / candidate.bpm);
    }
    detail::TempoGapData gap_data(
        static_cast<int>(maximum_interval + 1.0), 1, onsets);

    std::vector<OffsetCandidate> candidates;
    candidates.reserve(bpm_candidates.size());
    for (const auto& bpm_candidate : bpm_candidates) {
      Real detector_offset = base_offset_value(
          gap_data, static_cast<int>(pcm.sample_rate()), bpm_candidate.bpm);
      detector_offset =
          adjust_for_offbeats(pcm, detector_offset, bpm_candidate.bpm);
      candidates.push_back({bpm_candidate.bpm, detector_offset,
                            -detector_offset, bpm_candidate.fitness});
    }
    return OffsetDetectionResult{std::move(candidates)};
  } catch (const std::length_error&) {
    return OffsetDetectionResult{OffsetDetectionError::InputTooLarge};
  } catch (const std::bad_alloc&) {
    return OffsetDetectionResult{OffsetDetectionError::AllocationFailure};
  }
}

}  // namespace otgy::core
