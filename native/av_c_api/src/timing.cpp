// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

using namespace otgy::c_api;

namespace {

otgy_status return_timing_result(
    const otgy::core::TimingConversionResult& result, double* out_value,
    otgy_error** out_error) noexcept {
  if (!result) {
    return core_error(
        out_error, OTGY_ERROR_DOMAIN_TIMING_CONVERSION,
        static_cast<std::uint32_t>(result.error()),
        otgy::core::timing_conversion_error_message(result.error()));
  }
  *out_value = *result.value();
  return OTGY_STATUS_OK;
}

otgy::core::PlaybackPosition to_core(otgy_playback_position value) noexcept {
  return {static_cast<otgy::core::PlaybackPhase>(value.phase),
          value.timeline_seconds, value.audio_duration_seconds};
}

otgy_playback_position from_core(
    const otgy::core::PlaybackPosition& value) noexcept {
  return {static_cast<std::uint32_t>(value.phase()), value.timeline_seconds(),
          value.audio_duration_seconds()};
}

otgy_status return_playback_result(
    const otgy::core::PlaybackPositionResult& result,
    otgy_playback_position* out_position, otgy_error** out_error) noexcept {
  if (!result) {
    return core_error(
        out_error, OTGY_ERROR_DOMAIN_PLAYBACK_POSITION,
        static_cast<std::uint32_t>(result.error()),
        otgy::core::playback_position_error_message(result.error()));
  }
  *out_position = from_core(*result.position());
  return OTGY_STATUS_OK;
}

}  // namespace

extern "C" {

otgy_status otgy_seconds_to_beat(otgy_timing_parameters timing,
                                 double seconds, double* out_value,
                                 otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(
        otgy::core::seconds_to_beat(otgy::c_api::to_core(timing), seconds),
        out_value, out_error);
  });
}

otgy_status otgy_beat_to_seconds(otgy_timing_parameters timing, double beat,
                                 double* out_value, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(
        otgy::core::beat_to_seconds(otgy::c_api::to_core(timing), beat),
        out_value, out_error);
  });
}

otgy_status otgy_seconds_to_sixteenth_row(
    otgy_timing_parameters timing, double seconds, double* out_value,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(
        otgy::core::seconds_to_sixteenth_row(otgy::c_api::to_core(timing),
                                             seconds),
        out_value, out_error);
  });
}

otgy_status otgy_sixteenth_row_to_seconds(
    otgy_timing_parameters timing, double row, double* out_value,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(
        otgy::core::sixteenth_row_to_seconds(otgy::c_api::to_core(timing),
                                             row),
        out_value, out_error);
  });
}

otgy_status otgy_beat_to_sixteenth_row(double beat, double* out_value,
                                       otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(otgy::core::beat_to_sixteenth_row(beat),
                                out_value, out_error);
  });
}

otgy_status otgy_sixteenth_row_to_beat(double row, double* out_value,
                                       otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(otgy::core::sixteenth_row_to_beat(row),
                                out_value, out_error);
  });
}

otgy_status otgy_adjust_offset_by_beats(
    otgy_timing_parameters baseline, double cumulative_beats,
    double* out_value, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_value == nullptr) {
      return invalid_argument(out_error);
    }
    return return_timing_result(
        otgy::core::adjust_offset_by_beats(
            otgy::c_api::to_core(baseline), cumulative_beats),
        out_value, out_error);
  });
}

otgy_status otgy_generate_timing_grid(
    otgy_timing_parameters timing, double begin_seconds, double end_seconds,
    otgy_timing_grid** out_grid, otgy_error** out_error) {
  if (out_grid != nullptr) {
    *out_grid = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_grid == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::generate_timing_grid(
        otgy::c_api::to_core(timing), begin_seconds, end_seconds);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_TIMING_GRID,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::timing_grid_error_message(result.error()));
    }
    std::vector<otgy_timing_grid_line> lines;
    lines.reserve(result.grid()->lines().size());
    for (const auto& line : result.grid()->lines()) {
      lines.push_back({line.sixteenth_row, line.seconds,
                       static_cast<std::uint32_t>(line.kind)});
    }
    return make_handle<otgy_timing_grid>(
        otgy_timing_grid{std::move(lines)}, out_grid, out_error);
  });
}

void otgy_timing_grid_destroy(otgy_timing_grid* grid) { delete grid; }

const otgy_timing_grid_line* otgy_timing_grid_get_lines(
    const otgy_timing_grid* grid) {
  return grid == nullptr || grid->lines.empty() ? nullptr : grid->lines.data();
}

std::uint64_t otgy_timing_grid_get_line_count(const otgy_timing_grid* grid) {
  return grid == nullptr ? 0U
                         : static_cast<std::uint64_t>(grid->lines.size());
}

otgy_status otgy_chart_calculate_timeline_range(
    const otgy_chart* chart, otgy_timing_parameters timing,
    std::uint8_t has_audio_end, double audio_end_seconds,
    otgy_timeline_range* out_range, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_range == nullptr || has_audio_end > 1U) {
      return invalid_argument(
          out_error,
          has_audio_end > 1U ? OTGY_ABI_ERROR_INVALID_BOOLEAN
                             : OTGY_ABI_ERROR_NULL_ARGUMENT,
          has_audio_end > 1U ? kInvalidBoolean : kNullArgument);
    }
    const std::optional<double> audio_end =
        has_audio_end == 0U ? std::nullopt
                            : std::optional<double>(audio_end_seconds);
    const auto result = otgy::core::calculate_chart_timeline_range(
        chart->value, otgy::c_api::to_core(timing), audio_end);
    if (!result) {
      std::string_view message = "chart timeline range failed";
      switch (result.failure()->error) {
        case otgy::core::ChartTimelineRangeError::None:
          break;
        case otgy::core::ChartTimelineRangeError::ChartProjectionFailed:
          message = "chart projection failed while calculating timeline range";
          break;
        case otgy::core::ChartTimelineRangeError::TimelineRangeFailed:
          message = "timing projection failed while calculating chart range";
          break;
        case otgy::core::ChartTimelineRangeError::AllocationFailure:
          message = "unable to allocate chart timeline range state";
          break;
      }
      return core_error(out_error, OTGY_ERROR_DOMAIN_CHART_TIMELINE_RANGE,
                        static_cast<std::uint32_t>(result.failure()->error),
                        message);
    }
    out_range->begin_seconds = result.range()->begin_seconds();
    out_range->end_seconds = result.range()->end_seconds();
    out_range->end_sixteenth_row = result.range()->end_sixteenth_row();
    out_range->uses_zero_bpm_projection =
        result.range()->uses_zero_bpm_projection() ? 1U : 0U;
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_calculate_timeline_navigation(
    otgy_timeline_range range, double current_sixteenth_row,
    double visible_beats, otgy_timeline_navigation* out_navigation,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_navigation == nullptr || range.uses_zero_bpm_projection > 1U) {
      return invalid_argument(
          out_error,
          range.uses_zero_bpm_projection > 1U
              ? OTGY_ABI_ERROR_INVALID_BOOLEAN
              : OTGY_ABI_ERROR_NULL_ARGUMENT,
          range.uses_zero_bpm_projection > 1U ? kInvalidBoolean
                                              : kNullArgument);
    }
    const otgy::core::TimelineRange core_range(
        range.begin_seconds, range.end_seconds, range.end_sixteenth_row,
        range.uses_zero_bpm_projection != 0U);
    const auto result = otgy::core::calculate_timeline_navigation(
        core_range, current_sixteenth_row, visible_beats);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_TIMELINE_NAVIGATION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::timeline_navigation_error_message(result.error()));
    }
    const auto* value = result.navigation();
    *out_navigation = {
        value->to_begin_row(), value->page_up_row(), value->page_down_row(),
        value->to_end_row(), static_cast<std::uint8_t>(value->can_to_begin()),
        static_cast<std::uint8_t>(value->can_page_up()),
        static_cast<std::uint8_t>(value->can_page_down()),
        static_cast<std::uint8_t>(value->can_to_end())};
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_start_playback(
    std::uint8_t has_audio, double audio_duration_seconds,
    double cursor_timeline_seconds, otgy_playback_position* out_position,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr || has_audio > 1U) {
      return invalid_argument(
          out_error,
          has_audio > 1U ? OTGY_ABI_ERROR_INVALID_BOOLEAN
                         : OTGY_ABI_ERROR_NULL_ARGUMENT,
          has_audio > 1U ? kInvalidBoolean : kNullArgument);
    }
    const std::optional<double> duration =
        has_audio == 0U ? std::nullopt
                        : std::optional<double>(audio_duration_seconds);
    return return_playback_result(
        otgy::core::start_playback(duration, cursor_timeline_seconds),
        out_position, out_error);
  });
}

otgy_status otgy_advance_playback(otgy_playback_position position,
                                  double elapsed_seconds,
                                  otgy_playback_position* out_position,
                                  otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_playback_result(
        otgy::core::advance_playback(to_core(position), elapsed_seconds),
        out_position, out_error);
  });
}

otgy_status otgy_advance_playback_frames(
    otgy_playback_position start_position, std::uint64_t played_frames,
    std::uint32_t sample_rate, otgy_playback_position* out_position,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_playback_result(
        otgy::core::advance_playback_frames(to_core(start_position),
                                            played_frames, sample_rate),
        out_position, out_error);
  });
}

otgy_status otgy_pause_playback(otgy_playback_position position,
                                otgy_playback_position* out_position,
                                otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_playback_result(otgy::core::pause_playback(to_core(position)),
                                  out_position, out_error);
  });
}

otgy_status otgy_render_playback_pcm(
    const otgy_pcm* pcm, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error) {
  if (out_playback_pcm != nullptr) {
    *out_playback_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || out_playback_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    auto result =
        otgy::core::render_playback_pcm(pcm->value, timeline_start_seconds);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_PCM,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::playback_pcm_error_message(result.error()));
    }
    return make_handle<otgy_playback_pcm>(
        otgy_playback_pcm{std::move(result).take_pcm()}, out_playback_pcm,
        out_error);
  });
}

void otgy_playback_pcm_destroy(otgy_playback_pcm* pcm) { delete pcm; }

std::uint32_t otgy_playback_pcm_get_sample_rate(
    const otgy_playback_pcm* pcm) {
  return pcm == nullptr ? 0U : pcm->value.sample_rate();
}

std::uint64_t otgy_playback_pcm_get_frame_count(
    const otgy_playback_pcm* pcm) {
  return pcm == nullptr ? 0U : pcm->value.frame_count();
}

otgy_byte_view otgy_playback_pcm_get_bytes(const otgy_playback_pcm* pcm) {
  if (pcm == nullptr || pcm->value.bytes().empty()) {
    return {nullptr, 0U};
  }
  return {pcm->value.bytes().data(),
          static_cast<std::uint64_t>(pcm->value.bytes().size())};
}

otgy_status otgy_playback_sound_bank_create(
    const otgy_playback_sound_asset* assets, std::uint64_t asset_count,
    otgy_playback_sound_bank** out_bank, otgy_error** out_error) {
  if (out_bank != nullptr) {
    *out_bank = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::size_t count = 0U;
    if (out_bank == nullptr || (assets == nullptr && asset_count != 0U)) {
      return invalid_argument(out_error);
    }
    if (!to_size(asset_count, count)) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE,
                              kLengthOutOfRange);
    }
    std::vector<otgy::core::PlaybackSoundAsset> core_assets;
    core_assets.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      std::size_t wav_size = 0U;
      if (!valid_bytes(assets[index].wav_bytes, wav_size)) {
        return invalid_argument(out_error, OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE,
                                kLengthOutOfRange);
      }
      core_assets.push_back(
          {static_cast<otgy::core::PlaybackSoundLanguage>(
               assets[index].language),
           static_cast<otgy::core::PlaybackSoundId>(assets[index].id),
           assets[index].wav_bytes.data, wav_size});
    }
    auto result = otgy::core::decode_playback_sound_bank(
        core_assets.data(), core_assets.size());
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_SOUND_BANK,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::playback_sound_bank_error_message(result.error()));
    }
    return make_handle<otgy_playback_sound_bank>(
        otgy_playback_sound_bank{std::move(result).take_bank()}, out_bank,
        out_error);
  });
}

void otgy_playback_sound_bank_destroy(otgy_playback_sound_bank* bank) {
  delete bank;
}

otgy_status otgy_render_chart_playback_pcm(
    const otgy_pcm* pcm, const otgy_chart* chart,
    const otgy_playback_sound_bank* sound_bank,
    otgy_playback_sound_config config, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error) {
  if (out_playback_pcm != nullptr) {
    *out_playback_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || chart == nullptr || sound_bank == nullptr ||
        out_playback_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    if (config.transition_sounds_enabled > 1U) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_INVALID_BOOLEAN,
                              kInvalidBoolean);
    }
    const otgy::core::PlaybackSoundConfig core_config{
        static_cast<otgy::core::PlaybackSoundLanguage>(config.language),
        static_cast<otgy::core::PlaybackDanceMode>(config.dance_mode),
        config.transition_sounds_enabled != 0U,
        config.random_seed,
        {config.bpm, config.offset_seconds}};
    auto result = otgy::core::render_chart_playback_pcm(
        pcm->value, chart->value, sound_bank->value, core_config,
        timeline_start_seconds);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_PCM,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::playback_pcm_error_message(result.error()));
    }
    return make_handle<otgy_playback_pcm>(
        otgy_playback_pcm{std::move(result).take_pcm()}, out_playback_pcm,
        out_error);
  });
}

otgy_status otgy_render_rhythm_test_playback_pcm(
    const otgy_pcm* pcm, const otgy_playback_sound_bank* sound_bank,
    otgy_rhythm_test_sound_config config, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error) {
  if (out_playback_pcm != nullptr) {
    *out_playback_pcm = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || sound_bank == nullptr ||
        out_playback_pcm == nullptr) {
      return invalid_argument(out_error);
    }
    const otgy::core::RhythmTestSoundConfig core_config{
        static_cast<otgy::core::PlaybackSoundLanguage>(config.language),
        {config.bpm, config.offset_seconds}};
    auto result = otgy::core::render_rhythm_test_playback_pcm(
        pcm->value, sound_bank->value, core_config, timeline_start_seconds);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_PCM,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::playback_pcm_error_message(result.error()));
    }
    return make_handle<otgy_playback_pcm>(
        otgy_playback_pcm{std::move(result).take_pcm()}, out_playback_pcm,
        out_error);
  });
}

otgy_status otgy_coordinate_application_operation(
    std::uint32_t mode, std::uint32_t operation, std::uint32_t* out_result,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_result == nullptr) {
      return invalid_argument(out_error);
    }
    *out_result = static_cast<std::uint32_t>(
        otgy::core::coordinate_application_operation(
            static_cast<otgy::core::ApplicationOperationMode>(mode),
            static_cast<otgy::core::ApplicationOperationClass>(operation)));
    return OTGY_STATUS_OK;
  });
}

}  // extern "C"
