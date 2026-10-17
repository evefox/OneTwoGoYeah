// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include <stddef.h>

typedef otgy_status (*session_command)(otgy_chart_editor*,
                                      otgy_chart_session_result*, otgy_error**);

int main(void) {
  otgy_error* error = NULL;
  otgy_chart* chart = NULL;
  otgy_chart_editor* editor = NULL;
  otgy_chart_selections* selections = NULL;
  otgy_chart_paste_preview* preview = NULL;
  otgy_chart_session_result session = {7U, 7U};
  const session_command commands[] = {
      otgy_chart_editor_clear_selection, otgy_chart_editor_copy_selection,
      otgy_chart_editor_cut_selection, otgy_chart_editor_clear_clipboard};
  size_t index;
  int result = 0;

  if (OTGY_C_API_VERSION != 2U ||
      otgy_c_api_version() != OTGY_C_API_VERSION ||
      OTGY_CHART_EDIT_CUT_SOURCE_CHANGED != 21U) {
    return 1;
  }
  /* Every gate constant is pinned numerically: renaming or renumbering an
     existing value must be caught here rather than by a silent behaviour
     change. Appending new values at the same width does not bump the ABI
     version. */
  if (OTGY_APPLICATION_MODE_IDLE != 0U ||
      OTGY_APPLICATION_MODE_PLAYING != 1U ||
      OTGY_APPLICATION_MODE_EXPORTING_AUDIO != 2U ||
      OTGY_APPLICATION_MODE_CHART_INTERACTION != 3U ||
      OTGY_APPLICATION_OPERATION_GENERAL != 0U ||
      OTGY_APPLICATION_OPERATION_PAUSE_PLAYBACK != 1U ||
      OTGY_APPLICATION_OPERATION_TOGGLE_WAVEFORM != 2U ||
      OTGY_APPLICATION_OPERATION_CANCEL_AUDIO_EXPORT != 3U ||
      OTGY_APPLICATION_OPERATION_SELECTION_COMMAND != 4U ||
      OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW != 5U ||
      OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION != 6U ||
      OTGY_APPLICATION_OPERATION_ALLOWED != 0U ||
      OTGY_APPLICATION_OPERATION_PLAYBACK_ACTIVE != 1U ||
      OTGY_APPLICATION_OPERATION_AUDIO_EXPORT_ACTIVE != 2U ||
      OTGY_APPLICATION_OPERATION_INVALID_MODE != 3U ||
      OTGY_APPLICATION_OPERATION_INVALID_OPERATION != 4U ||
      OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE != 5U) {
    return 1;
  }
  if (otgy_chart_create_empty(&chart, &error) != OTGY_STATUS_OK ||
      chart == NULL || error != NULL) {
    result = 2;
    goto cleanup;
  }
  if (otgy_chart_editor_create(chart, &editor, &error) != OTGY_STATUS_OK ||
      editor == NULL || error != NULL) {
    result = 3;
    goto cleanup;
  }
  if (otgy_chart_editor_selection_count(NULL) != 0U ||
      otgy_chart_editor_clipboard_count(NULL) != 0U ||
      otgy_chart_editor_cut_pending(NULL) != 0U ||
      otgy_chart_editor_has_clipboard(NULL) != 0U ||
      otgy_chart_selections_get_data(NULL) != NULL ||
      otgy_chart_selections_get_count(NULL) != 0U ||
      otgy_chart_paste_preview_get_code(NULL) != OTGY_CHART_EDIT_CLIPBOARD_EMPTY ||
      otgy_chart_paste_preview_get_first_row(NULL) != 0U ||
      otgy_chart_paste_preview_get_last_row(NULL) != 0U ||
      otgy_chart_paste_preview_get_selections(NULL) != NULL ||
      otgy_chart_paste_preview_get_selection_count(NULL) != 0U ||
      otgy_chart_paste_preview_get_markers(NULL) != NULL ||
      otgy_chart_paste_preview_get_marker_count(NULL) != 0U) {
    result = 4;
    goto cleanup;
  }
  for (index = 0U; index < sizeof(commands) / sizeof(commands[0]); ++index) {
    session.code = 7U;
    session.changed = 7U;
    if (commands[index](NULL, &session, &error) != OTGY_STATUS_INVALID_ARGUMENT ||
        session.code != 0U || session.changed != 0U || error == NULL ||
        otgy_error_get_code(error) != OTGY_ABI_ERROR_NULL_ARGUMENT) {
      result = 5;
      goto cleanup;
    }
    otgy_error_destroy(error);
    error = NULL;
    if (commands[index](editor, NULL, &error) != OTGY_STATUS_INVALID_ARGUMENT ||
        error == NULL) {
      result = 6;
      goto cleanup;
    }
    otgy_error_destroy(error);
    error = NULL;
  }
  session.code = 7U;
  session.changed = 7U;
  if (otgy_chart_editor_select(NULL, OTGY_CHART_COLUMN_NPC, 0U, &session,
                               &error) != OTGY_STATUS_INVALID_ARGUMENT ||
      session.code != 0U || session.changed != 0U || error == NULL) {
    result = 7;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  if (otgy_chart_editor_select(editor, OTGY_CHART_COLUMN_NPC, 0U, NULL, &error) !=
          OTGY_STATUS_INVALID_ARGUMENT || error == NULL) {
    result = 8;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  selections = (otgy_chart_selections*)(uintptr_t)1U;
  if (otgy_chart_editor_copy_selections(NULL, &selections, &error) !=
          OTGY_STATUS_INVALID_ARGUMENT ||
      selections != NULL || error == NULL) {
    selections = NULL;
    result = 9;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  preview = (otgy_chart_paste_preview*)(uintptr_t)1U;
  if (otgy_chart_editor_preview_paste(NULL, 5U, &preview, &error) !=
          OTGY_STATUS_INVALID_ARGUMENT ||
      preview != NULL || error == NULL) {
    preview = NULL;
    result = 10;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  if (otgy_chart_editor_copy_selections(editor, NULL, &error) !=
          OTGY_STATUS_INVALID_ARGUMENT || error == NULL) {
    result = 11;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  if (otgy_chart_editor_preview_paste(editor, 5U, NULL, &error) !=
          OTGY_STATUS_INVALID_ARGUMENT || error == NULL) {
    result = 12;
    goto cleanup;
  }
  otgy_error_destroy(error);
  error = NULL;
  if (otgy_chart_editor_copy_selections(editor, &selections, &error) !=
          OTGY_STATUS_OK || selections == NULL || error != NULL ||
      otgy_chart_selections_get_count(selections) != 0U ||
      otgy_chart_selections_get_data(selections) != NULL ||
      otgy_chart_editor_preview_paste(editor, 5U, &preview, &error) !=
          OTGY_STATUS_OK || preview == NULL || error != NULL ||
      otgy_chart_paste_preview_get_code(preview) != OTGY_CHART_EDIT_CLIPBOARD_EMPTY) {
    result = 13;
    goto cleanup;
  }
  otgy_chart_editor_destroy(editor);
  editor = NULL;
  if (otgy_chart_selections_get_count(selections) != 0U ||
      otgy_chart_paste_preview_get_code(preview) != OTGY_CHART_EDIT_CLIPBOARD_EMPTY ||
      otgy_chart_paste_preview_get_selection_count(preview) != 0U ||
      otgy_chart_paste_preview_get_marker_count(preview) != 0U) {
    result = 14;
  }

cleanup:
  otgy_chart_paste_preview_destroy(preview);
  otgy_chart_selections_destroy(selections);
  otgy_chart_editor_destroy(editor);
  otgy_chart_destroy(chart);
  otgy_error_destroy(error);
  otgy_chart_paste_preview_destroy(NULL);
  otgy_chart_selections_destroy(NULL);
  otgy_chart_editor_destroy(NULL);
  otgy_error_destroy(NULL);
  otgy_bytes_destroy(NULL);
  otgy_pcm_destroy(NULL);
  return result;
}
