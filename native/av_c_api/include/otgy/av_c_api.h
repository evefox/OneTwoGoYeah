// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef OTGY_AV_C_API_H
#define OTGY_AV_C_API_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(OTGY_C_API_BUILD)
#define OTGY_C_API __declspec(dllexport)
#else
#define OTGY_C_API __declspec(dllimport)
#endif
#else
#define OTGY_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OTGY_C_API_VERSION UINT32_C(2)

typedef uint32_t otgy_status;
#define OTGY_STATUS_OK UINT32_C(0)
#define OTGY_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define OTGY_STATUS_OUT_OF_MEMORY UINT32_C(2)
#define OTGY_STATUS_CORE_ERROR UINT32_C(3)
#define OTGY_STATUS_UNEXPECTED_EXCEPTION UINT32_C(4)

typedef uint32_t otgy_error_domain;
#define OTGY_ERROR_DOMAIN_ABI UINT32_C(1)
#define OTGY_ERROR_DOMAIN_PCM UINT32_C(2)
#define OTGY_ERROR_DOMAIN_MP3_DECODE UINT32_C(3)
#define OTGY_ERROR_DOMAIN_OGG_VORBIS_DECODE UINT32_C(4)
#define OTGY_ERROR_DOMAIN_PCM_OFFSET UINT32_C(5)
#define OTGY_ERROR_DOMAIN_AUDIO_EXPORT_NAME UINT32_C(6)
#define OTGY_ERROR_DOMAIN_AUDIO_EXPORT_QUALITY UINT32_C(7)
#define OTGY_ERROR_DOMAIN_AUDIO_EXPORT_SNAPSHOT UINT32_C(8)
#define OTGY_ERROR_DOMAIN_AUDIO_EXPORT_TASK UINT32_C(9)
#define OTGY_ERROR_DOMAIN_WAVEFORM UINT32_C(10)
#define OTGY_ERROR_DOMAIN_ONSET_DETECTION UINT32_C(11)
#define OTGY_ERROR_DOMAIN_BPM_DETECTION UINT32_C(12)
#define OTGY_ERROR_DOMAIN_OFFSET_DETECTION UINT32_C(13)
#define OTGY_ERROR_DOMAIN_SYNC_ANALYSIS UINT32_C(14)
#define OTGY_ERROR_DOMAIN_TIMING_CONVERSION UINT32_C(15)
#define OTGY_ERROR_DOMAIN_TIMING_GRID UINT32_C(16)
#define OTGY_ERROR_DOMAIN_TIMELINE_RANGE UINT32_C(17)
#define OTGY_ERROR_DOMAIN_TIMELINE_NAVIGATION UINT32_C(18)
#define OTGY_ERROR_DOMAIN_PLAYBACK_POSITION UINT32_C(19)
#define OTGY_ERROR_DOMAIN_CHART_PROJECTION UINT32_C(20)
#define OTGY_ERROR_DOMAIN_CHART_NOTE_ORDINAL UINT32_C(21)
#define OTGY_ERROR_DOMAIN_CHART_EDIT UINT32_C(22)
#define OTGY_ERROR_DOMAIN_CHART_VALIDATION UINT32_C(23)
#define OTGY_ERROR_DOMAIN_PROJECT_SERIALIZATION UINT32_C(24)
#define OTGY_ERROR_DOMAIN_SLK_IMPORT UINT32_C(25)
#define OTGY_ERROR_DOMAIN_SLK_CHART_PROJECTION UINT32_C(26)
#define OTGY_ERROR_DOMAIN_SLK_EXPORT UINT32_C(27)
#define OTGY_ERROR_DOMAIN_SLK_POSITION UINT32_C(28)
#define OTGY_ERROR_DOMAIN_CHART_TIMELINE_RANGE UINT32_C(29)
#define OTGY_ERROR_DOMAIN_PLAYBACK_PCM UINT32_C(30)
#define OTGY_ERROR_DOMAIN_PLAYBACK_SOUND_BANK UINT32_C(31)
#define OTGY_ERROR_DOMAIN_AUDIO_END UINT32_C(32)
#define OTGY_ERROR_DOMAIN_PLAYBACK_ENGINE UINT32_C(33)

#define OTGY_ABI_ERROR_NULL_ARGUMENT UINT32_C(1)
#define OTGY_ABI_ERROR_LENGTH_OUT_OF_RANGE UINT32_C(2)
#define OTGY_ABI_ERROR_INVALID_BOOLEAN UINT32_C(3)
#define OTGY_ABI_ERROR_RESULT_NOT_AVAILABLE UINT32_C(4)
#define OTGY_ABI_ERROR_UNEXPECTED_EXCEPTION UINT32_C(5)

typedef struct otgy_string_view {
  const char* data;
  uint64_t length;
} otgy_string_view;

typedef struct otgy_byte_view {
  const uint8_t* data;
  uint64_t length;
} otgy_byte_view;

/*
 * Ownership contract for ABI version 2:
 *
 * - Every non-null opaque handle returned through an out parameter is owned
 *   by the caller and has exactly one matching *_destroy function.
 * - Every *_destroy function accepts NULL. Handles from different destroy
 *   families are never interchangeable.
 * - Pointer/length views returned by getters borrow storage from their owning
 *   handle and remain valid only until that handle is destroyed.
 * - Input views are borrowed only for the synchronous duration of the call.
 * - A returned otgy_error is independently owned and must be destroyed even
 *   when the caller ignores every other failed output.
 * - Failed calls set owning output handles to NULL and never publish partial
 *   arrays or byte buffers.
 */

typedef struct otgy_error otgy_error;
typedef struct otgy_bytes otgy_bytes;
typedef struct otgy_pcm otgy_pcm;
typedef struct otgy_audio_export_snapshot otgy_audio_export_snapshot;
typedef struct otgy_audio_export_output otgy_audio_export_output;
typedef struct otgy_audio_export_cancel_flag otgy_audio_export_cancel_flag;
typedef struct otgy_waveform_tile otgy_waveform_tile;
typedef struct otgy_onsets otgy_onsets;
typedef struct otgy_bpm_candidates otgy_bpm_candidates;
typedef struct otgy_offset_candidates otgy_offset_candidates;
typedef struct otgy_sync_analysis otgy_sync_analysis;
typedef struct otgy_timing_grid otgy_timing_grid;
typedef struct otgy_chart otgy_chart;
typedef struct otgy_chart_semantics otgy_chart_semantics;
typedef struct otgy_chart_markers otgy_chart_markers;
typedef struct otgy_chart_selections otgy_chart_selections;
typedef struct otgy_chart_paste_preview otgy_chart_paste_preview;
typedef struct otgy_showtime_moves otgy_showtime_moves;
typedef struct otgy_chart_editor otgy_chart_editor;
typedef struct otgy_chart_validation otgy_chart_validation;
typedef struct otgy_finished_chart otgy_finished_chart;
typedef struct otgy_slk_document otgy_slk_document;
typedef struct otgy_playback_pcm otgy_playback_pcm;
typedef struct otgy_playback_sound_bank otgy_playback_sound_bank;

/* Values accepted by otgy_generate_waveform_tile. */
#define OTGY_WAVEFORM_SHAPE_SIGNED UINT32_C(0)
#define OTGY_WAVEFORM_SHAPE_RECTIFIED UINT32_C(1)
#define OTGY_WAVEFORM_LUMINANCE_UNIFORM UINT32_C(0)
#define OTGY_WAVEFORM_LUMINANCE_AMPLITUDE UINT32_C(1)
#define OTGY_WAVEFORM_ANTIALIAS_NONE UINT32_C(0)
#define OTGY_WAVEFORM_ANTIALIAS_2X UINT32_C(1)
#define OTGY_WAVEFORM_ANTIALIAS_3X UINT32_C(2)
#define OTGY_WAVEFORM_ANTIALIAS_4X UINT32_C(3)
#define OTGY_WAVEFORM_FILTER_NONE UINT32_C(0)
#define OTGY_WAVEFORM_FILTER_HIGH_PASS UINT32_C(1)
#define OTGY_WAVEFORM_FILTER_LOW_PASS UINT32_C(2)

OTGY_C_API uint32_t otgy_c_api_version(void);
OTGY_C_API otgy_string_view otgy_core_version(void);

OTGY_C_API void otgy_error_destroy(otgy_error* error);
OTGY_C_API otgy_error_domain otgy_error_get_domain(const otgy_error* error);
OTGY_C_API uint32_t otgy_error_get_code(const otgy_error* error);
OTGY_C_API otgy_string_view otgy_error_get_message(const otgy_error* error);

OTGY_C_API void otgy_bytes_destroy(otgy_bytes* bytes);
OTGY_C_API otgy_byte_view otgy_bytes_get_view(const otgy_bytes* bytes);

typedef struct otgy_audio_source_info {
  uint32_t codec;
  uint64_t measured_bitrate_bps;
  uint64_t nominal_bitrate_bps;
} otgy_audio_source_info;

#define OTGY_AUDIO_SOURCE_CODEC_MP3 UINT32_C(1)
#define OTGY_AUDIO_SOURCE_CODEC_OGG_VORBIS UINT32_C(2)

OTGY_C_API otgy_status otgy_pcm_copy_from_channels(
    uint32_t sample_rate, const int16_t* left, uint64_t left_frame_count,
    const int16_t* right, uint64_t right_frame_count, otgy_pcm** out_pcm,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_decode_mp3(otgy_byte_view input,
                                       otgy_pcm** out_pcm,
                                       otgy_error** out_error);
OTGY_C_API otgy_status otgy_decode_ogg_vorbis(otgy_byte_view input,
                                              otgy_pcm** out_pcm,
                                              otgy_error** out_error);
OTGY_C_API void otgy_pcm_destroy(otgy_pcm* pcm);
OTGY_C_API uint32_t otgy_pcm_get_sample_rate(const otgy_pcm* pcm);
OTGY_C_API uint64_t otgy_pcm_get_frame_count(const otgy_pcm* pcm);
OTGY_C_API const int16_t* otgy_pcm_get_left(const otgy_pcm* pcm);
OTGY_C_API const int16_t* otgy_pcm_get_right(const otgy_pcm* pcm);
OTGY_C_API uint8_t otgy_pcm_get_source_info(
    const otgy_pcm* pcm, otgy_audio_source_info* out_source_info);
OTGY_C_API otgy_status otgy_pcm_sha256(const otgy_pcm* pcm,
                                       otgy_bytes** out_hash,
                                       otgy_error** out_error);

typedef struct otgy_pcm_offset_plan {
  int64_t signed_shift_frames;
  uint64_t output_frame_count;
} otgy_pcm_offset_plan;

OTGY_C_API otgy_status otgy_calculate_pcm_offset_plan(
    uint32_t sample_rate, uint64_t frame_count, double offset_seconds,
    otgy_pcm_offset_plan* out_plan, otgy_error** out_error);
OTGY_C_API otgy_status otgy_apply_pcm_offset(const otgy_pcm* pcm,
                                             double offset_seconds,
                                             otgy_pcm** out_pcm,
                                             otgy_error** out_error);

typedef struct otgy_audio_export_local_time {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} otgy_audio_export_local_time;

OTGY_C_API otgy_status otgy_normalize_audio_export_base_name(
    otgy_string_view input, otgy_bytes** out_utf8, otgy_error** out_error);
OTGY_C_API otgy_status otgy_default_audio_export_base_name(
    otgy_string_view source_file_name, otgy_bytes** out_utf8,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_make_audio_export_file_name(
    otgy_string_view base_name, otgy_audio_export_local_time local_time,
    uint32_t collision_index, otgy_bytes** out_utf8,
    otgy_error** out_error);

typedef struct otgy_audio_export_quality_policy {
  uint64_t nominal_bitrates_bps[5];
  uint64_t source_bitrate_bps;
  uint8_t quality_ceiling;
  uint8_t default_quality;
} otgy_audio_export_quality_policy;

OTGY_C_API otgy_status otgy_calculate_audio_export_quality(
    uint32_t sample_rate, otgy_audio_source_info source_info,
    otgy_audio_export_quality_policy* out_policy, otgy_error** out_error);
OTGY_C_API otgy_status otgy_validate_audio_export_quality(
    const otgy_audio_export_quality_policy* policy, uint8_t quality,
    otgy_error** out_error);

typedef struct otgy_audio_export_metadata {
  otgy_string_view title;
  otgy_string_view artist;
  otgy_string_view album;
} otgy_audio_export_metadata;

#define OTGY_AUDIO_EXPORT_STAGE_PREPARING_AUDIO UINT32_C(1)
#define OTGY_AUDIO_EXPORT_STAGE_ENCODING UINT32_C(2)
#define OTGY_AUDIO_EXPORT_STAGE_SAVING UINT32_C(3)

typedef void (*otgy_audio_export_progress_callback)(
    uint32_t stage, uint64_t completed_units, uint64_t total_units,
    void* context);
typedef uint8_t (*otgy_cancel_callback)(void* context);

OTGY_C_API otgy_status otgy_audio_export_cancel_flag_create(
    otgy_audio_export_cancel_flag** out_flag, otgy_error** out_error);
OTGY_C_API void otgy_audio_export_cancel_flag_destroy(
    otgy_audio_export_cancel_flag* flag);
OTGY_C_API void otgy_audio_export_cancel_flag_request(
    otgy_audio_export_cancel_flag* flag);
OTGY_C_API uint8_t otgy_audio_export_cancel_flag_is_requested(
    const otgy_audio_export_cancel_flag* flag);

#define OTGY_AUDIO_END_MODE_SOURCE_END UINT8_C(0)
#define OTGY_AUDIO_END_MODE_BEAT UINT8_C(1)

OTGY_C_API otgy_status otgy_audio_export_snapshot_create(
    const otgy_pcm* pcm, uint64_t pcm_identity, double offset_seconds,
    uint8_t quality, otgy_audio_source_info source_info,
    otgy_string_view requested_base_name, otgy_audio_export_metadata metadata,
    otgy_audio_export_snapshot** out_snapshot, otgy_error** out_error);
OTGY_C_API otgy_status otgy_audio_export_snapshot_create_with_end(
    const otgy_pcm* pcm, uint64_t pcm_identity, double offset_seconds,
    double bpm, uint8_t end_mode, uint64_t end_sixteenth_row,
    uint8_t quality, otgy_audio_source_info source_info,
    otgy_string_view requested_base_name, otgy_audio_export_metadata metadata,
    otgy_audio_export_snapshot** out_snapshot, otgy_error** out_error);
OTGY_C_API otgy_status otgy_calculate_audio_end_export_seconds(
    uint8_t end_mode, uint64_t end_sixteenth_row, double bpm,
    double offset_seconds, double audio_duration_seconds,
    double* out_seconds, otgy_error** out_error);
OTGY_C_API void otgy_audio_export_snapshot_destroy(
    otgy_audio_export_snapshot* snapshot);
OTGY_C_API otgy_status otgy_audio_export_snapshot_encode(
    const otgy_audio_export_snapshot* snapshot,
    otgy_audio_export_progress_callback progress_callback,
    otgy_cancel_callback cancel_callback, void* callback_context,
    otgy_audio_export_output** out_output, otgy_error** out_error);
OTGY_C_API void otgy_audio_export_output_destroy(
    otgy_audio_export_output* output);
OTGY_C_API otgy_byte_view otgy_audio_export_output_get_bytes(
    const otgy_audio_export_output* output);
OTGY_C_API int64_t otgy_audio_export_output_get_signed_shift_frames(
    const otgy_audio_export_output* output);
OTGY_C_API uint64_t otgy_audio_export_output_get_frame_count(
    const otgy_audio_export_output* output);

OTGY_C_API void otgy_waveform_tile_destroy(otgy_waveform_tile* tile);
OTGY_C_API uint64_t otgy_waveform_tile_get_block_index(
    const otgy_waveform_tile* tile);
OTGY_C_API uint32_t otgy_waveform_tile_get_pixels_per_second(
    const otgy_waveform_tile* tile);
OTGY_C_API uint16_t otgy_waveform_tile_get_channel_width(
    const otgy_waveform_tile* tile);
OTGY_C_API uint16_t otgy_waveform_tile_get_channel_spacing(
    const otgy_waveform_tile* tile);
OTGY_C_API otgy_byte_view otgy_waveform_tile_get_left(
    const otgy_waveform_tile* tile);
OTGY_C_API otgy_byte_view otgy_waveform_tile_get_right(
    const otgy_waveform_tile* tile);

OTGY_C_API otgy_status otgy_generate_waveform_tile(
    const otgy_pcm* pcm, double ui_zoom, double scale, uint64_t block_index,
    uint32_t shape, uint32_t luminance, uint32_t anti_aliasing, uint32_t filter,
    double filter_strength, otgy_waveform_tile** out_tile,
    otgy_error** out_error);

#define OTGY_DECLARE_WAVEFORM(name)                                           \
  OTGY_C_API otgy_status otgy_generate_##name##_waveform_tile(                \
      const otgy_pcm* pcm, double ui_zoom, double scale,                      \
      uint64_t block_index, otgy_waveform_tile** out_tile,                    \
      otgy_error** out_error)

OTGY_DECLARE_WAVEFORM(signed_uniform_1x);
OTGY_DECLARE_WAVEFORM(rectified_uniform_1x);
OTGY_DECLARE_WAVEFORM(signed_amplitude_1x);
OTGY_DECLARE_WAVEFORM(rectified_amplitude_1x);
OTGY_DECLARE_WAVEFORM(signed_uniform_2x);
OTGY_DECLARE_WAVEFORM(rectified_uniform_2x);
OTGY_DECLARE_WAVEFORM(signed_amplitude_2x);
OTGY_DECLARE_WAVEFORM(rectified_amplitude_2x);
OTGY_DECLARE_WAVEFORM(signed_uniform_3x);
OTGY_DECLARE_WAVEFORM(rectified_uniform_3x);
OTGY_DECLARE_WAVEFORM(signed_amplitude_3x);
OTGY_DECLARE_WAVEFORM(rectified_amplitude_3x);
OTGY_DECLARE_WAVEFORM(signed_uniform_4x);
OTGY_DECLARE_WAVEFORM(rectified_uniform_4x);
OTGY_DECLARE_WAVEFORM(signed_amplitude_4x);
OTGY_DECLARE_WAVEFORM(rectified_amplitude_4x);
#undef OTGY_DECLARE_WAVEFORM

typedef struct otgy_onset {
  uint64_t frame_index;
  double strength;
} otgy_onset;

typedef struct otgy_bpm_candidate {
  double bpm;
  double fitness;
} otgy_bpm_candidate;

typedef struct otgy_offset_candidate {
  double bpm;
  double detector_offset_seconds;
  double app_offset_seconds;
  double fitness;
} otgy_offset_candidate;

OTGY_C_API otgy_status otgy_detect_onsets(const otgy_pcm* pcm,
                                          otgy_onsets** out_onsets,
                                          otgy_error** out_error);
OTGY_C_API void otgy_onsets_destroy(otgy_onsets* onsets);
OTGY_C_API const otgy_onset* otgy_onsets_get_data(const otgy_onsets* onsets);
OTGY_C_API uint64_t otgy_onsets_get_count(const otgy_onsets* onsets);

OTGY_C_API otgy_status otgy_detect_bpm_candidates(
    const otgy_pcm* pcm, otgy_bpm_candidates** out_candidates,
    otgy_error** out_error);
OTGY_C_API void otgy_bpm_candidates_destroy(otgy_bpm_candidates* candidates);
OTGY_C_API const otgy_bpm_candidate* otgy_bpm_candidates_get_data(
    const otgy_bpm_candidates* candidates);
OTGY_C_API uint64_t otgy_bpm_candidates_get_count(
    const otgy_bpm_candidates* candidates);

OTGY_C_API otgy_status otgy_detect_offsets(
    const otgy_pcm* pcm, const otgy_bpm_candidate* candidates,
    uint64_t candidate_count, otgy_offset_candidates** out_candidates,
    otgy_error** out_error);
OTGY_C_API void otgy_offset_candidates_destroy(
    otgy_offset_candidates* candidates);
OTGY_C_API const otgy_offset_candidate* otgy_offset_candidates_get_data(
    const otgy_offset_candidates* candidates);
OTGY_C_API uint64_t otgy_offset_candidates_get_count(
    const otgy_offset_candidates* candidates);

typedef void (*otgy_sync_stage_callback)(uint32_t stage, void* context);
#define OTGY_SYNC_STAGE_DETECTING_ONSETS UINT32_C(0)
#define OTGY_SYNC_STAGE_SCANNING_INTERVALS UINT32_C(1)
#define OTGY_SYNC_STAGE_REFINING_CANDIDATES UINT32_C(2)
#define OTGY_SYNC_STAGE_SELECTING_BPM UINT32_C(3)
#define OTGY_SYNC_STAGE_CALCULATING_OFFSETS UINT32_C(4)
#define OTGY_SYNC_STAGE_FINALIZING_RESULTS UINT32_C(5)
OTGY_C_API otgy_status otgy_analyze_sync(
    const otgy_pcm* pcm, otgy_sync_stage_callback stage_callback,
    void* stage_context, otgy_sync_analysis** out_analysis,
    otgy_error** out_error);
OTGY_C_API void otgy_sync_analysis_destroy(otgy_sync_analysis* analysis);
OTGY_C_API uint64_t otgy_sync_analysis_get_analyzed_frame_count(
    const otgy_sync_analysis* analysis);
OTGY_C_API const otgy_offset_candidate* otgy_sync_analysis_get_candidates(
    const otgy_sync_analysis* analysis);
OTGY_C_API uint64_t otgy_sync_analysis_get_candidate_count(
    const otgy_sync_analysis* analysis);

typedef struct otgy_timing_parameters {
  double bpm;
  double offset_seconds;
} otgy_timing_parameters;

OTGY_C_API otgy_status otgy_seconds_to_beat(
    otgy_timing_parameters timing, double seconds, double* out_value,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_beat_to_seconds(
    otgy_timing_parameters timing, double beat, double* out_value,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_seconds_to_sixteenth_row(
    otgy_timing_parameters timing, double seconds, double* out_value,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_sixteenth_row_to_seconds(
    otgy_timing_parameters timing, double row, double* out_value,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_beat_to_sixteenth_row(
    double beat, double* out_value, otgy_error** out_error);
OTGY_C_API otgy_status otgy_sixteenth_row_to_beat(
    double row, double* out_value, otgy_error** out_error);
OTGY_C_API otgy_status otgy_adjust_offset_by_beats(
    otgy_timing_parameters baseline, double cumulative_beats,
    double* out_value, otgy_error** out_error);
OTGY_C_API otgy_status otgy_nearest_whole_beat_offset(
    otgy_timing_parameters baseline, int64_t* out_beats,
    otgy_error** out_error);

typedef struct otgy_timing_grid_line {
  uint64_t sixteenth_row;
  double seconds;
  uint32_t kind;
} otgy_timing_grid_line;

#define OTGY_TIMING_GRID_LINE_MEASURE UINT32_C(0)
#define OTGY_TIMING_GRID_LINE_BEAT UINT32_C(1)
#define OTGY_TIMING_GRID_LINE_SIXTEENTH UINT32_C(2)

OTGY_C_API otgy_status otgy_generate_timing_grid(
    otgy_timing_parameters timing, double begin_seconds, double end_seconds,
    otgy_timing_grid** out_grid, otgy_error** out_error);
OTGY_C_API void otgy_timing_grid_destroy(otgy_timing_grid* grid);
OTGY_C_API const otgy_timing_grid_line* otgy_timing_grid_get_lines(
    const otgy_timing_grid* grid);
OTGY_C_API uint64_t otgy_timing_grid_get_line_count(
    const otgy_timing_grid* grid);

typedef struct otgy_timeline_range {
  double begin_seconds;
  double end_seconds;
  double end_sixteenth_row;
  uint8_t uses_zero_bpm_projection;
} otgy_timeline_range;

OTGY_C_API otgy_status otgy_chart_calculate_timeline_range(
    const otgy_chart* chart, otgy_timing_parameters timing,
    uint8_t has_audio_end, double audio_end_seconds,
    otgy_timeline_range* out_range, otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_calculate_timeline_range_with_end(
    const otgy_chart* chart, otgy_timing_parameters timing,
    uint8_t has_audio_end, double audio_end_seconds, uint8_t has_custom_end,
    uint64_t custom_end_sixteenth_row, otgy_timeline_range* out_range,
    otgy_error** out_error);

typedef struct otgy_timeline_navigation {
  double to_begin_row;
  double page_up_row;
  double page_down_row;
  double to_end_row;
  uint8_t can_to_begin;
  uint8_t can_page_up;
  uint8_t can_page_down;
  uint8_t can_to_end;
} otgy_timeline_navigation;

OTGY_C_API otgy_status otgy_calculate_timeline_navigation(
    otgy_timeline_range range, double current_sixteenth_row,
    double visible_beats, otgy_timeline_navigation* out_navigation,
    otgy_error** out_error);

typedef struct otgy_playback_position {
  uint32_t phase;
  double timeline_seconds;
  double audio_duration_seconds;
} otgy_playback_position;

#define OTGY_PLAYBACK_PHASE_PAUSED UINT32_C(0)
#define OTGY_PLAYBACK_PHASE_PLAYING UINT32_C(1)

OTGY_C_API otgy_status otgy_start_playback(
    uint8_t has_audio, double audio_duration_seconds,
    double cursor_timeline_seconds, otgy_playback_position* out_position,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_advance_playback(
    otgy_playback_position position, double elapsed_seconds,
    otgy_playback_position* out_position, otgy_error** out_error);
OTGY_C_API otgy_status otgy_advance_playback_frames(
    otgy_playback_position start_position, uint64_t played_frames,
    uint32_t sample_rate, otgy_playback_position* out_position,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_pause_playback(
    otgy_playback_position position, otgy_playback_position* out_position,
    otgy_error** out_error);

OTGY_C_API otgy_status otgy_render_playback_pcm(
    const otgy_pcm* pcm, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error);
OTGY_C_API otgy_status otgy_calculate_playback_frame_limit(
    uint32_t sample_rate, double keep_seconds, uint64_t* out_frame_count,
    otgy_error** out_error);
OTGY_C_API void otgy_playback_pcm_destroy(otgy_playback_pcm* pcm);
OTGY_C_API uint32_t otgy_playback_pcm_get_sample_rate(
    const otgy_playback_pcm* pcm);
OTGY_C_API uint64_t otgy_playback_pcm_get_frame_count(
    const otgy_playback_pcm* pcm);
OTGY_C_API otgy_byte_view otgy_playback_pcm_get_bytes(
    const otgy_playback_pcm* pcm);

#define OTGY_PLAYBACK_SOUND_LANGUAGE_CHINESE UINT32_C(0)
#define OTGY_PLAYBACK_SOUND_LANGUAGE_ENGLISH UINT32_C(1)
#define OTGY_PLAYBACK_DANCE_MODE_4K UINT32_C(0)
#define OTGY_PLAYBACK_DANCE_MODE_8K UINT32_C(1)

#define OTGY_PLAYBACK_SOUND_F1_1 UINT32_C(0)
#define OTGY_PLAYBACK_SOUND_F1_2 UINT32_C(1)
#define OTGY_PLAYBACK_SOUND_F1_3 UINT32_C(2)
#define OTGY_PLAYBACK_SOUND_F1_4 UINT32_C(3)
#define OTGY_PLAYBACK_SOUND_F1_6 UINT32_C(4)
#define OTGY_PLAYBACK_SOUND_F1_7 UINT32_C(5)
#define OTGY_PLAYBACK_SOUND_F1_8 UINT32_C(6)
#define OTGY_PLAYBACK_SOUND_F1_9 UINT32_C(7)
#define OTGY_PLAYBACK_SOUND_F1_GO UINT32_C(8)
#define OTGY_PLAYBACK_SOUND_F1_YEAH UINT32_C(9)
#define OTGY_PLAYBACK_SOUND_M1_1 UINT32_C(10)
#define OTGY_PLAYBACK_SOUND_M1_2 UINT32_C(11)
#define OTGY_PLAYBACK_SOUND_M1_3 UINT32_C(12)
#define OTGY_PLAYBACK_SOUND_M1_4 UINT32_C(13)
#define OTGY_PLAYBACK_SOUND_M1_6 UINT32_C(14)
#define OTGY_PLAYBACK_SOUND_M1_7 UINT32_C(15)
#define OTGY_PLAYBACK_SOUND_M1_8 UINT32_C(16)
#define OTGY_PLAYBACK_SOUND_M1_9 UINT32_C(17)
#define OTGY_PLAYBACK_SOUND_M1_GO UINT32_C(18)
#define OTGY_PLAYBACK_SOUND_M1_YEAH UINT32_C(19)
#define OTGY_PLAYBACK_SOUND_SPACE_BAR UINT32_C(20)
#define OTGY_PLAYBACK_SOUND_TURN_1 UINT32_C(21)
#define OTGY_PLAYBACK_SOUND_TURN_2 UINT32_C(22)

typedef struct otgy_playback_sound_asset {
  uint32_t language;
  uint32_t id;
  otgy_byte_view wav_bytes;
} otgy_playback_sound_asset;

typedef struct otgy_playback_sound_config {
  uint32_t language;
  uint32_t dance_mode;
  uint8_t transition_sounds_enabled;
  uint64_t random_seed;
  double bpm;
  double offset_seconds;
} otgy_playback_sound_config;

typedef struct otgy_rhythm_test_sound_config {
  uint32_t language;
  double bpm;
  double offset_seconds;
} otgy_rhythm_test_sound_config;

OTGY_C_API otgy_status otgy_playback_sound_bank_create(
    const otgy_playback_sound_asset* assets, uint64_t asset_count,
    otgy_playback_sound_bank** out_bank, otgy_error** out_error);
OTGY_C_API void otgy_playback_sound_bank_destroy(
    otgy_playback_sound_bank* bank);
OTGY_C_API otgy_status otgy_render_chart_playback_pcm(
    const otgy_pcm* pcm, const otgy_chart* chart,
    const otgy_playback_sound_bank* sound_bank,
    otgy_playback_sound_config config, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error);
OTGY_C_API otgy_status otgy_render_rhythm_test_playback_pcm(
    const otgy_pcm* pcm, const otgy_playback_sound_bank* sound_bank,
    otgy_rhythm_test_sound_config config, double timeline_start_seconds,
    otgy_playback_pcm** out_playback_pcm, otgy_error** out_error);

typedef struct otgy_playback_engine otgy_playback_engine;

#define OTGY_PLAYBACK_JUDGEMENT_BASIS_SIXTEENTH_NOTE UINT32_C(0)
#define OTGY_PLAYBACK_JUDGEMENT_BASIS_HALF_BEAT UINT32_C(1)

#define OTGY_PLAYBACK_JUDGEMENT_PERFECT UINT32_C(0)
#define OTGY_PLAYBACK_JUDGEMENT_GREAT UINT32_C(1)
#define OTGY_PLAYBACK_JUDGEMENT_COOL UINT32_C(2)
#define OTGY_PLAYBACK_JUDGEMENT_BAD UINT32_C(3)
#define OTGY_PLAYBACK_JUDGEMENT_MISS UINT32_C(4)

typedef struct otgy_playback_engine_config {
  uint32_t language;
  uint32_t dance_mode;
  uint8_t transition_sounds_enabled;
  uint64_t random_seed;
  double bpm;
  double offset_seconds;
  double start_seconds;
  double end_seconds;
  uint32_t judgement_basis;
  double judgement_perfect_fraction;
  double judgement_great_fraction;
  double judgement_cool_fraction;
  double judgement_bad_fraction;
  double judgement_offset_ms;
} otgy_playback_engine_config;

typedef struct otgy_playback_judgement {
  uint32_t grade;
  double delta_ms;
  uint64_t note_id;
  uint32_t expected_sound;
  uint32_t pressed_sound;
} otgy_playback_judgement;

typedef struct otgy_playback_engine_state {
  uint64_t rendered_frames;
  double position_seconds;
  uint8_t finished;
} otgy_playback_engine_state;

typedef struct otgy_playback_judgement_windows {
  double unit_ms;
  double perfect_ms;
  double great_ms;
  double cool_ms;
  double bad_ms;
} otgy_playback_judgement_windows;

OTGY_C_API otgy_status otgy_playback_engine_create(
    const otgy_pcm* pcm, const otgy_chart* chart,
    const otgy_playback_sound_bank* sound_bank,
    otgy_playback_engine_config config, otgy_playback_engine** out_engine,
    otgy_error** out_error);
OTGY_C_API void otgy_playback_engine_destroy(otgy_playback_engine* engine);
OTGY_C_API uint32_t otgy_playback_engine_get_sample_rate(
    const otgy_playback_engine* engine);
OTGY_C_API void otgy_playback_engine_push_trigger(
    otgy_playback_engine* engine, uint32_t sound_id);
OTGY_C_API void otgy_playback_engine_render(
    otgy_playback_engine* engine, uint8_t* interleaved_pcm16le,
    uint32_t frame_count);
OTGY_C_API void otgy_playback_engine_drain_judgements(
    otgy_playback_engine* engine, otgy_playback_judgement* out_judgements,
    uint32_t capacity, uint32_t* out_count);
OTGY_C_API void otgy_playback_engine_get_state(
    const otgy_playback_engine* engine, otgy_playback_engine_state* out_state);
OTGY_C_API uint8_t otgy_playback_engine_is_finished(
    const otgy_playback_engine* engine);
OTGY_C_API uint64_t otgy_playback_engine_get_rendered_frames(
    const otgy_playback_engine* engine);
OTGY_C_API void otgy_playback_engine_reset(otgy_playback_engine* engine);

/// Computes the four symmetric judgement windows in milliseconds for a BPM and
/// judgement configuration. Used by the tuning menu to preview the windows.
OTGY_C_API otgy_status otgy_playback_judgement_windows_for(
    double bpm, uint32_t basis, double perfect_fraction,
    double great_fraction, double cool_fraction, double bad_fraction,
    otgy_playback_judgement_windows* out_windows, otgy_error** out_error);

OTGY_C_API otgy_status otgy_coordinate_application_operation(
    uint32_t mode, uint32_t operation, uint32_t* out_result,
    otgy_error** out_error);

#define OTGY_APPLICATION_MODE_IDLE UINT32_C(0)
#define OTGY_APPLICATION_MODE_PLAYING UINT32_C(1)
#define OTGY_APPLICATION_MODE_EXPORTING_AUDIO UINT32_C(2)
#define OTGY_APPLICATION_MODE_CHART_INTERACTION UINT32_C(3)
#define OTGY_APPLICATION_OPERATION_GENERAL UINT32_C(0)
#define OTGY_APPLICATION_OPERATION_PAUSE_PLAYBACK UINT32_C(1)
#define OTGY_APPLICATION_OPERATION_TOGGLE_WAVEFORM UINT32_C(2)
#define OTGY_APPLICATION_OPERATION_CANCEL_AUDIO_EXPORT UINT32_C(3)
#define OTGY_APPLICATION_OPERATION_SELECTION_COMMAND UINT32_C(4)
#define OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW UINT32_C(5)
#define OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION UINT32_C(6)
#define OTGY_APPLICATION_OPERATION_ALLOWED UINT32_C(0)
#define OTGY_APPLICATION_OPERATION_PLAYBACK_ACTIVE UINT32_C(1)
#define OTGY_APPLICATION_OPERATION_AUDIO_EXPORT_ACTIVE UINT32_C(2)
#define OTGY_APPLICATION_OPERATION_INVALID_MODE UINT32_C(3)
#define OTGY_APPLICATION_OPERATION_INVALID_OPERATION UINT32_C(4)
#define OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE UINT32_C(5)

typedef struct otgy_chart_marker {
  uint64_t id;
  uint64_t row;
  uint32_t column;
  uint32_t type;
  uint64_t show_n_count;
} otgy_chart_marker;

typedef struct otgy_showtime_move {
  uint64_t event_id;
  uint64_t row;
  uint64_t show_n_count;
  uint64_t range_start;
  uint64_t range_end;
  uint8_t is_first_in_sequence;
  uint8_t is_last_in_sequence;
} otgy_showtime_move;

#define OTGY_CHART_COLUMN_NPC UINT32_C(0)
#define OTGY_CHART_COLUMN_PLAYER UINT32_C(1)
#define OTGY_CHART_COLUMN_SHOWTIME UINT32_C(2)

#define OTGY_CHART_MARKER_NPC_BLOCK_START UINT32_C(0)
#define OTGY_CHART_MARKER_NPC_NOTE UINT32_C(1)
#define OTGY_CHART_MARKER_NPC_MOVE_END UINT32_C(2)
#define OTGY_CHART_MARKER_PLAYER_BLOCK_START UINT32_C(3)
#define OTGY_CHART_MARKER_PLAYER_NOTE UINT32_C(4)
#define OTGY_CHART_MARKER_PLAYER_MOVE_END UINT32_C(5)
#define OTGY_CHART_MARKER_SHOWTIME_START UINT32_C(6)
#define OTGY_CHART_MARKER_SHOWTIME_MOVE UINT32_C(7)

OTGY_C_API otgy_status otgy_chart_create_empty(otgy_chart** out_chart,
                                                otgy_error** out_error);
OTGY_C_API void otgy_chart_destroy(otgy_chart* chart);
OTGY_C_API otgy_status otgy_chart_clone(const otgy_chart* chart,
                                        otgy_chart** out_chart,
                                        otgy_error** out_error);
OTGY_C_API uint8_t otgy_chart_equal(const otgy_chart* left,
                                    const otgy_chart* right);

typedef struct otgy_chart_role_event {
  uint64_t id;
  uint64_t row;
  uint32_t role;
  uint32_t type;
  uint64_t drill_group_id;
  uint64_t dance_move_drill_id;
} otgy_chart_role_event;

#define OTGY_CHART_ROLE_NPC UINT32_C(0)
#define OTGY_CHART_ROLE_PLAYER UINT32_C(1)
#define OTGY_CHART_ROLE_EVENT_BLOCK_START UINT32_C(0)
#define OTGY_CHART_ROLE_EVENT_NOTE UINT32_C(1)
#define OTGY_CHART_ROLE_EVENT_MOVE_END UINT32_C(2)

typedef struct otgy_chart_showtime_start_event {
  uint64_t id;
  uint64_t row;
  uint64_t showtime_id;
} otgy_chart_showtime_start_event;

typedef struct otgy_chart_showtime_move_event {
  uint64_t id;
  uint64_t row;
  uint64_t showtime_id;
  uint64_t dance_move_drill_id;
  uint64_t show_n_count;
} otgy_chart_showtime_move_event;

typedef struct otgy_chart_drill_group {
  uint64_t id;
  uint64_t dance_move_drill_id;
  uint64_t npc_event_id_offset;
  uint64_t npc_event_id_count;
  uint64_t player_event_id_offset;
  uint64_t player_event_id_count;
} otgy_chart_drill_group;

typedef struct otgy_chart_dance_move_drill {
  uint64_t id;
  uint64_t move_n_count;
  uint64_t drill_group_id_offset;
  uint64_t drill_group_id_count;
} otgy_chart_dance_move_drill;

typedef struct otgy_chart_showtime {
  uint64_t id;
  uint64_t start_event_id;
  uint64_t dance_move_drill_id_offset;
  uint64_t dance_move_drill_id_count;
  uint64_t show_move_event_id_offset;
  uint64_t show_move_event_id_count;
} otgy_chart_showtime;

OTGY_C_API otgy_status otgy_chart_copy_semantics(
    const otgy_chart* chart, otgy_chart_semantics** out_semantics,
    otgy_error** out_error);
OTGY_C_API void otgy_chart_semantics_destroy(otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_role_event* otgy_chart_semantics_get_role_events(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_role_events_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_showtime_start_event*
otgy_chart_semantics_get_showtime_start_events(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_showtime_start_events_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_showtime_move_event*
otgy_chart_semantics_get_showtime_move_events(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_showtime_move_events_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_drill_group*
otgy_chart_semantics_get_drill_groups(const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_drill_groups_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const uint64_t* otgy_chart_semantics_get_drill_group_event_ids(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_drill_group_event_ids_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_dance_move_drill*
otgy_chart_semantics_get_dance_move_drills(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_dance_move_drills_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const uint64_t* otgy_chart_semantics_get_dance_move_drill_group_ids(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t
otgy_chart_semantics_get_dance_move_drill_group_ids_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const otgy_chart_showtime* otgy_chart_semantics_get_showtimes(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_showtimes_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const uint64_t*
otgy_chart_semantics_get_showtime_dance_move_drill_ids(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t
otgy_chart_semantics_get_showtime_dance_move_drill_ids_count(
    const otgy_chart_semantics* semantics);
OTGY_C_API const uint64_t*
otgy_chart_semantics_get_showtime_move_event_ids(
    const otgy_chart_semantics* semantics);
OTGY_C_API uint64_t otgy_chart_semantics_get_showtime_move_event_ids_count(
    const otgy_chart_semantics* semantics);

OTGY_C_API otgy_status otgy_chart_project_markers(
    const otgy_chart* chart, otgy_chart_markers** out_markers,
    otgy_error** out_error);
OTGY_C_API void otgy_chart_markers_destroy(otgy_chart_markers* markers);
OTGY_C_API const otgy_chart_marker* otgy_chart_markers_get_data(
    const otgy_chart_markers* markers);
OTGY_C_API uint64_t otgy_chart_markers_get_count(
    const otgy_chart_markers* markers);

OTGY_C_API otgy_status otgy_chart_project_showtime_moves(
    const otgy_chart* chart, otgy_showtime_moves** out_moves,
    otgy_error** out_error);
OTGY_C_API void otgy_showtime_moves_destroy(otgy_showtime_moves* moves);
OTGY_C_API const otgy_showtime_move* otgy_showtime_moves_get_data(
    const otgy_showtime_moves* moves);
OTGY_C_API uint64_t otgy_showtime_moves_get_count(
    const otgy_showtime_moves* moves);
OTGY_C_API otgy_status otgy_chart_note_ordinal(
    const otgy_chart* chart, uint64_t event_id, uint64_t* out_ordinal,
    otgy_error** out_error);

OTGY_C_API otgy_status otgy_chart_editor_create(
    const otgy_chart* chart, otgy_chart_editor** out_editor,
    otgy_error** out_error);
OTGY_C_API void otgy_chart_editor_destroy(otgy_chart_editor* editor);
/// Rebuilds the DanceMoveDrill relation from the visible NPC C/N/S sequence.
/// Sets *out_changed to 1 when the draft was repaired in place. This is a
/// load-time normalization: it clears undo/redo and rebuilds or removes stale
/// Showtime relations. Idempotent; returns 0 for an already-correct draft.
OTGY_C_API otgy_status otgy_chart_editor_repair_relations(
    otgy_chart_editor* editor, uint8_t* out_changed, otgy_error** out_error);
OTGY_C_API uint8_t otgy_chart_editor_can_undo(const otgy_chart_editor* editor);
OTGY_C_API uint8_t otgy_chart_editor_can_redo(const otgy_chart_editor* editor);
OTGY_C_API uint64_t otgy_chart_editor_get_undo_count(
    const otgy_chart_editor* editor);
OTGY_C_API uint64_t otgy_chart_editor_get_redo_count(
    const otgy_chart_editor* editor);
OTGY_C_API otgy_status otgy_chart_editor_copy_chart(
    const otgy_chart_editor* editor, otgy_chart** out_chart,
    otgy_error** out_error);

typedef struct otgy_chart_edit_result {
  uint32_t code;
  uint8_t changed;
  uint64_t primary_event_id;
} otgy_chart_edit_result;

#define OTGY_CHART_EDIT_SUCCESS UINT32_C(0)
#define OTGY_CHART_EDIT_NO_CHANGE UINT32_C(1)
#define OTGY_CHART_EDIT_POSITION_OCCUPIED UINT32_C(2)
#define OTGY_CHART_EDIT_CANNOT_APPEND_AFTER_END UINT32_C(3)
#define OTGY_CHART_EDIT_CANNOT_PLACE UINT32_C(4)
#define OTGY_CHART_EDIT_NO_NPC_SOURCE UINT32_C(5)
#define OTGY_CHART_EDIT_GENERATION_RANGE_OCCUPIED UINT32_C(6)
#define OTGY_CHART_EDIT_AUTO_GENERATED_MARKER_LOCKED UINT32_C(7)
#define OTGY_CHART_EDIT_INVALID_SHOWTIME_POSITION UINT32_C(8)
#define OTGY_CHART_EDIT_NO_REMAINING_DANCE_MOVES UINT32_C(9)
#define OTGY_CHART_EDIT_SHOWTIME_RANGE_OCCUPIED UINT32_C(10)
#define OTGY_CHART_EDIT_INVALID_DRAFT UINT32_C(11)
#define OTGY_CHART_EDIT_RANGE_OVERFLOW UINT32_C(12)
#define OTGY_CHART_EDIT_ID_EXHAUSTED UINT32_C(13)
#define OTGY_CHART_EDIT_ALLOCATION_FAILURE UINT32_C(14)
#define OTGY_CHART_EDIT_NOTHING_TO_UNDO UINT32_C(15)
#define OTGY_CHART_EDIT_NOTHING_TO_REDO UINT32_C(16)
#define OTGY_CHART_EDIT_SELECTION_UNAVAILABLE UINT32_C(17)
#define OTGY_CHART_EDIT_CLIPBOARD_EMPTY UINT32_C(18)
#define OTGY_CHART_EDIT_PASTE_RANGE_OCCUPIED UINT32_C(19)
#define OTGY_CHART_EDIT_SHOWTIME_CONTENT_STALE UINT32_C(20)
#define OTGY_CHART_EDIT_CUT_SOURCE_CHANGED UINT32_C(21)
#define OTGY_CHART_EDIT_SHOWTIME_PARTIALLY_COMPLETED UINT32_C(22)

OTGY_C_API otgy_status otgy_chart_editor_click(
    otgy_chart_editor* editor, uint32_t column, uint64_t row,
    otgy_chart_edit_result* out_result, otgy_error** out_error);
/// Executes the same click transaction and reports whether failed player
/// synchronization cleared existing N/S actions. Both outputs are initialized
/// before argument validation. The legacy result layout remains unchanged.
OTGY_C_API otgy_status otgy_chart_editor_click_with_effects(
    otgy_chart_editor* editor, uint32_t column, uint64_t row,
    otgy_chart_edit_result* out_result, uint8_t* out_player_actions_cleared,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_editor_clear(
    otgy_chart_editor* editor, otgy_chart_edit_result* out_result,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_editor_undo(
    otgy_chart_editor* editor, otgy_chart_edit_result* out_result,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_editor_redo(
    otgy_chart_editor* editor, otgy_chart_edit_result* out_result,
    otgy_error** out_error);

/// One selected dance move, projected from session state.
typedef struct otgy_chart_selection {
  uint8_t active;
  uint64_t dance_move_drill_id;
  uint64_t first_row;
  uint64_t last_row;
} otgy_chart_selection;

/// Session commands change selection or clipboard only, without draft history.
typedef struct otgy_chart_session_result {
  uint32_t code;
  uint8_t changed;
} otgy_chart_session_result;

/// Copies the ordered selection into an independently owned handle. All out
/// parameters are cleared before argument validation.
OTGY_C_API otgy_status otgy_chart_editor_copy_selections(
    const otgy_chart_editor* editor, otgy_chart_selections** out_selections,
    otgy_error** out_error);
OTGY_C_API void otgy_chart_selections_destroy(otgy_chart_selections* selections);
OTGY_C_API const otgy_chart_selection* otgy_chart_selections_get_data(
    const otgy_chart_selections* selections);
OTGY_C_API uint64_t otgy_chart_selections_get_count(
    const otgy_chart_selections* selections);
OTGY_C_API uint64_t otgy_chart_editor_selection_count(
    const otgy_chart_editor* editor);
/// Toggles the drill at an exact marker row from any valid chart column.
OTGY_C_API otgy_status otgy_chart_editor_select(
    otgy_chart_editor* editor, uint32_t column, uint64_t row,
    otgy_chart_session_result* out_result, otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_editor_clear_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error);

/// Null editors return zero for all session scalar queries.
OTGY_C_API uint8_t otgy_chart_editor_has_clipboard(
    const otgy_chart_editor* editor);
OTGY_C_API uint64_t otgy_chart_editor_clipboard_count(
    const otgy_chart_editor* editor);
OTGY_C_API uint8_t otgy_chart_editor_cut_pending(
    const otgy_chart_editor* editor);
OTGY_C_API otgy_status otgy_chart_editor_copy_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_chart_editor_clear_clipboard(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error);

/// Prepares a batch cut without deleting source content or adding history.
OTGY_C_API otgy_status otgy_chart_editor_cut_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error);

/// Each success adds one history entry. First cut placement atomically removes
/// the verified source and inserts the batch; subsequent placements are copies.
OTGY_C_API otgy_status otgy_chart_editor_paste(
    otgy_chart_editor* editor, uint64_t anchor_row,
    otgy_chart_edit_result* out_result, otgy_error** out_error);

/// Read-only core placement plan. Invalid plans still expose available target
/// geometry. The owned projection remains valid after editor changes/destruction.
OTGY_C_API otgy_status otgy_chart_editor_preview_paste(
    const otgy_chart_editor* editor, uint64_t anchor_row,
    otgy_chart_paste_preview** out_preview, otgy_error** out_error);
OTGY_C_API void otgy_chart_paste_preview_destroy(
    otgy_chart_paste_preview* preview);
OTGY_C_API uint32_t otgy_chart_paste_preview_get_code(
    const otgy_chart_paste_preview* preview);
OTGY_C_API uint64_t otgy_chart_paste_preview_get_first_row(
    const otgy_chart_paste_preview* preview);
OTGY_C_API uint64_t otgy_chart_paste_preview_get_last_row(
    const otgy_chart_paste_preview* preview);
OTGY_C_API const otgy_chart_selection* otgy_chart_paste_preview_get_selections(
    const otgy_chart_paste_preview* preview);
OTGY_C_API uint64_t otgy_chart_paste_preview_get_selection_count(
    const otgy_chart_paste_preview* preview);
OTGY_C_API const otgy_chart_marker* otgy_chart_paste_preview_get_markers(
    const otgy_chart_paste_preview* preview);
OTGY_C_API uint64_t otgy_chart_paste_preview_get_marker_count(
    const otgy_chart_paste_preview* preview);

typedef struct otgy_chart_diagnostic {
  uint32_t code;
  uint64_t object_id;
  uint64_t related_id;
  uint64_t row;
} otgy_chart_diagnostic;

#define OTGY_CHART_DIAGNOSTIC_EMPTY_CHART UINT32_C(0)
#define OTGY_CHART_DIAGNOSTIC_DUPLICATE_EVENT_ID UINT32_C(1)
#define OTGY_CHART_DIAGNOSTIC_DUPLICATE_EVENT_ROW UINT32_C(2)
#define OTGY_CHART_DIAGNOSTIC_DUPLICATE_DRILL_GROUP_ID UINT32_C(3)
#define OTGY_CHART_DIAGNOSTIC_DUPLICATE_DANCE_MOVE_DRILL_ID UINT32_C(4)
#define OTGY_CHART_DIAGNOSTIC_DUPLICATE_SHOWTIME_ID UINT32_C(5)
#define OTGY_CHART_DIAGNOSTIC_MISSING_REFERENCE UINT32_C(6)
#define OTGY_CHART_DIAGNOSTIC_INVALID_EVENT_RELATION UINT32_C(7)
#define OTGY_CHART_DIAGNOSTIC_INVALID_DRILL_BLOCK UINT32_C(8)
#define OTGY_CHART_DIAGNOSTIC_DRILL_BLOCK_MISMATCH UINT32_C(9)
#define OTGY_CHART_DIAGNOSTIC_INVALID_DANCE_MOVE_DRILL UINT32_C(10)
#define OTGY_CHART_DIAGNOSTIC_INVALID_SHOWTIME UINT32_C(11)
#define OTGY_CHART_DIAGNOSTIC_ORPHAN_EVENT UINT32_C(12)
#define OTGY_CHART_DIAGNOSTIC_ORPHAN_DRILL_GROUP UINT32_C(13)
#define OTGY_CHART_DIAGNOSTIC_ORPHAN_DANCE_MOVE_DRILL UINT32_C(14)

OTGY_C_API otgy_status otgy_chart_validate_finished(
    const otgy_chart* chart, otgy_chart_validation** out_validation,
    otgy_error** out_error);
OTGY_C_API void otgy_chart_validation_destroy(
    otgy_chart_validation* validation);
OTGY_C_API uint8_t otgy_chart_validation_is_finished(
    const otgy_chart_validation* validation);
OTGY_C_API const otgy_chart_diagnostic* otgy_chart_validation_get_diagnostics(
    const otgy_chart_validation* validation);
OTGY_C_API uint64_t otgy_chart_validation_get_diagnostic_count(
    const otgy_chart_validation* validation);
OTGY_C_API otgy_status otgy_chart_validation_copy_finished(
    const otgy_chart_validation* validation,
    otgy_finished_chart** out_finished_chart, otgy_error** out_error);
OTGY_C_API void otgy_finished_chart_destroy(otgy_finished_chart* chart);
OTGY_C_API otgy_status otgy_finished_chart_export_slk(
    const otgy_finished_chart* chart, otgy_bytes** out_bytes,
    otgy_error** out_error);

OTGY_C_API otgy_status otgy_slk_import(
    otgy_byte_view bytes, otgy_slk_document** out_document,
    otgy_error** out_error);
OTGY_C_API void otgy_slk_document_destroy(otgy_slk_document* document);
OTGY_C_API otgy_status otgy_slk_document_project_chart(
    const otgy_slk_document* document, otgy_chart** out_chart,
    otgy_error** out_error);

OTGY_C_API otgy_status otgy_project_encode_formal_draft(
    const otgy_chart* chart, otgy_bytes** out_bytes, otgy_error** out_error);
OTGY_C_API otgy_status otgy_project_decode_formal_draft(
    otgy_byte_view bytes, otgy_chart** out_chart, otgy_error** out_error);
OTGY_C_API otgy_status otgy_project_encode_recovery_snapshot(
    const otgy_chart* chart, otgy_bytes** out_bytes, otgy_error** out_error);
OTGY_C_API otgy_status otgy_project_decode_recovery_snapshot(
    otgy_byte_view bytes, otgy_chart** out_chart, otgy_error** out_error);

typedef struct otgy_slk_position {
  uint64_t measure;
  uint64_t beat;
  uint64_t sixteenth;
  uint64_t total_sixteenth;
} otgy_slk_position;

OTGY_C_API otgy_status otgy_compose_slk_position(
    uint64_t measure, uint64_t beat, uint64_t sixteenth,
    otgy_slk_position* out_position, otgy_error** out_error);
OTGY_C_API otgy_status otgy_decompose_slk_position(
    uint64_t total_sixteenth, otgy_slk_position* out_position,
    otgy_error** out_error);
OTGY_C_API otgy_status otgy_validate_slk_position(
    uint64_t measure, uint64_t beat, uint64_t sixteenth,
    uint64_t total_sixteenth, otgy_slk_position* out_position,
    otgy_error** out_error);

#ifdef __cplusplus
}
#endif

#endif  // OTGY_AV_C_API_H
