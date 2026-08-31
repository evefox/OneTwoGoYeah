// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

using namespace otgy::c_api;

namespace {

template <typename Input, typename Output, typename Convert>
std::vector<Output> convert_vector(const std::vector<Input>& input,
                                   Convert&& convert) {
  std::vector<Output> result;
  result.reserve(input.size());
  for (const auto& item : input) {
    result.push_back(convert(item));
  }
  return result;
}

struct SyncCallbackBridge final {
  otgy_sync_stage_callback callback;
  void* context;
};

void report_sync_stage(otgy::core::SyncAnalysisStage stage,
                       void* context) noexcept {
  auto* bridge = static_cast<SyncCallbackBridge*>(context);
  bridge->callback(static_cast<std::uint32_t>(stage), bridge->context);
}

}  // namespace

extern "C" {

otgy_status otgy_detect_onsets(const otgy_pcm* pcm, otgy_onsets** out_onsets,
                               otgy_error** out_error) {
  if (out_onsets != nullptr) {
    *out_onsets = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_onsets == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::detect_onsets(pcm->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_ONSET_DETECTION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::onset_detection_error_message(result.error()));
    }
    auto values = convert_vector<otgy::core::Onset, otgy_onset>(
        *result.onsets(), [](const auto& value) {
          return otgy_onset{value.frame_index, value.strength};
        });
    return make_handle<otgy_onsets>(otgy_onsets{std::move(values)},
                                    out_onsets, out_error);
  });
}

void otgy_onsets_destroy(otgy_onsets* onsets) { delete onsets; }

const otgy_onset* otgy_onsets_get_data(const otgy_onsets* onsets) {
  return onsets == nullptr || onsets->value.empty() ? nullptr
                                                    : onsets->value.data();
}

std::uint64_t otgy_onsets_get_count(const otgy_onsets* onsets) {
  return onsets == nullptr ? 0U
                           : static_cast<std::uint64_t>(onsets->value.size());
}

otgy_status otgy_detect_bpm_candidates(
    const otgy_pcm* pcm, otgy_bpm_candidates** out_candidates,
    otgy_error** out_error) {
  if (out_candidates != nullptr) {
    *out_candidates = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_candidates == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::detect_bpm_candidates(pcm->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_BPM_DETECTION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::bpm_detection_error_message(result.error()));
    }
    auto values = convert_vector<otgy::core::BpmCandidate, otgy_bpm_candidate>(
        *result.candidates(), [](const auto& value) {
          return otgy_bpm_candidate{value.bpm, value.fitness};
        });
    return make_handle<otgy_bpm_candidates>(
        otgy_bpm_candidates{std::move(values)}, out_candidates, out_error);
  });
}

void otgy_bpm_candidates_destroy(otgy_bpm_candidates* candidates) {
  delete candidates;
}

const otgy_bpm_candidate* otgy_bpm_candidates_get_data(
    const otgy_bpm_candidates* candidates) {
  return candidates == nullptr || candidates->value.empty()
             ? nullptr
             : candidates->value.data();
}

std::uint64_t otgy_bpm_candidates_get_count(
    const otgy_bpm_candidates* candidates) {
  return candidates == nullptr
             ? 0U
             : static_cast<std::uint64_t>(candidates->value.size());
}

otgy_status otgy_detect_offsets(
    const otgy_pcm* pcm, const otgy_bpm_candidate* candidates,
    std::uint64_t candidate_count, otgy_offset_candidates** out_candidates,
    otgy_error** out_error) {
  if (out_candidates != nullptr) {
    *out_candidates = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::size_t count = 0U;
    if (pcm == nullptr || out_candidates == nullptr ||
        !to_size(candidate_count, count) ||
        (candidates == nullptr && candidate_count != 0U)) {
      return invalid_argument(out_error);
    }
    std::vector<otgy::core::BpmCandidate> core_candidates;
    core_candidates.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      core_candidates.push_back({candidates[index].bpm,
                                 candidates[index].fitness});
    }
    const auto result =
        otgy::core::detect_offsets(pcm->value, core_candidates);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_OFFSET_DETECTION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::offset_detection_error_message(result.error()));
    }
    auto values =
        convert_vector<otgy::core::OffsetCandidate, otgy_offset_candidate>(
            *result.candidates(),
            [](const auto& value) { return otgy::c_api::from_core(value); });
    return make_handle<otgy_offset_candidates>(
        otgy_offset_candidates{std::move(values)}, out_candidates, out_error);
  });
}

void otgy_offset_candidates_destroy(otgy_offset_candidates* candidates) {
  delete candidates;
}

const otgy_offset_candidate* otgy_offset_candidates_get_data(
    const otgy_offset_candidates* candidates) {
  return candidates == nullptr || candidates->value.empty()
             ? nullptr
             : candidates->value.data();
}

std::uint64_t otgy_offset_candidates_get_count(
    const otgy_offset_candidates* candidates) {
  return candidates == nullptr
             ? 0U
             : static_cast<std::uint64_t>(candidates->value.size());
}

otgy_status otgy_analyze_sync(
    const otgy_pcm* pcm, otgy_sync_stage_callback stage_callback,
    void* stage_context, otgy_sync_analysis** out_analysis,
    otgy_error** out_error) {
  if (out_analysis != nullptr) {
    *out_analysis = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_analysis == nullptr) {
      return invalid_argument(out_error);
    }
    SyncCallbackBridge bridge{stage_callback, stage_context};
    const auto result = otgy::core::analyze_sync(
        pcm->value, stage_callback == nullptr ? nullptr : report_sync_stage,
        &bridge);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_SYNC_ANALYSIS,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::sync_analysis_error_message(result.error()));
    }
    auto values =
        convert_vector<otgy::core::OffsetCandidate, otgy_offset_candidate>(
            result.analysis()->candidates(),
            [](const auto& value) { return otgy::c_api::from_core(value); });
    return make_handle<otgy_sync_analysis>(
        otgy_sync_analysis{result.analysis()->analyzed_frame_count(),
                           std::move(values)},
        out_analysis, out_error);
  });
}

void otgy_sync_analysis_destroy(otgy_sync_analysis* analysis) {
  delete analysis;
}

std::uint64_t otgy_sync_analysis_get_analyzed_frame_count(
    const otgy_sync_analysis* analysis) {
  return analysis == nullptr ? 0U : analysis->analyzed_frame_count;
}

const otgy_offset_candidate* otgy_sync_analysis_get_candidates(
    const otgy_sync_analysis* analysis) {
  return analysis == nullptr || analysis->candidates.empty()
             ? nullptr
             : analysis->candidates.data();
}

std::uint64_t otgy_sync_analysis_get_candidate_count(
    const otgy_sync_analysis* analysis) {
  return analysis == nullptr
             ? 0U
             : static_cast<std::uint64_t>(analysis->candidates.size());
}

}  // extern "C"
