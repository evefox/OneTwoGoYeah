// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "otgy/av_c_api.h"

#include "av_core/audio/audio_export.hpp"
#include "av_core/audio/audio_export_name.hpp"
#include "av_core/audio/audio_export_quality.hpp"
#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"
#include "av_core/audio/pcm_resample.hpp"
#include "av_core/chart/chart_editor.hpp"
#include "av_core/chart/chart_validator.hpp"
#include "av_core/coordination/operation_gate.hpp"
#include "av_core/playback/playback_engine.hpp"
#include "av_core/playback/playback_position.hpp"
#include "av_core/playback/playback_pcm.hpp"
#include "av_core/playback/playback_sound.hpp"
#include "av_core/project/project_serialization.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"
#include "av_core/slk/slk_export.hpp"
#include "av_core/slk/slk_position.hpp"
#include "av_core/timing/bpm_detector.hpp"
#include "av_core/timing/offset_detector.hpp"
#include "av_core/timing/onset_detector.hpp"
#include "av_core/timing/sync_analyzer.hpp"
#include "av_core/timing/timeline_navigation.hpp"
#include "av_core/timing/timeline_range.hpp"
#include "av_core/timing/timing_conversion.hpp"
#include "av_core/timing/timing_grid.hpp"
#include "av_core/version.hpp"
#include "av_core/waveform/waveform_tile.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct otgy_error final {
  otgy_error_domain domain;
  std::uint32_t code;
  std::string_view message;
};

struct otgy_bytes final {
  std::vector<std::uint8_t> value;
};

struct otgy_waveform_filter_cache final {
  otgy::core::WaveformFilter filter;
  double strength;
  otgy::core::Pcm16 pcm;
};

struct otgy_pcm final {
  otgy::core::Pcm16 value;
  bool has_source_info;
  otgy::core::AudioSourceInfo source_info;
  mutable std::optional<otgy_waveform_filter_cache> waveform_filter_cache;
};

struct otgy_audio_export_snapshot final {
  otgy::core::AudioExportSnapshot value;
};

struct otgy_audio_export_output final {
  otgy::core::AudioExportBytes value;
};

struct otgy_audio_export_cancel_flag final {
  std::atomic_bool requested{false};
};

struct otgy_waveform_tile final {
  otgy::core::WaveformTile value;
};

struct otgy_onsets final {
  std::vector<otgy_onset> value;
};

struct otgy_bpm_candidates final {
  std::vector<otgy_bpm_candidate> value;
};

struct otgy_offset_candidates final {
  std::vector<otgy_offset_candidate> value;
};

struct otgy_sync_analysis final {
  std::uint64_t analyzed_frame_count;
  std::vector<otgy_offset_candidate> candidates;
};

struct otgy_timing_grid final {
  std::vector<otgy_timing_grid_line> lines;
};

struct otgy_chart final {
  otgy::core::ChartDraft value;
};

struct otgy_chart_semantics final {
  std::vector<otgy_chart_role_event> role_events;
  std::vector<otgy_chart_showtime_start_event> showtime_start_events;
  std::vector<otgy_chart_showtime_move_event> showtime_move_events;
  std::vector<otgy_chart_drill_group> drill_groups;
  std::vector<std::uint64_t> drill_group_event_ids;
  std::vector<otgy_chart_dance_move_drill> dance_move_drills;
  std::vector<std::uint64_t> dance_move_drill_group_ids;
  std::vector<otgy_chart_showtime> showtimes;
  std::vector<std::uint64_t> showtime_dance_move_drill_ids;
  std::vector<std::uint64_t> showtime_move_event_ids;
};

struct otgy_chart_markers final {
  std::vector<otgy_chart_marker> value;
};

struct otgy_chart_selections final {
  std::vector<otgy_chart_selection> value;
};

struct otgy_chart_paste_preview final {
  std::uint32_t code;
  std::uint64_t first_row;
  std::uint64_t last_row;
  std::vector<otgy_chart_selection> selections;
  std::vector<otgy_chart_marker> markers;
};

struct otgy_showtime_moves final {
  std::vector<otgy_showtime_move> value;
};

struct otgy_chart_editor final {
  otgy::core::ChartEditor value;
};

struct otgy_finished_chart final {
  otgy::core::FinishedChart value;
};

struct otgy_chart_validation final {
  std::optional<otgy::core::FinishedChart> finished;
  std::vector<otgy_chart_diagnostic> diagnostics;
};

struct otgy_slk_document final {
  otgy::core::FinishedSlkDocument value;
};

struct otgy_playback_pcm final {
  otgy::core::PlaybackPcm value;
};

struct otgy_playback_sound_bank final {
  otgy::core::PlaybackSoundBank value;
};

struct otgy_playback_engine final {
  otgy::core::PlaybackEngine value;
};

namespace otgy::c_api {

inline constexpr std::string_view kNullArgument =
    "required C ABI argument is null";
inline constexpr std::string_view kLengthOutOfRange =
    "C ABI length exceeds the native address range";
inline constexpr std::string_view kInvalidBoolean =
    "C ABI boolean must be zero or one";
inline constexpr std::string_view kResultNotAvailable =
    "requested C ABI result is not available";
inline constexpr std::string_view kOutOfMemory =
    "unable to allocate C ABI result";
inline constexpr std::string_view kUnexpectedException =
    "unexpected C++ exception was contained at the C ABI boundary";

inline void clear_error(otgy_error** out_error) noexcept {
  if (out_error != nullptr) {
    *out_error = nullptr;
  }
}

inline otgy_status set_error(otgy_error** out_error, otgy_status status,
                             otgy_error_domain domain, std::uint32_t code,
                             std::string_view message) noexcept {
  if (out_error == nullptr) {
    return status;
  }
  auto* error = new (std::nothrow) otgy_error{domain, code, message};
  if (error == nullptr) {
    return OTGY_STATUS_OUT_OF_MEMORY;
  }
  *out_error = error;
  return status;
}

inline otgy_status invalid_argument(
    otgy_error** out_error, std::uint32_t code = OTGY_ABI_ERROR_NULL_ARGUMENT,
    std::string_view message = kNullArgument) noexcept {
  return set_error(out_error, OTGY_STATUS_INVALID_ARGUMENT,
                   OTGY_ERROR_DOMAIN_ABI, code, message);
}

inline otgy_status out_of_memory(otgy_error** out_error) noexcept {
  return set_error(out_error, OTGY_STATUS_OUT_OF_MEMORY,
                   OTGY_ERROR_DOMAIN_ABI, 0U, kOutOfMemory);
}

inline otgy_status core_error(otgy_error** out_error,
                              otgy_error_domain domain, std::uint32_t code,
                              std::string_view message) noexcept {
  return set_error(out_error, OTGY_STATUS_CORE_ERROR, domain, code, message);
}

template <typename Function>
otgy_status boundary(otgy_error** out_error, Function&& function) noexcept {
  clear_error(out_error);
  try {
    return std::forward<Function>(function)();
  } catch (const std::bad_alloc&) {
    return out_of_memory(out_error);
  } catch (...) {
    return set_error(out_error, OTGY_STATUS_UNEXPECTED_EXCEPTION,
                     OTGY_ERROR_DOMAIN_ABI,
                     OTGY_ABI_ERROR_UNEXPECTED_EXCEPTION,
                     kUnexpectedException);
  }
}

inline bool to_size(std::uint64_t value, std::size_t& out) noexcept {
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  out = static_cast<std::size_t>(value);
  return true;
}

inline bool valid_bytes(otgy_byte_view view, std::size_t& size) noexcept {
  return to_size(view.length, size);
}

inline bool valid_string(otgy_string_view view,
                         std::string_view& value) noexcept {
  std::size_t length = 0U;
  if (!to_size(view.length, length) ||
      (view.data == nullptr && view.length != 0U)) {
    return false;
  }
  value = std::string_view(view.data == nullptr ? "" : view.data, length);
  return true;
}

inline otgy::core::AudioSourceInfo to_core(
    otgy_audio_source_info value) noexcept {
  return {static_cast<otgy::core::AudioSourceCodec>(value.codec),
          value.measured_bitrate_bps, value.nominal_bitrate_bps};
}

inline otgy_audio_source_info from_core(
    otgy::core::AudioSourceInfo value) noexcept {
  return {static_cast<std::uint32_t>(value.codec), value.measured_bitrate_bps,
          value.nominal_bitrate_bps};
}

inline otgy::core::TimingParameters to_core(
    otgy_timing_parameters value) noexcept {
  return {value.bpm, value.offset_seconds};
}

inline otgy_offset_candidate from_core(
    const otgy::core::OffsetCandidate& value) noexcept {
  return {value.bpm, value.detector_offset_seconds, value.app_offset_seconds,
          value.fitness};
}

template <typename Handle, typename Value>
otgy_status make_handle(Value&& value, Handle** out,
                        otgy_error** out_error) noexcept {
  auto* handle = new (std::nothrow) Handle{std::forward<Value>(value)};
  if (handle == nullptr) {
    return out_of_memory(out_error);
  }
  *out = handle;
  return OTGY_STATUS_OK;
}

inline otgy_status make_bytes(std::vector<std::uint8_t> value,
                              otgy_bytes** out,
                              otgy_error** out_error) noexcept {
  return make_handle<otgy_bytes>(std::move(value), out, out_error);
}

inline otgy_status make_utf8(std::string value, otgy_bytes** out,
                             otgy_error** out_error) {
  std::vector<std::uint8_t> bytes(value.begin(), value.end());
  return make_bytes(std::move(bytes), out, out_error);
}

}  // namespace otgy::c_api
