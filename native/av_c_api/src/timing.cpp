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

otgy_status return_whole_beat_result(
    const otgy::core::WholeBeatCountResult& result, std::int64_t* out_value,
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

otgy_status otgy_nearest_whole_beat_offset(
    otgy_timing_parameters baseline, int64_t* out_beats,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_beats == nullptr) {
      return invalid_argument(out_error);
    }
    return return_whole_beat_result(
        otgy::core::nearest_whole_beat_offset(otgy::c_api::to_core(baseline)),
        out_beats, out_error);
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
  return otgy_chart_calculate_timeline_range_with_end(
      chart, timing, has_audio_end, audio_end_seconds, 0U, 0U, out_range,
      out_error);
}

otgy_status otgy_chart_calculate_timeline_range_with_end(
    const otgy_chart* chart, otgy_timing_parameters timing,
    std::uint8_t has_audio_end, double audio_end_seconds,
    std::uint8_t has_custom_end, std::uint64_t custom_end_sixteenth_row,
    otgy_timeline_range* out_range, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_range == nullptr || has_audio_end > 1U ||
        has_custom_end > 1U) {
      return invalid_argument(
          out_error,
          has_audio_end > 1U || has_custom_end > 1U
              ? OTGY_ABI_ERROR_INVALID_BOOLEAN
              : OTGY_ABI_ERROR_NULL_ARGUMENT,
          has_audio_end > 1U || has_custom_end > 1U ? kInvalidBoolean
                                                    : kNullArgument);
    }
    const std::optional<double> audio_end =
        has_audio_end == 0U ? std::nullopt
                            : std::optional<double>(audio_end_seconds);
    const auto custom_end =
        has_custom_end == 0U
            ? std::nullopt
            : std::optional<std::uint64_t>(custom_end_sixteenth_row);
    const auto result = otgy::core::calculate_chart_timeline_range_with_end(
        chart->value, otgy::c_api::to_core(timing), audio_end, custom_end);
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

otgy_status otgy_calculate_playback_frame_limit(
    std::uint32_t sample_rate, double keep_seconds,
    std::uint64_t* out_frame_count, otgy_error** out_error) {
  if (out_frame_count != nullptr) {
    *out_frame_count = 0U;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_frame_count == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result =
        otgy::core::calculate_playback_frame_limit(sample_rate, keep_seconds);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_PCM,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::playback_pcm_error_message(result.error()));
    }
    *out_frame_count = *result.frame_count();
    return OTGY_STATUS_OK;
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

otgy_status otgy_playback_engine_create(
    const otgy_pcm* pcm, const otgy_chart* chart,
    const otgy_playback_sound_bank* sound_bank,
    otgy_playback_engine_config config, otgy_playback_engine** out_engine,
    otgy_error** out_error) {
  if (out_engine != nullptr) {
    *out_engine = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (pcm == nullptr || chart == nullptr || sound_bank == nullptr ||
        out_engine == nullptr) {
      return invalid_argument(out_error);
    }
    if (config.transition_sounds_enabled > 1U ||
        config.judgement_enabled > 1U) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_INVALID_BOOLEAN,
                              kInvalidBoolean);
    }
    const otgy::core::JudgementConfig judgement{
        {config.bpm, config.offset_seconds},
        static_cast<otgy::core::JudgementWindowBasis>(
            config.judgement_basis),
        config.judgement_perfect_fraction,
        config.judgement_great_fraction,
        config.judgement_cool_fraction,
        config.judgement_bad_fraction,
        config.judgement_offset_ms};
    auto* engine = new (std::nothrow) otgy_playback_engine{};
    if (engine == nullptr) {
      return out_of_memory(out_error);
    }
    const auto error = engine->value.initialize(
        pcm->value, chart->value, sound_bank->value,
        {static_cast<otgy::core::PlaybackSoundLanguage>(config.language),
         static_cast<otgy::core::PlaybackDanceMode>(config.dance_mode),
         config.transition_sounds_enabled != 0U, config.random_seed,
         {config.bpm, config.offset_seconds}, config.start_seconds,
         config.end_seconds, judgement,
         config.judgement_enabled != 0U});
    if (error != otgy::core::PlaybackEngineError::None) {
      delete engine;
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_ENGINE,
          static_cast<std::uint32_t>(error),
          otgy::core::playback_engine_error_message(error));
    }
    *out_engine = engine;
    return OTGY_STATUS_OK;
  });
}

void otgy_playback_engine_destroy(otgy_playback_engine* engine) {
  delete engine;
}

std::uint32_t otgy_playback_engine_get_sample_rate(
    const otgy_playback_engine* engine) {
  return engine == nullptr ? 0U : engine->value.sample_rate();
}

void otgy_playback_engine_push_trigger(otgy_playback_engine* engine,
                                       std::uint32_t sound_id) {
  if (engine == nullptr) {
    return;
  }
  engine->value.push_trigger(
      static_cast<otgy::core::PlaybackSoundId>(sound_id));
}

void otgy_playback_engine_render(otgy_playback_engine* engine,
                                 std::uint8_t* interleaved_pcm16le,
                                 std::uint32_t frame_count) {
  if (engine == nullptr || interleaved_pcm16le == nullptr ||
      frame_count == 0U) {
    return;
  }
  engine->value.render(interleaved_pcm16le, frame_count);
}

void otgy_playback_engine_drain_judgements(
    otgy_playback_engine* engine, otgy_playback_judgement* out_judgements,
    std::uint32_t capacity, std::uint32_t* out_count) {
  if (out_count != nullptr) {
    *out_count = 0U;
  }
  if (engine == nullptr || out_judgements == nullptr || capacity == 0U) {
    return;
  }
  otgy::core::JudgementResult buffer[16]{};
  std::uint32_t total = 0U;
  while (total < capacity) {
    const auto request = (capacity - total) < 16U ? (capacity - total) : 16U;
    const auto count = engine->value.drain_judgements(buffer, request);
    if (count == 0U) {
      break;
    }
    for (std::size_t index = 0U; index < count; ++index) {
      out_judgements[total++] = otgy_playback_judgement{
          static_cast<std::uint32_t>(buffer[index].grade),
          buffer[index].delta_ms,
          buffer[index].note_id.value,
          static_cast<std::uint32_t>(buffer[index].expected_sound),
          static_cast<std::uint32_t>(buffer[index].pressed_sound),
          buffer[index].note_seconds,
          buffer[index].press_seconds,
          buffer[index].queue_delay_ms};
    }
    if (count < request) {
      break;
    }
  }
  if (out_count != nullptr) {
    *out_count = total;
  }
}

void otgy_playback_engine_get_state(
    const otgy_playback_engine* engine,
    otgy_playback_engine_state* out_state) {
  if (out_state == nullptr) {
    return;
  }
  if (engine == nullptr) {
    *out_state = otgy_playback_engine_state{0U, 0.0, 1U};
    return;
  }
  const auto state = engine->value.state();
  *out_state = otgy_playback_engine_state{
      state.rendered_frames, state.position_seconds,
      static_cast<std::uint8_t>(state.finished ? 1U : 0U)};
}

void otgy_playback_engine_reset(otgy_playback_engine* engine) {
  if (engine != nullptr) {
    engine->value.reset();
  }
}

std::uint8_t otgy_playback_engine_is_finished(
    const otgy_playback_engine* engine) {
  if (engine == nullptr) {
    return 1U;
  }
  otgy_playback_engine_state state{};
  otgy_playback_engine_get_state(engine, &state);
  return state.finished;
}

std::uint64_t otgy_playback_engine_get_rendered_frames(
    const otgy_playback_engine* engine) {
  if (engine == nullptr) {
    return 0U;
  }
  otgy_playback_engine_state state{};
  otgy_playback_engine_get_state(engine, &state);
  return state.rendered_frames;
}

otgy_status otgy_playback_judgement_windows_for(
    double bpm, std::uint32_t basis, double perfect_fraction,
    double great_fraction, double cool_fraction, double bad_fraction,
    otgy_playback_judgement_windows* out_windows, otgy_error** out_error) {
  if (out_windows != nullptr) {
    *out_windows = {};
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_windows == nullptr) {
      return invalid_argument(out_error);
    }
    const otgy::core::JudgementConfig config{
        {bpm, 0.0},
        static_cast<otgy::core::JudgementWindowBasis>(basis),
        perfect_fraction,
        great_fraction,
        cool_fraction,
        bad_fraction,
        0.0};
    const auto windows = otgy::core::judgement_windows_ms(config);
    if (!windows) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PLAYBACK_ENGINE,
          static_cast<std::uint32_t>(
              otgy::core::PlaybackEngineError::InvalidConfiguration),
          otgy::core::playback_engine_error_message(
              otgy::core::PlaybackEngineError::InvalidConfiguration));
    }
    out_windows->unit_ms = windows->unit;
    out_windows->perfect_ms = windows->perfect;
    out_windows->great_ms = windows->great;
    out_windows->cool_ms = windows->cool;
    out_windows->bad_ms = windows->bad;
    return OTGY_STATUS_OK;
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
