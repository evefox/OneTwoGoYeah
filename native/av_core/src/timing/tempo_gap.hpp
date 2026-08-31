// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/timing/onset_detector.hpp"
#include "tempo_constants.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace otgy::core::detail {

class TempoGapData final {
 public:
  TempoGapData(int buffer_size, int downsample,
               const std::vector<Onset>& onsets)
      : onsets(onsets),
        wrapped_positions(onsets.size()),
        wrapped_onsets(static_cast<std::size_t>(buffer_size)),
        window(static_cast<std::size_t>(kTempoGapWindowSize >> downsample)),
        window_size(kTempoGapWindowSize >> downsample),
        downsample(downsample) {
    const double step = 6.2831853071795864 /
                        static_cast<double>(window_size - 1);
    for (int index = 0; index < window_size; ++index) {
      window[static_cast<std::size_t>(index)] =
          0.54 - 0.46 * std::cos(static_cast<double>(index) * step);
    }
  }

  const std::vector<Onset>& onsets;
  std::vector<int> wrapped_positions;
  std::vector<double> wrapped_onsets;
  std::vector<double> window;
  int window_size;
  int downsample;
};

[[nodiscard]] inline double tempo_gap_confidence(
    const TempoGapData& data, int gap_position, int interval) noexcept {
  const int half_window_size = data.window_size / 2;
  double area = 0.0;
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

}  // namespace otgy::core::detail
