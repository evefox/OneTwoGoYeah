// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.hpp"

using namespace otgy::c_api;

namespace {

otgy::core::ChartDraft empty_chart() noexcept {
  return {{}, {}, {}, {}, {}, {}};
}

otgy::core::ChartColumn chart_column(std::uint32_t value) noexcept {
  // Preserve invalid input as invalid instead of wrapping to a valid uint8 enum.
  return static_cast<otgy::core::ChartColumn>(
      value <= OTGY_CHART_COLUMN_SHOWTIME ? value : UINT8_MAX);
}

otgy_chart_marker from_core(const otgy::core::ChartMarker& value) noexcept {
  return {value.id.value, value.row,
          static_cast<std::uint32_t>(value.column),
          static_cast<std::uint32_t>(value.type), value.show_n_count};
}

otgy_showtime_move from_core(
    const otgy::core::ShowtimeMoveProjection& value) noexcept {
  return {value.event_id.value,
          value.row,
          value.show_n_count,
          value.range_start,
          value.range_end,
          static_cast<std::uint8_t>(value.is_first_in_sequence),
          static_cast<std::uint8_t>(value.is_last_in_sequence)};
}

template <typename Id>
void append_ids(const std::vector<Id>& source,
                std::vector<std::uint64_t>& target) {
  target.reserve(target.size() + source.size());
  for (const auto id : source) {
    target.push_back(id.value);
  }
}

void copy_edit_result(const otgy::core::ChartEditResult& source,
                      otgy_chart_edit_result& target) noexcept {
  target = {static_cast<std::uint32_t>(source.code),
            static_cast<std::uint8_t>(source.changed),
            source.primary_event_id.value};
}

otgy_chart_selection from_core(
    const otgy::core::ChartSelection& value) noexcept {
  return {static_cast<std::uint8_t>(value.active), value.drill_id.value,
          value.first_row, value.last_row};
}

void copy_session_result(const otgy::core::ChartSessionResult& source,
                         otgy_chart_session_result& target) noexcept {
  target = {static_cast<std::uint32_t>(source.code),
            static_cast<std::uint8_t>(source.changed)};
}

template <typename Edit>
otgy_status edit_chart(otgy_chart_editor* editor,
                       otgy_chart_edit_result* out_result,
                       otgy_error** out_error, Edit&& edit) {
  if (out_result != nullptr) {
    *out_result = otgy_chart_edit_result{};
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_result == nullptr) {
      return invalid_argument(out_error);
    }
    copy_edit_result(edit(editor->value), *out_result);
    return OTGY_STATUS_OK;
  });
}

/// Session commands zero the out result before the argument check, so a caller
/// that ignores the returned status never reads a stale command result.
template <typename Edit>
otgy_status session_chart(otgy_chart_editor* editor,
                          otgy_chart_session_result* out_result,
                          otgy_error** out_error, Edit&& edit) {
  if (out_result != nullptr) {
    *out_result = otgy_chart_session_result{};
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_result == nullptr) {
      return invalid_argument(out_error);
    }
    copy_session_result(edit(editor->value), *out_result);
    return OTGY_STATUS_OK;
  });
}

otgy_status return_slk_position(const otgy::core::SlkPositionResult& result,
                                otgy_slk_position* out_position,
                                otgy_error** out_error) noexcept {
  if (!result) {
    return core_error(out_error, OTGY_ERROR_DOMAIN_SLK_POSITION,
                      static_cast<std::uint32_t>(result.error()),
                      otgy::core::slk_position_error_message(result.error()));
  }
  *out_position = {result.position()->measure, result.position()->beat,
                   result.position()->sixteenth,
                   result.position()->total_sixteenth};
  return OTGY_STATUS_OK;
}

}  // namespace

extern "C" {

otgy_status otgy_chart_create_empty(otgy_chart** out_chart,
                                    otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (out_chart == nullptr) {
      return invalid_argument(out_error);
    }
    return make_handle<otgy_chart>(otgy_chart{empty_chart()}, out_chart,
                                   out_error);
  });
}

void otgy_chart_destroy(otgy_chart* chart) { delete chart; }

otgy_status otgy_chart_clone(const otgy_chart* chart, otgy_chart** out_chart,
                             otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_chart == nullptr) {
      return invalid_argument(out_error);
    }
    return make_handle<otgy_chart>(otgy_chart{chart->value}, out_chart,
                                   out_error);
  });
}

std::uint8_t otgy_chart_equal(const otgy_chart* left,
                              const otgy_chart* right) {
  return left != nullptr && right != nullptr &&
                 otgy::core::chart_drafts_equal(left->value, right->value)
             ? 1U
             : 0U;
}

otgy_status otgy_chart_copy_semantics(
    const otgy_chart* chart, otgy_chart_semantics** out_semantics,
    otgy_error** out_error) {
  if (out_semantics != nullptr) {
    *out_semantics = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_semantics == nullptr) {
      return invalid_argument(out_error);
    }
    otgy_chart_semantics result;
    const auto& draft = chart->value;
    result.role_events.reserve(draft.role_events().size());
    result.showtime_start_events.reserve(
        draft.showtime_start_events().size());
    result.showtime_move_events.reserve(draft.showtime_move_events().size());
    result.drill_groups.reserve(draft.drill_groups().size());
    result.dance_move_drills.reserve(draft.dance_move_drills().size());
    result.showtimes.reserve(draft.showtimes().size());
    for (const auto& event : draft.role_events()) {
      result.role_events.push_back(
          {event.id.value, event.row, static_cast<std::uint32_t>(event.role),
           static_cast<std::uint32_t>(event.type), event.drill_group_id.value,
           event.dance_move_drill_id.value});
    }
    for (const auto& event : draft.showtime_start_events()) {
      result.showtime_start_events.push_back(
          {event.id.value, event.row, event.showtime_id.value});
    }
    for (const auto& event : draft.showtime_move_events()) {
      result.showtime_move_events.push_back(
          {event.id.value, event.row, event.showtime_id.value,
           event.dance_move_drill_id.value, event.show_n_count});
    }
    for (const auto& group : draft.drill_groups()) {
      const auto npc_offset = result.drill_group_event_ids.size();
      append_ids(group.npc_event_ids, result.drill_group_event_ids);
      const auto player_offset = result.drill_group_event_ids.size();
      append_ids(group.player_event_ids, result.drill_group_event_ids);
      result.drill_groups.push_back(
          {group.id.value, group.dance_move_drill_id.value,
           static_cast<std::uint64_t>(npc_offset),
           static_cast<std::uint64_t>(group.npc_event_ids.size()),
           static_cast<std::uint64_t>(player_offset),
           static_cast<std::uint64_t>(group.player_event_ids.size())});
    }
    for (const auto& drill : draft.dance_move_drills()) {
      const auto offset = result.dance_move_drill_group_ids.size();
      append_ids(drill.drill_group_ids,
                 result.dance_move_drill_group_ids);
      result.dance_move_drills.push_back(
          {drill.id.value, drill.move_n_count,
           static_cast<std::uint64_t>(offset),
           static_cast<std::uint64_t>(drill.drill_group_ids.size())});
    }
    for (const auto& showtime : draft.showtimes()) {
      const auto dance_offset =
          result.showtime_dance_move_drill_ids.size();
      append_ids(showtime.dance_move_drill_ids,
                 result.showtime_dance_move_drill_ids);
      const auto move_offset = result.showtime_move_event_ids.size();
      append_ids(showtime.show_move_event_ids,
                 result.showtime_move_event_ids);
      result.showtimes.push_back(
          {showtime.id.value, showtime.start_event_id.value,
           static_cast<std::uint64_t>(dance_offset),
           static_cast<std::uint64_t>(showtime.dance_move_drill_ids.size()),
           static_cast<std::uint64_t>(move_offset),
           static_cast<std::uint64_t>(showtime.show_move_event_ids.size())});
    }
    return make_handle<otgy_chart_semantics>(std::move(result), out_semantics,
                                             out_error);
  });
}

void otgy_chart_semantics_destroy(otgy_chart_semantics* semantics) {
  delete semantics;
}

#define OTGY_SEMANTIC_VECTOR_ACCESSORS(name, field, type)                     \
  const type* otgy_chart_semantics_get_##name(                                \
      const otgy_chart_semantics* semantics) {                                \
    return semantics == nullptr || semantics->field.empty()                   \
               ? nullptr                                                     \
               : semantics->field.data();                                    \
  }                                                                           \
  std::uint64_t otgy_chart_semantics_get_##name##_count(                      \
      const otgy_chart_semantics* semantics) {                                \
    return semantics == nullptr                                               \
               ? 0U                                                          \
               : static_cast<std::uint64_t>(semantics->field.size());         \
  }

OTGY_SEMANTIC_VECTOR_ACCESSORS(role_events, role_events, otgy_chart_role_event)
OTGY_SEMANTIC_VECTOR_ACCESSORS(showtime_start_events, showtime_start_events,
                               otgy_chart_showtime_start_event)
OTGY_SEMANTIC_VECTOR_ACCESSORS(showtime_move_events, showtime_move_events,
                               otgy_chart_showtime_move_event)
OTGY_SEMANTIC_VECTOR_ACCESSORS(drill_groups, drill_groups,
                               otgy_chart_drill_group)
OTGY_SEMANTIC_VECTOR_ACCESSORS(drill_group_event_ids, drill_group_event_ids,
                               std::uint64_t)
OTGY_SEMANTIC_VECTOR_ACCESSORS(dance_move_drills, dance_move_drills,
                               otgy_chart_dance_move_drill)
OTGY_SEMANTIC_VECTOR_ACCESSORS(dance_move_drill_group_ids,
                               dance_move_drill_group_ids, std::uint64_t)
OTGY_SEMANTIC_VECTOR_ACCESSORS(showtimes, showtimes, otgy_chart_showtime)
OTGY_SEMANTIC_VECTOR_ACCESSORS(showtime_dance_move_drill_ids,
                               showtime_dance_move_drill_ids, std::uint64_t)
OTGY_SEMANTIC_VECTOR_ACCESSORS(showtime_move_event_ids,
                               showtime_move_event_ids, std::uint64_t)

#undef OTGY_SEMANTIC_VECTOR_ACCESSORS

otgy_status otgy_chart_project_markers(
    const otgy_chart* chart, otgy_chart_markers** out_markers,
    otgy_error** out_error) {
  if (out_markers != nullptr) {
    *out_markers = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_markers == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::project_chart_markers(chart->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_CHART_PROJECTION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::chart_projection_error_message(result.error()));
    }
    std::vector<otgy_chart_marker> markers;
    markers.reserve(result.markers()->size());
    for (const auto& marker : *result.markers()) {
      markers.push_back(from_core(marker));
    }
    return make_handle<otgy_chart_markers>(
        otgy_chart_markers{std::move(markers)}, out_markers, out_error);
  });
}

void otgy_chart_markers_destroy(otgy_chart_markers* markers) {
  delete markers;
}

const otgy_chart_marker* otgy_chart_markers_get_data(
    const otgy_chart_markers* markers) {
  return markers == nullptr || markers->value.empty() ? nullptr
                                                      : markers->value.data();
}

std::uint64_t otgy_chart_markers_get_count(
    const otgy_chart_markers* markers) {
  return markers == nullptr
             ? 0U
             : static_cast<std::uint64_t>(markers->value.size());
}

otgy_status otgy_chart_project_showtime_moves(
    const otgy_chart* chart, otgy_showtime_moves** out_moves,
    otgy_error** out_error) {
  if (out_moves != nullptr) {
    *out_moves = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_moves == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::project_showtime_moves(chart->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_CHART_PROJECTION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::chart_projection_error_message(result.error()));
    }
    std::vector<otgy_showtime_move> moves;
    moves.reserve(result.moves()->size());
    for (const auto& move : *result.moves()) {
      moves.push_back(from_core(move));
    }
    return make_handle<otgy_showtime_moves>(
        otgy_showtime_moves{std::move(moves)}, out_moves, out_error);
  });
}

void otgy_showtime_moves_destroy(otgy_showtime_moves* moves) { delete moves; }

const otgy_showtime_move* otgy_showtime_moves_get_data(
    const otgy_showtime_moves* moves) {
  return moves == nullptr || moves->value.empty() ? nullptr
                                                 : moves->value.data();
}

std::uint64_t otgy_showtime_moves_get_count(
    const otgy_showtime_moves* moves) {
  return moves == nullptr ? 0U
                          : static_cast<std::uint64_t>(moves->value.size());
}

otgy_status otgy_chart_note_ordinal(const otgy_chart* chart,
                                    std::uint64_t event_id,
                                    std::uint64_t* out_ordinal,
                                    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_ordinal == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = otgy::core::chart_note_ordinal(
        chart->value, otgy::core::ChartEventId{event_id});
    if (!result) {
      const std::string_view message =
          result.error == otgy::core::ChartNoteOrdinalError::EventNotFound
              ? "chart event was not found"
              : "chart event is not a note";
      return core_error(out_error, OTGY_ERROR_DOMAIN_CHART_NOTE_ORDINAL,
                        static_cast<std::uint32_t>(result.error), message);
    }
    *out_ordinal = result.ordinal;
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_chart_editor_create(
    const otgy_chart* chart, otgy_chart_editor** out_editor,
    otgy_error** out_error) {
  if (out_editor != nullptr) {
    *out_editor = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_editor == nullptr) {
      return invalid_argument(out_error);
    }
    return make_handle<otgy_chart_editor>(
        otgy_chart_editor{otgy::core::ChartEditor(chart->value)}, out_editor,
        out_error);
  });
}

void otgy_chart_editor_destroy(otgy_chart_editor* editor) { delete editor; }

std::uint8_t otgy_chart_editor_can_undo(const otgy_chart_editor* editor) {
  return editor != nullptr && editor->value.can_undo() ? 1U : 0U;
}

std::uint8_t otgy_chart_editor_can_redo(const otgy_chart_editor* editor) {
  return editor != nullptr && editor->value.can_redo() ? 1U : 0U;
}

std::uint64_t otgy_chart_editor_get_undo_count(
    const otgy_chart_editor* editor) {
  return editor == nullptr
             ? 0U
             : static_cast<std::uint64_t>(editor->value.undo_count());
}

std::uint64_t otgy_chart_editor_get_redo_count(
    const otgy_chart_editor* editor) {
  return editor == nullptr
             ? 0U
             : static_cast<std::uint64_t>(editor->value.redo_count());
}

otgy_status otgy_chart_editor_copy_chart(
    const otgy_chart_editor* editor, otgy_chart** out_chart,
    otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_chart == nullptr) {
      return invalid_argument(out_error);
    }
    return make_handle<otgy_chart>(otgy_chart{editor->value.draft()},
                                   out_chart, out_error);
  });
}

otgy_status otgy_chart_editor_click(
    otgy_chart_editor* editor, std::uint32_t column, std::uint64_t row,
    otgy_chart_edit_result* out_result, otgy_error** out_error) {
  return edit_chart(editor, out_result, out_error, [&](auto& value) {
    return value.click(chart_column(column), row);
  });
}

otgy_status otgy_chart_editor_click_with_effects(
    otgy_chart_editor* editor, std::uint32_t column, std::uint64_t row,
    otgy_chart_edit_result* out_result, std::uint8_t* out_player_actions_cleared,
    otgy_error** out_error) {
  if (out_result != nullptr) {
    *out_result = otgy_chart_edit_result{};
  }
  if (out_player_actions_cleared != nullptr) {
    *out_player_actions_cleared = 0U;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_result == nullptr ||
        out_player_actions_cleared == nullptr) {
      return invalid_argument(out_error);
    }
    const auto result = editor->value.click(chart_column(column), row);
    copy_edit_result(result, *out_result);
    *out_player_actions_cleared =
        static_cast<std::uint8_t>(result.player_actions_cleared);
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_chart_editor_clear(otgy_chart_editor* editor,
                                    otgy_chart_edit_result* out_result,
                                    otgy_error** out_error) {
  return edit_chart(editor, out_result, out_error,
                    [](auto& value) { return value.clear(); });
}

otgy_status otgy_chart_editor_undo(otgy_chart_editor* editor,
                                   otgy_chart_edit_result* out_result,
                                   otgy_error** out_error) {
  return edit_chart(editor, out_result, out_error,
                    [](auto& value) { return value.undo(); });
}

otgy_status otgy_chart_editor_redo(otgy_chart_editor* editor,
                                   otgy_chart_edit_result* out_result,
                                   otgy_error** out_error) {
  return edit_chart(editor, out_result, out_error,
                    [](auto& value) { return value.redo(); });
}

otgy_status otgy_chart_editor_copy_selections(
    const otgy_chart_editor* editor, otgy_chart_selections** out_selections,
    otgy_error** out_error) {
  if (out_selections != nullptr) {
    *out_selections = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_selections == nullptr) {
      return invalid_argument(out_error);
    }
    otgy_chart_selections result;
    result.value.reserve(editor->value.selections().size());
    for (const auto& selection : editor->value.selections()) {
      result.value.push_back(from_core(selection));
    }
    return make_handle<otgy_chart_selections>(std::move(result), out_selections,
                                            out_error);
  });
}

void otgy_chart_selections_destroy(otgy_chart_selections* selections) {
  delete selections;
}

const otgy_chart_selection* otgy_chart_selections_get_data(
    const otgy_chart_selections* selections) {
  return selections == nullptr || selections->value.empty()
             ? nullptr
             : selections->value.data();
}

std::uint64_t otgy_chart_selections_get_count(
    const otgy_chart_selections* selections) {
  return selections == nullptr ? 0U : selections->value.size();
}

std::uint64_t otgy_chart_editor_selection_count(
    const otgy_chart_editor* editor) {
  return editor == nullptr ? 0U : editor->value.selections().size();
}

otgy_status otgy_chart_editor_select(otgy_chart_editor* editor,
                                     std::uint32_t column, std::uint64_t row,
                                     otgy_chart_session_result* out_result,
                                     otgy_error** out_error) {
  return session_chart(editor, out_result, out_error, [&](auto& value) {
    return value.select(chart_column(column), row);
  });
}

otgy_status otgy_chart_editor_clear_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error) {
  return session_chart(editor, out_result, out_error,
                       [](auto& value) { return value.clear_selection(); });
}

uint8_t otgy_chart_editor_has_clipboard(const otgy_chart_editor* editor) {
  return editor != nullptr && editor->value.has_clipboard() ? UINT8_C(1)
                                                            : UINT8_C(0);
}

std::uint64_t otgy_chart_editor_clipboard_count(
    const otgy_chart_editor* editor) {
  return editor == nullptr || !editor->value.has_clipboard()
             ? 0U
             : editor->value.clipboard().content.dance_move_drills().size();
}

std::uint8_t otgy_chart_editor_cut_pending(const otgy_chart_editor* editor) {
  return editor != nullptr && editor->value.has_pending_cut() ? 1U : 0U;
}

otgy_status otgy_chart_editor_copy_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error) {
  return session_chart(editor, out_result, out_error,
                       [](auto& value) { return value.copy_selection(); });
}

otgy_status otgy_chart_editor_clear_clipboard(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error) {
  return session_chart(editor, out_result, out_error,
                       [](auto& value) { return value.clear_clipboard(); });
}

otgy_status otgy_chart_editor_cut_selection(
    otgy_chart_editor* editor, otgy_chart_session_result* out_result,
    otgy_error** out_error) {
  return session_chart(editor, out_result, out_error,
                       [](auto& value) { return value.cut_selection(); });
}

otgy_status otgy_chart_editor_paste(otgy_chart_editor* editor,
                                    std::uint64_t anchor_row,
                                    otgy_chart_edit_result* out_result,
                                    otgy_error** out_error) {
  return edit_chart(editor, out_result, out_error,
                    [&](auto& value) { return value.paste(anchor_row); });
}

otgy_status otgy_chart_editor_preview_paste(
    const otgy_chart_editor* editor, std::uint64_t anchor_row,
    otgy_chart_paste_preview** out_preview, otgy_error** out_error) {
  if (out_preview != nullptr) {
    *out_preview = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (editor == nullptr || out_preview == nullptr) {
      return invalid_argument(out_error);
    }
    const auto preview = editor->value.preview_paste(anchor_row);
    otgy_chart_paste_preview result{static_cast<std::uint32_t>(preview.code),
                                   preview.anchor_row, preview.last_row, {}, {}};
    result.selections.reserve(preview.spans.size());
    result.markers.reserve(preview.ghost_markers.size());
    for (const auto& span : preview.spans) {
      result.selections.push_back(from_core(span));
    }
    for (const auto& marker : preview.ghost_markers) {
      result.markers.push_back(from_core(marker));
    }
    return make_handle<otgy_chart_paste_preview>(std::move(result), out_preview,
                                               out_error);
  });
}

void otgy_chart_paste_preview_destroy(otgy_chart_paste_preview* preview) {
  delete preview;
}

std::uint32_t otgy_chart_paste_preview_get_code(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr ? OTGY_CHART_EDIT_CLIPBOARD_EMPTY : preview->code;
}

std::uint64_t otgy_chart_paste_preview_get_first_row(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr ? 0U : preview->first_row;
}

std::uint64_t otgy_chart_paste_preview_get_last_row(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr ? 0U : preview->last_row;
}

const otgy_chart_selection* otgy_chart_paste_preview_get_selections(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr || preview->selections.empty()
             ? nullptr
             : preview->selections.data();
}

std::uint64_t otgy_chart_paste_preview_get_selection_count(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr ? 0U : preview->selections.size();
}

const otgy_chart_marker* otgy_chart_paste_preview_get_markers(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr || preview->markers.empty()
             ? nullptr
             : preview->markers.data();
}

std::uint64_t otgy_chart_paste_preview_get_marker_count(
    const otgy_chart_paste_preview* preview) {
  return preview == nullptr ? 0U : preview->markers.size();
}

otgy_status otgy_chart_validate_finished(
    const otgy_chart* chart, otgy_chart_validation** out_validation,
    otgy_error** out_error) {
  if (out_validation != nullptr) {
    *out_validation = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_validation == nullptr) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::validate_finished_chart(chart->value);
    if (result.error() != otgy::core::ChartValidationError::None) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_CHART_VALIDATION,
                        static_cast<std::uint32_t>(result.error()),
                        "unable to allocate chart validation state");
    }
    std::optional<otgy::core::FinishedChart> finished;
    std::vector<otgy_chart_diagnostic> diagnostics;
    if (result.is_finished()) {
      finished.emplace(std::move(result).take_finished_chart());
    } else {
      diagnostics.reserve(result.diagnostics()->size());
      for (const auto& diagnostic : *result.diagnostics()) {
        diagnostics.push_back(
            {static_cast<std::uint32_t>(diagnostic.code),
             diagnostic.object_id, diagnostic.related_id, diagnostic.row});
      }
    }
    return make_handle<otgy_chart_validation>(
        otgy_chart_validation{std::move(finished), std::move(diagnostics)},
        out_validation, out_error);
  });
}

void otgy_chart_validation_destroy(otgy_chart_validation* validation) {
  delete validation;
}

std::uint8_t otgy_chart_validation_is_finished(
    const otgy_chart_validation* validation) {
  return validation != nullptr && validation->finished.has_value() ? 1U : 0U;
}

const otgy_chart_diagnostic* otgy_chart_validation_get_diagnostics(
    const otgy_chart_validation* validation) {
  return validation == nullptr || validation->diagnostics.empty()
             ? nullptr
             : validation->diagnostics.data();
}

std::uint64_t otgy_chart_validation_get_diagnostic_count(
    const otgy_chart_validation* validation) {
  return validation == nullptr
             ? 0U
             : static_cast<std::uint64_t>(validation->diagnostics.size());
}

otgy_status otgy_chart_validation_copy_finished(
    const otgy_chart_validation* validation,
    otgy_finished_chart** out_finished_chart, otgy_error** out_error) {
  if (out_finished_chart != nullptr) {
    *out_finished_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (validation == nullptr || out_finished_chart == nullptr) {
      return invalid_argument(out_error);
    }
    if (!validation->finished.has_value()) {
      return invalid_argument(out_error, OTGY_ABI_ERROR_RESULT_NOT_AVAILABLE,
                              kResultNotAvailable);
    }
    return make_handle<otgy_finished_chart>(
        otgy_finished_chart{*validation->finished}, out_finished_chart,
        out_error);
  });
}

void otgy_finished_chart_destroy(otgy_finished_chart* chart) { delete chart; }

otgy_status otgy_finished_chart_export_slk(
    const otgy_finished_chart* chart, otgy_bytes** out_bytes,
    otgy_error** out_error) {
  if (out_bytes != nullptr) {
    *out_bytes = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_bytes == nullptr) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::export_finished_chart_to_slk(chart->value);
    if (!result) {
      return core_error(out_error, OTGY_ERROR_DOMAIN_SLK_EXPORT,
                        static_cast<std::uint32_t>(result.error()),
                        otgy::core::slk_export_error_message(result.error()));
    }
    return make_bytes(std::move(result).take_bytes(), out_bytes, out_error);
  });
}

otgy_status otgy_slk_import(otgy_byte_view bytes,
                            otgy_slk_document** out_document,
                            otgy_error** out_error) {
  if (out_document != nullptr) {
    *out_document = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::size_t size = 0U;
    if (out_document == nullptr || !valid_bytes(bytes, size)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::import_finished_slk(bytes.data, size);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_SLK_IMPORT,
          static_cast<std::uint32_t>(result.failure()->error),
          otgy::core::slk_import_error_message(result.failure()->error));
    }
    return make_handle<otgy_slk_document>(
        otgy_slk_document{std::move(result).take_document()}, out_document,
        out_error);
  });
}

void otgy_slk_document_destroy(otgy_slk_document* document) { delete document; }

otgy_status otgy_slk_document_project_chart(
    const otgy_slk_document* document, otgy_chart** out_chart,
    otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (document == nullptr || out_chart == nullptr) {
      return invalid_argument(out_error);
    }
    auto result =
        otgy::core::project_finished_slk_to_chart_draft(document->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_SLK_CHART_PROJECTION,
          static_cast<std::uint32_t>(result.failure()->error),
          otgy::core::slk_chart_draft_projection_error_message(
              result.failure()->error));
    }
    return make_handle<otgy_chart>(
        otgy_chart{std::move(result).take_draft()}, out_chart, out_error);
  });
}

otgy_status otgy_project_encode_formal_draft(
    const otgy_chart* chart, double bpm, double offset_seconds,
    otgy_bytes** out_bytes, otgy_error** out_error) {
  if (out_bytes != nullptr) {
    *out_bytes = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_bytes == nullptr) {
      return invalid_argument(out_error);
    }
    auto result =
        otgy::core::encode_formal_draft(chart->value, bpm, offset_seconds);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PROJECT_SERIALIZATION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::project_serialization_error_message(result.error()));
    }
    return make_bytes(std::move(result).take_bytes(), out_bytes, out_error);
  });
}

otgy_status otgy_project_decode_formal_draft(
    otgy_byte_view bytes, otgy_chart** out_chart, double* out_bpm,
    double* out_offset_seconds, otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::size_t size = 0U;
    if (out_chart == nullptr || out_bpm == nullptr ||
        out_offset_seconds == nullptr || !valid_bytes(bytes, size)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::decode_formal_draft(bytes.data, size);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PROJECT_SERIALIZATION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::project_serialization_error_message(result.error()));
    }
    auto payload = std::move(result).take_payload();
    auto* chart = new (std::nothrow) otgy_chart{std::move(payload.chart)};
    if (chart == nullptr) {
      return out_of_memory(out_error);
    }
    *out_bpm = payload.bpm;
    *out_offset_seconds = payload.offset_seconds;
    *out_chart = chart;
    return OTGY_STATUS_OK;
  });
}

otgy_status otgy_project_encode_recovery_snapshot(
    const otgy_chart* chart, otgy_bytes** out_bytes, otgy_error** out_error) {
  if (out_bytes != nullptr) {
    *out_bytes = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    if (chart == nullptr || out_bytes == nullptr) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::encode_recovery_snapshot(chart->value);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PROJECT_SERIALIZATION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::project_serialization_error_message(result.error()));
    }
    return make_bytes(std::move(result).take_bytes(), out_bytes, out_error);
  });
}

otgy_status otgy_project_decode_recovery_snapshot(
    otgy_byte_view bytes, otgy_chart** out_chart, otgy_error** out_error) {
  if (out_chart != nullptr) {
    *out_chart = nullptr;
  }
  return boundary(out_error, [&]() -> otgy_status {
    std::size_t size = 0U;
    if (out_chart == nullptr || !valid_bytes(bytes, size)) {
      return invalid_argument(out_error);
    }
    auto result = otgy::core::decode_recovery_snapshot(bytes.data, size);
    if (!result) {
      return core_error(
          out_error, OTGY_ERROR_DOMAIN_PROJECT_SERIALIZATION,
          static_cast<std::uint32_t>(result.error()),
          otgy::core::project_serialization_error_message(result.error()));
    }
    return make_handle<otgy_chart>(
        otgy_chart{std::move(result).take_chart()}, out_chart, out_error);
  });
}

otgy_status otgy_compose_slk_position(
    std::uint64_t measure, std::uint64_t beat, std::uint64_t sixteenth,
    otgy_slk_position* out_position, otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_slk_position(
        otgy::core::compose_slk_position(measure, beat, sixteenth),
        out_position, out_error);
  });
}

otgy_status otgy_decompose_slk_position(
    std::uint64_t total_sixteenth, otgy_slk_position* out_position,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_slk_position(
        otgy::core::decompose_slk_position(total_sixteenth), out_position,
        out_error);
  });
}

otgy_status otgy_validate_slk_position(
    std::uint64_t measure, std::uint64_t beat, std::uint64_t sixteenth,
    std::uint64_t total_sixteenth, otgy_slk_position* out_position,
    otgy_error** out_error) {
  return boundary(out_error, [&]() -> otgy_status {
    if (out_position == nullptr) {
      return invalid_argument(out_error);
    }
    return return_slk_position(
        otgy::core::validate_slk_position(measure, beat, sixteenth,
                                          total_sixteenth),
        out_position, out_error);
  });
}

}  // extern "C"
