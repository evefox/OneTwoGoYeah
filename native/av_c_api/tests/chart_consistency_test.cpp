// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include "av_core/chart/chart_editor.hpp"
#include "av_core/chart/chart_projection.hpp"
#include "av_core/chart/chart_validator.hpp"
#include "av_core/coordination/operation_gate.hpp"
#include "av_core/playback/playback_position.hpp"
#include "av_core/project/project_serialization.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"
#include "av_core/slk/slk_export.hpp"
#include "av_core/slk/slk_import.hpp"
#include "av_core/slk/slk_position.hpp"
#include "av_core/timing/timeline_navigation.hpp"
#include "av_core/timing/timeline_range.hpp"
#include "av_core/timing/timing_conversion.hpp"
#include "av_core/timing/timing_grid.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool same_double(double left, double right) {
  return std::memcmp(&left, &right, sizeof(double)) == 0;
}

std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  expect(stream.good(), "unable to open SLK fixture");
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

otgy_byte_view view(const std::vector<std::uint8_t>& bytes) {
  return {bytes.empty() ? nullptr : bytes.data(),
          static_cast<std::uint64_t>(bytes.size())};
}

template <typename T, void (*Destroy)(T*)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

template <typename T, void (*Destroy)(T*)>
Handle<T, Destroy> handle(T* value) {
  return Handle<T, Destroy>(value, Destroy);
}

void expect_error(const otgy_error* error, otgy_error_domain domain,
                  std::uint32_t code, std::string_view message) {
  expect(error != nullptr, "missing C ABI error");
  expect(otgy_error_get_domain(error) == domain,
         "C ABI error domain differs");
  expect(otgy_error_get_code(error) == code, "C ABI error code differs");
  const auto actual = otgy_error_get_message(error);
  expect(std::string_view(actual.data, static_cast<std::size_t>(actual.length)) ==
             message,
         "C ABI error message differs");
}

otgy::core::ChartDraft empty_chart() {
  return {{}, {}, {}, {}, {}, {}};
}

otgy::core::ChartEditor build_two_complete_moves() {
  otgy::core::ChartEditor editor(empty_chart());
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 0U)),
         "first move NPC-C failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 2U)),
         "first move NPC-N failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 4U)),
         "first move NPC-N failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 6U)),
         "first move NPC-N failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 6U)),
         "first move NPC-S failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Player, 8U)),
         "first move PLY-C failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Player, 10U)),
         "first move player generation failed");

  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 16U)),
         "second move NPC-C failed");
  for (std::uint64_t row = 18U; row <= 32U; row += 2U) {
    expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, row)),
           "second move NPC-N failed");
  }
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Npc, 32U)),
         "second move NPC-S failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Player, 34U)),
         "second move PLY-C failed");
  expect(static_cast<bool>(editor.click(otgy::core::ChartColumn::Player, 36U)),
         "second move player generation failed");
  return editor;
}

void compare_edit(const otgy::core::ChartEditResult& direct,
                  const otgy_chart_edit_result& abi) {
  expect(abi.code == static_cast<std::uint32_t>(direct.code) &&
             abi.changed == static_cast<std::uint8_t>(direct.changed) &&
             abi.primary_event_id == direct.primary_event_id.value,
         "chart edit result differs");
}

void compare_selection(const otgy::core::ChartSelection& direct,
                       const otgy_chart_selection& abi) {
  expect(abi.active == static_cast<std::uint8_t>(direct.active) &&
             abi.dance_move_drill_id == direct.drill_id.value &&
             abi.first_row == direct.first_row &&
             abi.last_row == direct.last_row,
         "chart selection differs");
}

void compare_session(const otgy::core::ChartSessionResult& direct,
                     const otgy_chart_session_result& abi) {
  expect(abi.code == static_cast<std::uint32_t>(direct.code) &&
             abi.changed == static_cast<std::uint8_t>(direct.changed),
         "chart session result differs");
}

void compare_selections(
    const std::vector<otgy::core::ChartSelection>& direct,
    const otgy_chart_selections* abi) {
  expect(otgy_chart_selections_get_count(abi) == direct.size(),
         "chart selection count differs");
  const auto* values = otgy_chart_selections_get_data(abi);
  expect(direct.empty() || values != nullptr, "missing chart selection data");
  for (std::size_t index = 0U; index < direct.size(); ++index) {
    compare_selection(direct[index], values[index]);
    if (index != 0U) {
      const auto& previous = values[index - 1U];
      const auto& current = values[index];
      expect(previous.first_row < current.first_row ||
                 (previous.first_row == current.first_row &&
                  previous.dance_move_drill_id < current.dance_move_drill_id),
             "chart selections are not strictly ordered by row and drill identity");
    }
  }
}

void compare_session_state(const otgy::core::ChartEditor& direct,
                           const otgy_chart_editor* abi) {
  otgy_chart_selections* raw_selections = nullptr;
  otgy_error* raw_error = nullptr;
  expect(otgy_chart_editor_copy_selections(abi, &raw_selections, &raw_error) ==
             OTGY_STATUS_OK && raw_error == nullptr && raw_selections != nullptr,
         "C ABI selection snapshot failed");
  auto selections =
      handle<otgy_chart_selections, otgy_chart_selections_destroy>(raw_selections);
  compare_selections(direct.selections(), selections.get());
  expect(otgy_chart_editor_selection_count(abi) == direct.selections().size() &&
             otgy_chart_editor_has_clipboard(abi) ==
                 static_cast<std::uint8_t>(direct.has_clipboard()) &&
             otgy_chart_editor_clipboard_count(abi) ==
                 direct.clipboard().content.dance_move_drills().size() &&
             otgy_chart_editor_cut_pending(abi) ==
                 static_cast<std::uint8_t>(direct.has_pending_cut()) &&
             otgy_chart_editor_get_undo_count(abi) == direct.undo_count() &&
             otgy_chart_editor_get_redo_count(abi) == direct.redo_count(),
         "chart session state differs");
}

void compare_preview(const otgy::core::ChartPastePreview& direct,
                     const otgy_chart_paste_preview* abi) {
  expect(abi != nullptr &&
             otgy_chart_paste_preview_get_code(abi) ==
                 static_cast<std::uint32_t>(direct.code) &&
             otgy_chart_paste_preview_get_first_row(abi) == direct.anchor_row &&
             otgy_chart_paste_preview_get_last_row(abi) == direct.last_row &&
             otgy_chart_paste_preview_get_selection_count(abi) ==
                 direct.spans.size() &&
             otgy_chart_paste_preview_get_marker_count(abi) ==
                 direct.ghost_markers.size(),
         "paste preview envelope or counts differ");
  const auto* spans = otgy_chart_paste_preview_get_selections(abi);
  expect(direct.spans.empty() || spans != nullptr,
         "missing paste preview spans");
  for (std::size_t index = 0U; index < direct.spans.size(); ++index) {
    compare_selection(direct.spans[index], spans[index]);
  }
  const auto* markers = otgy_chart_paste_preview_get_markers(abi);
  expect(direct.ghost_markers.empty() || markers != nullptr,
         "missing paste preview markers");
  for (std::size_t index = 0U; index < direct.ghost_markers.size(); ++index) {
    const auto& value = direct.ghost_markers[index];
    expect(markers[index].id == value.id.value &&
               markers[index].row == value.row &&
               markers[index].column == static_cast<std::uint32_t>(value.column) &&
               markers[index].type == static_cast<std::uint32_t>(value.type) &&
               markers[index].show_n_count == value.show_n_count,
           "paste preview marker differs");
  }
}

void compare_markers(const std::vector<otgy::core::ChartMarker>& direct,
                     const otgy_chart_markers* abi) {
  expect(otgy_chart_markers_get_count(abi) == direct.size(),
         "chart marker count differs");
  const auto* values = otgy_chart_markers_get_data(abi);
  for (std::size_t index = 0U; index < direct.size(); ++index) {
    expect(values[index].id == direct[index].id.value &&
               values[index].row == direct[index].row &&
               values[index].column ==
                   static_cast<std::uint32_t>(direct[index].column) &&
               values[index].type ==
                   static_cast<std::uint32_t>(direct[index].type) &&
               values[index].show_n_count == direct[index].show_n_count,
           "chart marker differs");
  }
}

void compare_showtime_moves(
    const std::vector<otgy::core::ShowtimeMoveProjection>& direct,
    const otgy_showtime_moves* abi) {
  expect(otgy_showtime_moves_get_count(abi) == direct.size(),
         "Showtime move count differs");
  const auto* values = otgy_showtime_moves_get_data(abi);
  for (std::size_t index = 0U; index < direct.size(); ++index) {
    expect(values[index].event_id == direct[index].event_id.value &&
               values[index].row == direct[index].row &&
               values[index].show_n_count == direct[index].show_n_count &&
               values[index].range_start == direct[index].range_start &&
               values[index].range_end == direct[index].range_end &&
               values[index].is_first_in_sequence ==
                   static_cast<std::uint8_t>(
                       direct[index].is_first_in_sequence) &&
               values[index].is_last_in_sequence ==
                   static_cast<std::uint8_t>(direct[index].is_last_in_sequence),
           "Showtime move projection differs");
  }
}

void compare_semantics(const otgy::core::ChartDraft& direct,
                       const otgy_chart_semantics* abi) {
  expect(otgy_chart_semantics_get_role_events_count(abi) ==
             direct.role_events().size(),
         "role event count differs");
  const auto* role_events = otgy_chart_semantics_get_role_events(abi);
  for (std::size_t index = 0U; index < direct.role_events().size(); ++index) {
    const auto& value = direct.role_events()[index];
    expect(role_events[index].id == value.id.value &&
               role_events[index].row == value.row &&
               role_events[index].role ==
                   static_cast<std::uint32_t>(value.role) &&
               role_events[index].type ==
                   static_cast<std::uint32_t>(value.type) &&
               role_events[index].drill_group_id ==
                   value.drill_group_id.value &&
               role_events[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value,
           "role event semantics differ");
  }

  expect(otgy_chart_semantics_get_showtime_start_events_count(abi) ==
             direct.showtime_start_events().size(),
         "Showtime start event count differs");
  const auto* starts =
      otgy_chart_semantics_get_showtime_start_events(abi);
  for (std::size_t index = 0U;
       index < direct.showtime_start_events().size(); ++index) {
    const auto& value = direct.showtime_start_events()[index];
    expect(starts[index].id == value.id.value &&
               starts[index].row == value.row &&
               starts[index].showtime_id == value.showtime_id.value,
           "Showtime start semantics differ");
  }

  expect(otgy_chart_semantics_get_showtime_move_events_count(abi) ==
             direct.showtime_move_events().size(),
         "Showtime move event count differs");
  const auto* moves = otgy_chart_semantics_get_showtime_move_events(abi);
  for (std::size_t index = 0U;
       index < direct.showtime_move_events().size(); ++index) {
    const auto& value = direct.showtime_move_events()[index];
    expect(moves[index].id == value.id.value &&
               moves[index].row == value.row &&
               moves[index].showtime_id == value.showtime_id.value &&
               moves[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value &&
               moves[index].show_n_count == value.show_n_count,
           "Showtime move semantics differ");
  }

  expect(otgy_chart_semantics_get_drill_groups_count(abi) ==
             direct.drill_groups().size(),
         "drill group count differs");
  const auto* groups = otgy_chart_semantics_get_drill_groups(abi);
  const auto* group_event_ids =
      otgy_chart_semantics_get_drill_group_event_ids(abi);
  std::uint64_t expected_group_event_count = 0U;
  for (std::size_t index = 0U; index < direct.drill_groups().size(); ++index) {
    const auto& value = direct.drill_groups()[index];
    expect(groups[index].id == value.id.value &&
               groups[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value,
           "drill group semantics differ");
    expect(groups[index].npc_event_id_offset == expected_group_event_count &&
               groups[index].npc_event_id_count ==
                   value.npc_event_ids.size(),
           "NPC drill group slice differs");
    for (std::size_t item = 0U; item < value.npc_event_ids.size(); ++item) {
      expect(group_event_ids[groups[index].npc_event_id_offset + item] ==
                 value.npc_event_ids[item].value,
             "NPC drill group event differs");
    }
    expected_group_event_count += value.npc_event_ids.size();
    expect(groups[index].player_event_id_offset ==
                   expected_group_event_count &&
               groups[index].player_event_id_count ==
                   value.player_event_ids.size(),
           "player drill group slice differs");
    for (std::size_t item = 0U; item < value.player_event_ids.size(); ++item) {
      expect(group_event_ids[groups[index].player_event_id_offset + item] ==
                 value.player_event_ids[item].value,
             "player drill group event differs");
    }
    expected_group_event_count += value.player_event_ids.size();
  }
  expect(otgy_chart_semantics_get_drill_group_event_ids_count(abi) ==
             expected_group_event_count,
         "drill group backing ID count differs");

  expect(otgy_chart_semantics_get_dance_move_drills_count(abi) ==
             direct.dance_move_drills().size(),
         "dance move drill count differs");
  const auto* drills = otgy_chart_semantics_get_dance_move_drills(abi);
  const auto* drill_group_ids =
      otgy_chart_semantics_get_dance_move_drill_group_ids(abi);
  std::uint64_t expected_drill_group_count = 0U;
  for (std::size_t index = 0U; index < direct.dance_move_drills().size();
       ++index) {
    const auto& value = direct.dance_move_drills()[index];
    expect(drills[index].id == value.id.value &&
               drills[index].move_n_count == value.move_n_count &&
               drills[index].drill_group_id_offset ==
                   expected_drill_group_count &&
               drills[index].drill_group_id_count ==
                   value.drill_group_ids.size(),
           "dance move drill semantics differ");
    for (std::size_t item = 0U; item < value.drill_group_ids.size(); ++item) {
      expect(drill_group_ids[drills[index].drill_group_id_offset + item] ==
                 value.drill_group_ids[item].value,
             "dance move drill group differs");
    }
    expected_drill_group_count += value.drill_group_ids.size();
  }
  expect(
      otgy_chart_semantics_get_dance_move_drill_group_ids_count(abi) ==
          expected_drill_group_count,
      "dance move drill backing ID count differs");

  expect(otgy_chart_semantics_get_showtimes_count(abi) ==
             direct.showtimes().size(),
         "Showtime relation count differs");
  const auto* showtimes = otgy_chart_semantics_get_showtimes(abi);
  const auto* showtime_drill_ids =
      otgy_chart_semantics_get_showtime_dance_move_drill_ids(abi);
  const auto* showtime_move_ids =
      otgy_chart_semantics_get_showtime_move_event_ids(abi);
  std::uint64_t expected_showtime_drill_count = 0U;
  std::uint64_t expected_showtime_move_count = 0U;
  for (std::size_t index = 0U; index < direct.showtimes().size(); ++index) {
    const auto& value = direct.showtimes()[index];
    expect(showtimes[index].id == value.id.value &&
               showtimes[index].start_event_id ==
                   value.start_event_id.value &&
               showtimes[index].dance_move_drill_id_offset ==
                   expected_showtime_drill_count &&
               showtimes[index].dance_move_drill_id_count ==
                   value.dance_move_drill_ids.size() &&
               showtimes[index].show_move_event_id_offset ==
                   expected_showtime_move_count &&
               showtimes[index].show_move_event_id_count ==
                   value.show_move_event_ids.size(),
           "Showtime relation semantics differ");
    for (std::size_t item = 0U; item < value.dance_move_drill_ids.size();
         ++item) {
      expect(showtime_drill_ids
                 [showtimes[index].dance_move_drill_id_offset + item] ==
                 value.dance_move_drill_ids[item].value,
             "Showtime dance move reference differs");
    }
    for (std::size_t item = 0U; item < value.show_move_event_ids.size();
         ++item) {
      expect(showtime_move_ids
                 [showtimes[index].show_move_event_id_offset + item] ==
                 value.show_move_event_ids[item].value,
             "Showtime move event reference differs");
    }
    expected_showtime_drill_count += value.dance_move_drill_ids.size();
    expected_showtime_move_count += value.show_move_event_ids.size();
  }
  expect(
      otgy_chart_semantics_get_showtime_dance_move_drill_ids_count(abi) ==
              expected_showtime_drill_count &&
          otgy_chart_semantics_get_showtime_move_event_ids_count(abi) ==
              expected_showtime_move_count,
      "Showtime backing ID counts differ");
}

struct EditorPair final {
  otgy::core::ChartEditor direct;
  Handle<otgy_chart_editor, otgy_chart_editor_destroy> abi;

  explicit EditorPair(const otgy::core::ChartDraft& draft)
      : direct(draft), abi(nullptr, otgy_chart_editor_destroy) {
    const auto encoded = otgy::core::encode_recovery_snapshot(draft);
    expect(static_cast<bool>(encoded), "editor pair fixture encode failed");
    otgy_chart* raw_chart = nullptr;
    otgy_error* error = nullptr;
    expect(otgy_project_decode_recovery_snapshot(view(*encoded.bytes()),
                                                &raw_chart, &error) ==
               OTGY_STATUS_OK && error == nullptr,
           "editor pair fixture decode failed");
    auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
    otgy_chart_editor* raw_editor = nullptr;
    expect(otgy_chart_editor_create(chart.get(), &raw_editor, &error) ==
               OTGY_STATUS_OK && error == nullptr,
           "editor pair creation failed");
    abi.reset(raw_editor);
    compare();
  }

  void compare() const {
    otgy_error* error = nullptr;
    otgy_chart* raw_chart = nullptr;
    expect(otgy_chart_editor_copy_chart(abi.get(), &raw_chart, &error) ==
               OTGY_STATUS_OK && error == nullptr,
           "editor pair chart copy failed");
    auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(chart.get(), &raw_semantics, &error) ==
               OTGY_STATUS_OK && error == nullptr,
           "editor pair semantic copy failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct.draft(), semantics.get());
    compare_session_state(direct, abi.get());
  }

  otgy::core::ChartSessionResult select(otgy::core::ChartColumn column,
                                       std::uint64_t row) {
    const auto result = direct.select(column, row);
    otgy_chart_session_result projected{};
    otgy_error* error = nullptr;
    expect(otgy_chart_editor_select(abi.get(), static_cast<std::uint32_t>(column),
                                    row, &projected, &error) == OTGY_STATUS_OK &&
               error == nullptr,
           "editor pair select failed");
    compare_session(result, projected);
    compare();
    return result;
  }

  otgy::core::ChartSessionResult session(
      otgy::core::ChartSessionResult (otgy::core::ChartEditor::*core)() noexcept,
      otgy_status (*function)(otgy_chart_editor*, otgy_chart_session_result*,
                             otgy_error**)) {
    const auto result = (direct.*core)();
    otgy_chart_session_result projected{};
    otgy_error* error = nullptr;
    expect(function(abi.get(), &projected, &error) == OTGY_STATUS_OK &&
               error == nullptr,
           "editor pair session command failed");
    compare_session(result, projected);
    compare();
    return result;
  }

  otgy::core::ChartEditResult paste(std::uint64_t row) {
    const auto result = direct.paste(row);
    otgy_chart_edit_result projected{};
    otgy_error* error = nullptr;
    expect(otgy_chart_editor_paste(abi.get(), row, &projected, &error) ==
               OTGY_STATUS_OK && error == nullptr,
           "editor pair paste failed");
    compare_edit(result, projected);
    compare();
    return result;
  }

  otgy::core::ChartEditResult click(otgy::core::ChartColumn column,
                                   std::uint64_t row) {
    const auto result = direct.click(column, row);
    otgy_chart_edit_result projected{};
    otgy_error* error = nullptr;
    expect(otgy_chart_editor_click(abi.get(), static_cast<std::uint32_t>(column),
                                   row, &projected, &error) == OTGY_STATUS_OK &&
               error == nullptr,
           "editor pair click failed");
    compare_edit(result, projected);
    compare();
    return result;
  }

  otgy::core::ChartEditResult history(bool undo) {
    const auto result = undo ? direct.undo() : direct.redo();
    otgy_chart_edit_result projected{};
    otgy_error* error = nullptr;
    const auto status = undo
        ? otgy_chart_editor_undo(abi.get(), &projected, &error)
        : otgy_chart_editor_redo(abi.get(), &projected, &error);
    expect(status == OTGY_STATUS_OK && error == nullptr,
           "editor pair history command failed");
    compare_edit(result, projected);
    compare();
    return result;
  }

  Handle<otgy_chart_paste_preview, otgy_chart_paste_preview_destroy> preview(
      std::uint64_t row) const {
    const auto before = direct.draft();
    const auto undo = direct.undo_count();
    const auto redo = direct.redo_count();
    const auto selections = direct.selections();
    const auto clipboard = direct.clipboard();
    const auto pending = direct.has_pending_cut();
    const auto expected = direct.preview_paste(row);
    otgy_chart_paste_preview* raw_preview = nullptr;
    otgy_error* error = nullptr;
    expect(otgy_chart_editor_preview_paste(abi.get(), row, &raw_preview,
                                          &error) == OTGY_STATUS_OK &&
               error == nullptr,
           "editor pair preview failed");
    auto result = handle<otgy_chart_paste_preview,
                         otgy_chart_paste_preview_destroy>(raw_preview);
    compare_preview(expected, result.get());
    expect(otgy::core::chart_drafts_equal(before, direct.draft()) &&
               direct.undo_count() == undo && direct.redo_count() == redo &&
               direct.has_pending_cut() == pending &&
               direct.clipboard().base_row == clipboard.base_row &&
               otgy::core::chart_drafts_equal(direct.clipboard().content,
                                               clipboard.content),
           "preview mutated the draft, clipboard or history");
    otgy_chart_selections* raw_selections = nullptr;
    expect(otgy_chart_editor_copy_selections(abi.get(), &raw_selections,
                                             &error) == OTGY_STATUS_OK,
           "preview selection read-back failed");
    auto selection_copy =
        handle<otgy_chart_selections, otgy_chart_selections_destroy>(raw_selections);
    compare_selections(selections, selection_copy.get());
    compare();
    return result;
  }
};

void test_batch_selection_and_preview() {
  auto fixture = build_two_complete_moves();
  expect(static_cast<bool>(fixture.select(otgy::core::ChartColumn::Npc, 0U)) &&
             static_cast<bool>(fixture.copy_selection()) &&
             static_cast<bool>(fixture.paste(100U)),
         "third move fixture creation failed");
  EditorPair pair(fixture.draft());
  const auto original = pair.direct.draft();
  auto empty_preview = pair.preview(200U);
  expect(otgy_chart_paste_preview_get_code(empty_preview.get()) ==
             OTGY_CHART_EDIT_CLIPBOARD_EMPTY,
         "empty clipboard preview was not reported");

  const auto markers = otgy::core::project_chart_markers(original);
  std::array<bool, 6> seen{};
  for (const auto& marker : *markers.markers()) {
    if (marker.row >= 16U) {
      continue;
    }
    expect(pair.select(marker.column, marker.row).changed &&
               pair.direct.selections().size() == 1U,
           "role marker did not select its move");
    expect(pair.select(marker.column, marker.row).changed &&
               pair.direct.selections().empty(),
           "repeated role marker did not toggle its move off");
    seen[static_cast<std::size_t>(marker.type)] = true;
  }
  expect(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
         "selection fixture omitted a role marker type");
  pair.select(otgy::core::ChartColumn::Npc, 100U);
  pair.select(otgy::core::ChartColumn::Npc, 0U);
  expect(pair.direct.selections().size() == 2U &&
             pair.direct.selections()[0].first_row == 0U &&
             pair.direct.selections()[1].first_row == 100U,
         "noncontiguous selection order depends on click order");
  const auto selected = pair.direct.selections();
  const auto blank = pair.select(otgy::core::ChartColumn::Npc, 99U);
  expect(!blank.changed && pair.direct.selections().size() == 2U,
         "blank click discarded selections");
  const auto cross_column = pair.select(otgy::core::ChartColumn::Player, 0U);
  expect(cross_column.changed && pair.direct.selections().size() == 1U &&
             pair.direct.selections().front().first_row == 100U,
         "cross-column click did not toggle the marker-row move");
  expect(pair.select(otgy::core::ChartColumn::Showtime, 0U).changed &&
             pair.direct.selections().size() == 2U,
         "cross-column click did not restore the marker-row move");
  pair.select(otgy::core::ChartColumn::Npc, 100U);
  expect(pair.direct.selections().size() == 1U &&
             pair.direct.selections().front().first_row == 0U,
         "toggle removed another selected move");
  pair.select(otgy::core::ChartColumn::Npc, 100U);

  otgy_chart_selections* raw_owned = nullptr;
  otgy_error* error = nullptr;
  expect(otgy_chart_editor_copy_selections(pair.abi.get(), &raw_owned, &error) ==
             OTGY_STATUS_OK && error == nullptr,
         "owned selection creation failed");
  auto owned =
      handle<otgy_chart_selections, otgy_chart_selections_destroy>(raw_owned);
  pair.session(&otgy::core::ChartEditor::copy_selection,
               otgy_chart_editor_copy_selection);
  expect(pair.direct.undo_count() == 0U &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), original) &&
             otgy_chart_editor_clipboard_count(pair.abi.get()) == 2U &&
             !pair.direct.has_pending_cut(),
         "batch copy changed draft or history");
  otgy::core::ChartEditor control(original);
  static_cast<void>(control.select(otgy::core::ChartColumn::Npc, 100U));
  static_cast<void>(control.select(otgy::core::ChartColumn::Npc, 0U));
  expect(static_cast<bool>(control.copy_selection()), "control copy failed");

  EditorPair gap(original);
  gap.select(otgy::core::ChartColumn::Npc, 0U);
  gap.select(otgy::core::ChartColumn::Npc, 100U);
  gap.session(&otgy::core::ChartEditor::copy_selection,
              otgy_chart_editor_copy_selection);
  expect(gap.click(otgy::core::ChartColumn::Npc, 250U).changed,
         "batch envelope obstruction setup failed");
  auto gap_collision = gap.preview(200U);
  expect(otgy_chart_paste_preview_get_code(gap_collision.get()) ==
             OTGY_CHART_EDIT_PASTE_RANGE_OCCUPIED &&
             otgy_chart_paste_preview_get_selection_count(gap_collision.get()) == 2U &&
             gap.paste(200U).code == otgy::core::ChartEditCode::PasteRangeOccupied,
         "batch paste ignored occupied space between fragments");
  auto preview = pair.preview(200U);
  const auto expected_preview = pair.direct.preview_paste(200U);
  expect(expected_preview.code == otgy::core::ChartEditCode::Success &&
             expected_preview.spans.size() == 2U &&
             expected_preview.spans[0].first_row == 200U &&
             expected_preview.spans[1].first_row == 300U &&
             expected_preview.last_row == 314U,
         "batch preview lost shared base or relative gaps");
  auto overflow = pair.preview(UINT64_MAX);
  expect(otgy_chart_paste_preview_get_code(overflow.get()) ==
             OTGY_CHART_EDIT_RANGE_OVERFLOW &&
             pair.paste(UINT64_MAX).code == otgy::core::ChartEditCode::RangeOverflow,
         "preview and commit disagree on row overflow");
  expect(pair.direct.undo_count() == 0U &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), original),
         "overflow changed the draft or history");
  expect(pair.paste(200U).changed && static_cast<bool>(control.paste(200U)) &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), control.draft()),
         "preview or refusal consumed object identifiers");
  expect(pair.direct.undo_count() == 1U &&
             pair.direct.selections().size() == 2U &&
             pair.direct.selections()[0].first_row == 200U &&
             pair.direct.selections()[1].first_row == 300U &&
             pair.direct.selections()[0].drill_id.value != selected[0].drill_id.value &&
             pair.direct.selections()[1].drill_id.value != selected[1].drill_id.value,
         "batch paste lost its fresh identities, selection or single history");
  const auto pasted_markers = otgy::core::project_chart_markers(pair.direct.draft());
  for (const auto& ghost : expected_preview.ghost_markers) {
    const auto match = std::find_if(
        pasted_markers.markers()->begin(), pasted_markers.markers()->end(),
        [&ghost](const otgy::core::ChartMarker& marker) {
          return marker.row == ghost.row && marker.column == ghost.column &&
                 marker.type == ghost.type &&
                 marker.show_n_count == ghost.show_n_count;
        });
    expect(match != pasted_markers.markers()->end() &&
               match->id.value != ghost.id.value,
           "committed marker differs from its preview geometry");
  }
  pair.session(&otgy::core::ChartEditor::clear_selection,
               otgy_chart_editor_clear_selection);
  pair.session(&otgy::core::ChartEditor::clear_clipboard,
               otgy_chart_editor_clear_clipboard);
  compare_selections(selected, owned.get());
  compare_preview(expected_preview, preview.get());
  pair.abi.reset();
  compare_selections(selected, owned.get());
  compare_preview(expected_preview, preview.get());
}

void test_selection_order_and_lifecycle() {
  const otgy::core::ChartDraft tied{
      {{{101U}, 0U, otgy::core::ChartRole::Npc,
        otgy::core::ChartRoleEventType::BlockStart, {201U}, {20U}},
       {{102U}, 0U, otgy::core::ChartRole::Player,
        otgy::core::ChartRoleEventType::BlockStart, {202U}, {10U}}},
      {}, {},
      {{{201U}, {20U}, {{101U}}, {}}, {{202U}, {10U}, {}, {{102U}}}},
      {{{20U}, 0U, {{201U}}}, {{10U}, 0U, {{202U}}}}, {}};
  // Global row uniqueness rejects this draft before selection can be derived;
  // distinct selectable dance moves cannot have equal first rows.
  EditorPair invalid(tied);
  for (const auto column : {otgy::core::ChartColumn::Npc,
                             otgy::core::ChartColumn::Player}) {
    const auto result = invalid.select(column, 0U);
    expect(result.code == otgy::core::ChartEditCode::InvalidDraft &&
               !result.changed && invalid.direct.selections().empty() &&
               invalid.direct.undo_count() == 0U &&
               otgy::core::chart_drafts_equal(invalid.direct.draft(), tied),
           "duplicate-row selection changed an invalid draft or session");
  }

  const otgy::core::ChartDraft ordered{
      {{{101U}, 0U, otgy::core::ChartRole::Npc,
        otgy::core::ChartRoleEventType::BlockStart, {201U}, {20U}},
       {{102U}, 16U, otgy::core::ChartRole::Player,
        otgy::core::ChartRoleEventType::BlockStart, {202U}, {10U}}},
      {}, {},
      {{{201U}, {20U}, {{101U}}, {}}, {{202U}, {10U}, {}, {{102U}}}},
      {{{20U}, 0U, {{201U}}}, {{10U}, 0U, {{202U}}}}, {}};
  EditorPair pair(ordered);
  expect(pair.select(otgy::core::ChartColumn::Player, 16U).changed &&
             pair.select(otgy::core::ChartColumn::Npc, 0U).changed &&
             pair.direct.selections().size() == 2U &&
             pair.direct.selections()[0].first_row == 0U &&
             pair.direct.selections()[0].drill_id.value == 20U &&
             pair.direct.selections()[1].first_row == 16U &&
             pair.direct.selections()[1].drill_id.value == 10U,
         "selection order did not prioritize source row over identity and click order");
  pair.session(&otgy::core::ChartEditor::copy_selection,
               otgy_chart_editor_copy_selection);
  auto preview = pair.preview(100U);
  expect(otgy_chart_paste_preview_get_code(preview.get()) == OTGY_CHART_EDIT_SUCCESS,
         "incomplete selected draft could not be previewed");
  expect(pair.direct.undo_count() == 0U &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), ordered),
         "selection or copy edited an incomplete draft");
  pair.session(&otgy::core::ChartEditor::clear_selection,
               otgy_chart_editor_clear_selection);
  expect(pair.select(otgy::core::ChartColumn::Npc, 0U).changed &&
             pair.select(otgy::core::ChartColumn::Player, 16U).changed &&
             pair.direct.selections().size() == 2U &&
             pair.direct.selections()[0].first_row == 0U &&
             pair.direct.selections()[0].drill_id.value == 20U &&
             pair.direct.selections()[1].first_row == 16U &&
             pair.direct.selections()[1].drill_id.value == 10U,
         "source-row order changed after reverse selection");
  expect(pair.click(otgy::core::ChartColumn::Npc, 0U).changed &&
             pair.direct.selections().size() == 1U &&
             pair.direct.selections().front().drill_id.value == 10U,
         "deleted selected move remained in the selection");
  expect(pair.history(true).changed && pair.direct.selections().size() == 1U &&
             pair.direct.selections().front().drill_id.value == 10U,
         "undo restored a historical selected move");
  expect(pair.history(false).changed && pair.direct.selections().size() == 1U,
         "redo lost the surviving selection");

  EditorPair exhausted(otgy::core::ChartDraft{
      {{{1U}, 0U, otgy::core::ChartRole::Npc,
        otgy::core::ChartRoleEventType::BlockStart, {2U}, {UINT64_MAX}}},
      {}, {}, {{{2U}, {UINT64_MAX}, {{1U}}, {}}},
      {{{UINT64_MAX}, 0U, {{2U}}}}, {}});
  exhausted.select(otgy::core::ChartColumn::Npc, 0U);
  exhausted.session(&otgy::core::ChartEditor::copy_selection,
                    otgy_chart_editor_copy_selection);
  const auto exhausted_before = exhausted.direct.draft();
  auto exhausted_preview = exhausted.preview(100U);
  expect(otgy_chart_paste_preview_get_code(exhausted_preview.get()) ==
             OTGY_CHART_EDIT_ID_EXHAUSTED &&
             exhausted.paste(100U).code == otgy::core::ChartEditCode::IdExhausted &&
             exhausted.direct.undo_count() == 0U &&
             otgy::core::chart_drafts_equal(exhausted.direct.draft(),
                                             exhausted_before),
         "identifier exhaustion changed draft or disagreed with preview");

  auto generated = build_two_complete_moves();
  expect(static_cast<bool>(generated.click(otgy::core::ChartColumn::Showtime, 52U)),
         "Showtime selection fixture generation failed");
  EditorPair showtime(generated.draft());
  showtime.select(otgy::core::ChartColumn::Npc, 0U);
  const auto start_row = generated.draft().showtime_start_events().front().row;
  const auto unavailable = showtime.select(otgy::core::ChartColumn::Showtime,
                                           start_row);
  expect(!unavailable.changed && showtime.direct.selections().size() == 1U,
         "SHOW-C changed existing selection");
  for (const auto& move : generated.draft().showtime_move_events()) {
    showtime.session(&otgy::core::ChartEditor::clear_selection,
                     otgy_chart_editor_clear_selection);
    expect(showtime.select(otgy::core::ChartColumn::Showtime, move.row).changed &&
               showtime.direct.selections().size() == 1U &&
               showtime.direct.selections().front().drill_id.value ==
                   move.dance_move_drill_id.value,
           "SHOW-D did not resolve its referenced dance move");
  }
  showtime.session(&otgy::core::ChartEditor::copy_selection,
                   otgy_chart_editor_copy_selection);
  const auto before_paste = showtime.direct.draft();
  expect(showtime.paste(300U).changed &&
             showtime.direct.draft().showtimes().size() ==
                 before_paste.showtimes().size() &&
             showtime.direct.draft().showtime_start_events().size() ==
                 before_paste.showtime_start_events().size() &&
             showtime.direct.draft().showtime_move_events().size() ==
                 before_paste.showtime_move_events().size(),
         "copying SHOW-D copied Showtime relations into the fragment");
}

void test_deferred_cut_consistency() {
  const auto original = build_two_complete_moves().draft();
  EditorPair pair(original);
  pair.select(otgy::core::ChartColumn::Npc, 0U);
  pair.session(&otgy::core::ChartEditor::cut_selection,
               otgy_chart_editor_cut_selection);
  otgy::core::ChartEditor control(original);
  static_cast<void>(control.select(otgy::core::ChartColumn::Npc, 0U));
  expect(static_cast<bool>(control.cut_selection()), "control cut preparation failed");
  const auto source_selection = pair.direct.selections();
  expect(pair.direct.undo_count() == 0U && pair.direct.has_pending_cut() &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), original),
         "cut preparation modified its source");
  auto occupied = pair.preview(16U);
  expect(otgy_chart_paste_preview_get_code(occupied.get()) ==
             OTGY_CHART_EDIT_PASTE_RANGE_OCCUPIED &&
             otgy_chart_paste_preview_get_selection_count(occupied.get()) == 1U &&
             otgy_chart_paste_preview_get_marker_count(occupied.get()) != 0U,
         "occupied cut preview lost its destination geometry");
  const auto refused = pair.paste(16U);
  expect(refused.code == otgy::core::ChartEditCode::PasteRangeOccupied &&
             !refused.changed && pair.direct.undo_count() == 0U &&
             pair.direct.has_pending_cut() &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), original),
         "failed cut placement deleted source or pending state");
  otgy_chart_selections* raw_selection = nullptr;
  otgy_error* error = nullptr;
  expect(otgy_chart_editor_copy_selections(pair.abi.get(), &raw_selection,
                                          &error) == OTGY_STATUS_OK,
         "failed cut selection read-back failed");
  auto selection =
      handle<otgy_chart_selections, otgy_chart_selections_destroy>(raw_selection);
  compare_selections(source_selection, selection.get());

  pair.select(otgy::core::ChartColumn::Npc, 16U);
  pair.session(&otgy::core::ChartEditor::cut_selection,
               otgy_chart_editor_cut_selection);
  static_cast<void>(control.select(otgy::core::ChartColumn::Npc, 16U));
  expect(static_cast<bool>(control.cut_selection()), "control batch cut failed");
  expect(pair.direct.undo_count() == 0U &&
             otgy_chart_editor_clipboard_count(pair.abi.get()) == 2U,
         "batch cut preparation added history");
  auto overlap = pair.preview(1U);
  expect(otgy_chart_paste_preview_get_code(overlap.get()) == OTGY_CHART_EDIT_SUCCESS,
         "cut source overlap was rejected by preview");
  expect(pair.paste(1U).changed && static_cast<bool>(control.paste(1U)) &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), control.draft()) &&
             pair.direct.undo_count() == 1U &&
             !pair.direct.has_pending_cut() &&
             pair.direct.draft().dance_move_drills().size() == 2U &&
             pair.direct.selections()[0].first_row == 1U &&
             pair.direct.selections()[1].first_row == 17U,
         "overlapping batch cut did not atomically replace source");
  for (const auto& event : original.role_events()) {
    expect(std::none_of(pair.direct.draft().role_events().begin(),
                        pair.direct.draft().role_events().end(),
                        [&event](const otgy::core::ChartRoleEvent& item) {
                          return item.id.value == event.id.value;
                        }),
           "cut retained an old source event identity");
  }
  expect(pair.history(true).changed &&
             otgy::core::chart_drafts_equal(pair.direct.draft(), original) &&
             !pair.direct.has_pending_cut() && pair.direct.has_clipboard() &&
             pair.direct.selections().empty(),
         "one undo did not restore source without restoring cut intent");
  expect(pair.history(false).changed && !pair.direct.has_pending_cut() &&
             pair.direct.selections().empty(),
         "redo restored historical selection or cut intent");
  const auto before_copy = pair.direct.draft();
  expect(pair.paste(200U).changed &&
             pair.direct.draft().dance_move_drills().size() == 4U &&
             pair.direct.undo_count() == 2U && !pair.direct.has_pending_cut(),
         "subsequent cut clipboard placement did not copy the snapshot");
  for (const auto& event : before_copy.role_events()) {
    expect(std::any_of(pair.direct.draft().role_events().begin(),
                       pair.direct.draft().role_events().end(),
                       [&event](const otgy::core::ChartRoleEvent& item) {
                         return item.id.value == event.id.value &&
                                item.row == event.row;
                       }),
           "repeat placement removed the previous destination");
  }

  EditorPair changed(original);
  changed.select(otgy::core::ChartColumn::Npc, 0U);
  changed.session(&otgy::core::ChartEditor::cut_selection,
                  otgy_chart_editor_cut_selection);
  expect(changed.click(otgy::core::ChartColumn::Npc, 2U).changed,
         "cut source mutation failed");
  const auto modified = changed.direct.draft();
  auto stale = changed.preview(200U);
  expect(otgy_chart_paste_preview_get_code(stale.get()) ==
             OTGY_CHART_EDIT_CUT_SOURCE_CHANGED &&
             otgy_chart_paste_preview_get_marker_count(stale.get()) != 0U,
         "changed cut source preview did not retain snapshot geometry");
  const auto failed = changed.paste(200U);
  expect(failed.code == otgy::core::ChartEditCode::CutSourceChanged &&
             !failed.changed && changed.direct.undo_count() == 1U &&
             changed.direct.has_pending_cut() &&
             otgy::core::chart_drafts_equal(changed.direct.draft(), modified),
         "changed cut source was deleted or silently refreshed");
  changed.session(&otgy::core::ChartEditor::clear_clipboard,
                  otgy_chart_editor_clear_clipboard);
  expect(!changed.direct.has_pending_cut() &&
             otgy::core::chart_drafts_equal(changed.direct.draft(), modified),
         "canceling cut altered the source");
}

void test_timing_and_coordination() {
  const otgy::core::TimingParameters direct_timing{129.24970691676435,
                                                    -0.090521541950113374};
  const otgy_timing_parameters abi_timing{direct_timing.bpm,
                                          direct_timing.offset_seconds};
  struct Conversion final {
    otgy::core::TimingConversionResult (*direct)(
        otgy::core::TimingParameters, double) noexcept;
    otgy_status (*abi)(otgy_timing_parameters, double, double*, otgy_error**);
    double input;
  };
  const std::array<Conversion, 4> conversions{{
      {otgy::core::seconds_to_beat, otgy_seconds_to_beat, 1.25},
      {otgy::core::beat_to_seconds, otgy_beat_to_seconds, 3.5},
      {otgy::core::seconds_to_sixteenth_row,
       otgy_seconds_to_sixteenth_row, 1.25},
      {otgy::core::sixteenth_row_to_seconds,
       otgy_sixteenth_row_to_seconds, 14.0},
  }};
  for (const auto& conversion : conversions) {
    const auto direct = conversion.direct(direct_timing, conversion.input);
    double abi = 0.0;
    otgy_error* raw_error = nullptr;
    expect(conversion.abi(abi_timing, conversion.input, &abi, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI timing conversion failed");
    auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
    expect(error == nullptr && same_double(abi, *direct.value()),
           "timing conversion differs");
  }
  const auto direct_beat_row = otgy::core::beat_to_sixteenth_row(-2.25);
  double abi_beat_row = 0.0;
  otgy_error* raw_error = nullptr;
  expect(otgy_beat_to_sixteenth_row(-2.25, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_beat_row, *direct_beat_row.value()),
         "beat-to-row differs");
  const auto direct_row_beat = otgy::core::sixteenth_row_to_beat(-9.0);
  expect(otgy_sixteenth_row_to_beat(-9.0, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_beat_row, *direct_row_beat.value()),
         "row-to-beat differs");

  const auto direct_offset =
      otgy::core::adjust_offset_by_beats(direct_timing, -2.0);
  double abi_offset = 0.0;
  expect(otgy_adjust_offset_by_beats(abi_timing, -2.0, &abi_offset,
                                     &raw_error) == OTGY_STATUS_OK &&
             same_double(abi_offset, *direct_offset.value()),
         "Offset beat adjustment differs");

  const auto direct_no_grid =
      otgy::core::seconds_to_beat({0.0, 0.0}, 1.0);
  expect(otgy_seconds_to_beat({0.0, 0.0}, 1.0, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "NoGrid status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_TIMING_CONVERSION,
               static_cast<std::uint32_t>(direct_no_grid.error()),
               otgy::core::timing_conversion_error_message(
                   direct_no_grid.error()));

  const auto direct_grid =
      otgy::core::generate_timing_grid({100.0, 0.0}, 0.0, 9.6);
  otgy_timing_grid* raw_grid = nullptr;
  expect(otgy_generate_timing_grid({100.0, 0.0}, 0.0, 9.6, &raw_grid,
                                   &raw_error) == OTGY_STATUS_OK,
         "C ABI timing grid failed");
  auto grid = handle<otgy_timing_grid, otgy_timing_grid_destroy>(raw_grid);
  expect(otgy_timing_grid_get_line_count(grid.get()) ==
             direct_grid.grid()->lines().size(),
         "timing grid count differs");
  const auto* abi_lines = otgy_timing_grid_get_lines(grid.get());
  for (std::size_t index = 0U; index < direct_grid.grid()->lines().size();
       ++index) {
    const auto& direct = direct_grid.grid()->lines()[index];
    expect(abi_lines[index].sixteenth_row == direct.sixteenth_row &&
               same_double(abi_lines[index].seconds, direct.seconds) &&
               abi_lines[index].kind ==
                   static_cast<std::uint32_t>(direct.kind),
           "timing grid line differs");
  }

  const auto direct_range = otgy::core::calculate_chart_timeline_range(
      empty_chart(), direct_timing, 7.25);
  otgy_chart* raw_range_chart = nullptr;
  expect(otgy_chart_create_empty(&raw_range_chart, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI range chart create failed");
  auto range_chart =
      handle<otgy_chart, otgy_chart_destroy>(raw_range_chart);
  otgy_timeline_range abi_range{};
  expect(otgy_chart_calculate_timeline_range(
             range_chart.get(), abi_timing, 1U, 7.25, &abi_range,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI timeline range failed");
  expect(same_double(abi_range.begin_seconds,
                     direct_range.range()->begin_seconds()) &&
             same_double(abi_range.end_seconds,
                         direct_range.range()->end_seconds()) &&
             same_double(abi_range.end_sixteenth_row,
                         direct_range.range()->end_sixteenth_row()) &&
             abi_range.uses_zero_bpm_projection ==
                 static_cast<std::uint8_t>(
                     direct_range.range()->uses_zero_bpm_projection()),
         "timeline range differs");

  const auto direct_navigation = otgy::core::calculate_timeline_navigation(
      *direct_range.range(), 15.25, 6.5);
  otgy_timeline_navigation abi_navigation{};
  expect(otgy_calculate_timeline_navigation(
             abi_range, 15.25, 6.5, &abi_navigation, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI navigation failed");
  expect(same_double(abi_navigation.to_begin_row,
                     direct_navigation.navigation()->to_begin_row()) &&
             same_double(abi_navigation.page_up_row,
                         direct_navigation.navigation()->page_up_row()) &&
             same_double(abi_navigation.page_down_row,
                         direct_navigation.navigation()->page_down_row()) &&
             same_double(abi_navigation.to_end_row,
                         direct_navigation.navigation()->to_end_row()) &&
             abi_navigation.can_page_down ==
                 static_cast<std::uint8_t>(
                     direct_navigation.navigation()->can_page_down()),
         "timeline navigation differs");

  const auto direct_start = otgy::core::start_playback(10.0, -0.25);
  otgy_playback_position abi_position{};
  expect(otgy_start_playback(1U, 10.0, -0.25, &abi_position, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI playback start failed");
  expect(abi_position.phase ==
                 static_cast<std::uint32_t>(direct_start.position()->phase()) &&
             same_double(abi_position.timeline_seconds,
                         direct_start.position()->timeline_seconds()),
         "playback start differs");
  const auto direct_advance =
      otgy::core::advance_playback(*direct_start.position(), 0.5);
  expect(otgy_advance_playback(abi_position, 0.5, &abi_position, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_position.timeline_seconds,
                         direct_advance.position()->timeline_seconds()),
         "playback advance differs");
  const auto direct_pause =
      otgy::core::pause_playback(*direct_advance.position());
  expect(otgy_pause_playback(abi_position, &abi_position, &raw_error) ==
             OTGY_STATUS_OK &&
             abi_position.phase ==
                 static_cast<std::uint32_t>(direct_pause.position()->phase()),
         "playback pause differs");

  for (std::uint32_t mode = 0U; mode <= 4U; ++mode) {
    for (std::uint32_t operation = 0U; operation <= 8U; ++operation) {
      const auto direct = otgy::core::coordinate_application_operation(
          static_cast<otgy::core::ApplicationOperationMode>(mode),
          static_cast<otgy::core::ApplicationOperationClass>(operation));
      std::uint32_t abi = 99U;
      expect(otgy_coordinate_application_operation(mode, operation, &abi,
                                                   &raw_error) ==
                 OTGY_STATUS_OK &&
                 abi == static_cast<std::uint32_t>(direct),
             "operation gate differs");
    }
  }
  otgy_timing_grid_destroy(nullptr);
}

Handle<otgy_chart, otgy_chart_destroy> test_chart_edit_and_persistence() {
  auto direct_draft = empty_chart();
  otgy::core::ChartEditor direct_editor(direct_draft);
  otgy_chart* raw_chart = nullptr;
  otgy_error* raw_error = nullptr;
  expect(otgy_chart_create_empty(&raw_chart, &raw_error) == OTGY_STATUS_OK,
         "C ABI empty chart failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  otgy_chart_editor* raw_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI editor create failed");
  auto editor =
      handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

  const auto direct_first =
      direct_editor.click(otgy::core::ChartColumn::Npc, 0U);
  otgy_chart_edit_result abi_first{};
  expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 0U,
                                 &abi_first, &raw_error) == OTGY_STATUS_OK,
         "C ABI first edit failed");
  compare_edit(direct_first, abi_first);
  const auto direct_second =
      direct_editor.click(otgy::core::ChartColumn::Npc, 4U);
  otgy_chart_edit_result abi_second{};
  expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 4U,
                                 &abi_second, &raw_error) == OTGY_STATUS_OK,
         "C ABI second edit failed");
  compare_edit(direct_second, abi_second);
  expect(otgy_chart_editor_can_undo(editor.get()) == direct_editor.can_undo() &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 direct_editor.undo_count(),
         "editor history differs");

  auto direct_undo = direct_editor.undo();
  otgy_chart_edit_result abi_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI undo failed");
  compare_edit(direct_undo, abi_undo);
  auto direct_redo = direct_editor.redo();
  otgy_chart_edit_result abi_redo{};
  expect(otgy_chart_editor_redo(editor.get(), &abi_redo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI redo failed");
  compare_edit(direct_redo, abi_redo);
  expect(otgy_chart_editor_can_redo(editor.get()) == direct_editor.can_redo(),
         "redo state differs");
  expect(otgy_chart_editor_get_redo_count(editor.get()) ==
             direct_editor.redo_count(),
         "redo history count differs");

  const auto direct_select =
      direct_editor.select(otgy::core::ChartColumn::Npc, 4U);
  otgy_chart_session_result abi_session{};
  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 4U,
                                  &abi_session, &raw_error) == OTGY_STATUS_OK,
         "C ABI select failed");
  compare_session(direct_select, abi_session);
  compare_session_state(direct_editor, editor.get());
  expect(direct_editor.selections().size() == 1U &&
             direct_editor.selections().front().first_row == 0U &&
             direct_editor.selections().front().last_row == 4U,
         "C ABI selection span differs");
  expect(otgy_chart_editor_get_undo_count(editor.get()) ==
                 direct_editor.undo_count() &&
             otgy_chart_editor_get_redo_count(editor.get()) ==
                 direct_editor.redo_count(),
         "selection changed chart history");

  const auto direct_deselect =
      direct_editor.select(otgy::core::ChartColumn::Npc, 100U);
  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 100U,
                                  &abi_session, &raw_error) == OTGY_STATUS_OK,
         "C ABI empty row select failed");
  compare_session(direct_deselect, abi_session);
  expect(abi_session.changed == 0U &&
             otgy_chart_editor_selection_count(editor.get()) == 1U,
         "empty row changed the C ABI selection");
  compare_session_state(direct_editor, editor.get());

  const auto direct_clear_selection = direct_editor.clear_selection();
  expect(otgy_chart_editor_clear_selection(editor.get(), &abi_session,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI clear selection failed");
  compare_session(direct_clear_selection, abi_session);
  expect(abi_session.changed == 1U &&
             otgy_chart_editor_selection_count(editor.get()) == 0U,
         "clearing the C ABI selection failed");
  compare_session_state(direct_editor, editor.get());

  const auto direct_reselect =
      direct_editor.select(otgy::core::ChartColumn::Npc, 4U);
  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 4U,
                                  &abi_session, &raw_error) == OTGY_STATUS_OK,
         "C ABI select failed");
  compare_session(direct_reselect, abi_session);
  auto* unavailable_selections = reinterpret_cast<otgy_chart_selections*>(1);
  expect(otgy_chart_editor_copy_selections(nullptr, &unavailable_selections,
                                          &raw_error) ==
                 OTGY_STATUS_INVALID_ARGUMENT &&
             unavailable_selections == nullptr,
         "null editor exposed C ABI selections");
  auto session_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(session_error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
  session_error.reset();

  abi_session = {1U, 1U};
  expect(otgy_chart_editor_clear_selection(nullptr, &abi_session, &raw_error) ==
                 OTGY_STATUS_INVALID_ARGUMENT &&
             abi_session.code == 0U && abi_session.changed == 0U,
         "null editor was not rejected");
  session_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(session_error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
  session_error.reset();

  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 4U,
                                  nullptr, &raw_error) ==
             OTGY_STATUS_INVALID_ARGUMENT,
         "null session result was not rejected");
  session_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(session_error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
  session_error.reset();

  expect(otgy_chart_editor_has_clipboard(editor.get()) == 0U &&
             otgy_chart_editor_has_clipboard(nullptr) == 0U,
         "clipboard state differs before copy");

  const auto direct_copy = direct_editor.copy_selection();
  expect(otgy_chart_editor_copy_selection(editor.get(), &abi_session,
                                          &raw_error) == OTGY_STATUS_OK,
         "C ABI copy selection failed");
  compare_session(direct_copy, abi_session);
  expect(abi_session.changed == 1U &&
             otgy_chart_editor_has_clipboard(editor.get()) == 1U,
         "C ABI copy did not fill the clipboard");
  expect(otgy_chart_editor_get_undo_count(editor.get()) ==
             direct_editor.undo_count(),
         "copy changed chart history");

  const auto history_before_paste = direct_editor.undo_count();
  const auto direct_paste = direct_editor.paste(200U);
  otgy_chart_edit_result abi_paste{};
  expect(otgy_chart_editor_paste(editor.get(), 200U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI paste failed");
  compare_edit(direct_paste, abi_paste);
  expect(abi_paste.code == OTGY_CHART_EDIT_SUCCESS &&
             abi_paste.changed == 1U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_paste + 1U,
         "successful C ABI paste did not add one history entry");

  compare_session_state(direct_editor, editor.get());
  expect(direct_editor.selections().size() == 1U &&
             direct_editor.selections().front().first_row == 200U &&
             direct_editor.selections().front().last_row == 204U,
         "C ABI pasted selection differs");

  abi_paste = {7U, 7U, 7U};
  const auto direct_occupied = direct_editor.paste(200U);
  expect(otgy_chart_editor_paste(editor.get(), 200U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI occupied paste failed");
  compare_edit(direct_occupied, abi_paste);
  expect(abi_paste.code == OTGY_CHART_EDIT_PASTE_RANGE_OCCUPIED &&
             abi_paste.changed == 0U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_paste + 1U,
         "occupied C ABI paste was not reported");

  abi_paste = {7U, 7U, 7U};
  expect(otgy_chart_editor_paste(nullptr, 0U, &abi_paste, &raw_error) ==
                 OTGY_STATUS_INVALID_ARGUMENT &&
             abi_paste.code == 0U && abi_paste.changed == 0U &&
             abi_paste.primary_event_id == 0U,
         "null editor was not rejected by paste");
  session_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(session_error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
  session_error.reset();

  const auto history_before_cut = otgy_chart_editor_get_undo_count(editor.get());
  const auto before_cut = direct_editor.draft();
  const auto direct_cut = direct_editor.cut_selection();
  otgy_chart_session_result abi_cut{7U, 7U};
  expect(otgy_chart_editor_cut_selection(editor.get(), &abi_cut, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI cut preparation failed");
  compare_session(direct_cut, abi_cut);
  expect(abi_cut.code == OTGY_CHART_EDIT_SUCCESS && abi_cut.changed == 1U &&
             otgy_chart_editor_get_undo_count(editor.get()) == history_before_cut &&
             otgy::core::chart_drafts_equal(direct_editor.draft(), before_cut) &&
             otgy_chart_editor_cut_pending(editor.get()) == 1U &&
             otgy_chart_editor_clipboard_count(editor.get()) == 1U,
         "cut preparation changed the draft or history");
  compare_session_state(direct_editor, editor.get());

  const auto direct_cut_paste = direct_editor.paste(300U);
  expect(otgy_chart_editor_paste(editor.get(), 300U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI cut placement failed");
  compare_edit(direct_cut_paste, abi_paste);
  expect(abi_paste.changed == 1U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_cut + 1U &&
             otgy_chart_editor_cut_pending(editor.get()) == 0U,
         "cut placement did not add exactly one history entry");
  compare_session_state(direct_editor, editor.get());

  const auto direct_clear_after_cut = direct_editor.clear_selection();
  expect(otgy_chart_editor_clear_selection(editor.get(), &abi_session,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI post-cut selection clear failed");
  compare_session(direct_clear_after_cut, abi_session);
  const auto direct_cut_again = direct_editor.cut_selection();
  abi_cut = {7U, 7U};
  expect(otgy_chart_editor_cut_selection(editor.get(), &abi_cut, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI cut without a selection failed");
  compare_session(direct_cut_again, abi_cut);
  expect(abi_cut.code == OTGY_CHART_EDIT_SELECTION_UNAVAILABLE &&
             abi_cut.changed == 0U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_cut + 1U,
         "cut without a C ABI selection was not reported");
  compare_session_state(direct_editor, editor.get());

  const auto history_before_clear_clipboard =
      otgy_chart_editor_get_undo_count(editor.get());
  const auto direct_clear_clipboard = direct_editor.clear_clipboard();
  otgy_chart_session_result abi_clear_clipboard{};
  expect(otgy_chart_editor_clear_clipboard(editor.get(), &abi_clear_clipboard,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI clipboard clear failed");
  compare_session(direct_clear_clipboard, abi_clear_clipboard);
  expect(abi_clear_clipboard.code == OTGY_CHART_EDIT_SUCCESS &&
             abi_clear_clipboard.changed == 1U &&
             otgy_chart_editor_has_clipboard(editor.get()) == 0U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_clear_clipboard,
         "C ABI clipboard clear was not reported");

  const auto direct_empty_clipboard = direct_editor.paste(400U);
  abi_paste = {7U, 7U, 7U};
  expect(otgy_chart_editor_paste(editor.get(), 400U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI paste after a clipboard clear failed");
  compare_edit(direct_empty_clipboard, abi_paste);
  expect(abi_paste.code == OTGY_CHART_EDIT_CLIPBOARD_EMPTY &&
             abi_paste.changed == 0U &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 history_before_clear_clipboard,
         "empty C ABI clipboard was not reported");

  otgy_chart_session_result abi_null_clear{};
  expect(otgy_chart_editor_clear_clipboard(nullptr, &abi_null_clear,
                                           &raw_error) ==
                 OTGY_STATUS_INVALID_ARGUMENT &&
             abi_null_clear.code == 0U && abi_null_clear.changed == 0U,
         "null editor was not rejected by the clipboard clear");
  session_error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(session_error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_NULL_ARGUMENT,
               "required C ABI argument is null");
  session_error.reset();

  otgy_chart* raw_edited = nullptr;
  expect(otgy_chart_editor_copy_chart(editor.get(), &raw_edited, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI editor chart copy failed");
  auto edited = handle<otgy_chart, otgy_chart_destroy>(raw_edited);
  const auto direct_markers =
      otgy::core::project_chart_markers(direct_editor.draft());
  otgy_chart_markers* raw_markers = nullptr;
  expect(otgy_chart_project_markers(edited.get(), &raw_markers, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI marker projection failed");
  auto markers =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_markers);
  compare_markers(*direct_markers.markers(), markers.get());

  const auto direct_ordinal = otgy::core::chart_note_ordinal(
      direct_editor.draft(), direct_second.primary_event_id);
  std::uint64_t abi_ordinal = 0U;
  expect(otgy_chart_note_ordinal(edited.get(),
                                 direct_second.primary_event_id.value,
                                 &abi_ordinal, &raw_error) == OTGY_STATUS_OK &&
             abi_ordinal == direct_ordinal.ordinal,
         "note ordinal differs");

  const double bpm = 100.00000000000003;
  const double offset = -0.12345678901234566;
  const auto direct_formal =
      otgy::core::encode_formal_draft(direct_editor.draft(), bpm, offset);
  otgy_bytes* raw_formal = nullptr;
  expect(otgy_project_encode_formal_draft(edited.get(), bpm, offset,
                                          &raw_formal, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI formal encode failed");
  auto formal = handle<otgy_bytes, otgy_bytes_destroy>(raw_formal);
  const auto formal_view = otgy_bytes_get_view(formal.get());
  expect(formal_view.length == direct_formal.bytes()->size() &&
             std::memcmp(formal_view.data, direct_formal.bytes()->data(),
                         direct_formal.bytes()->size()) == 0,
         "formal project bytes differ");
  otgy_chart* raw_decoded = nullptr;
  double decoded_bpm = 0.0;
  double decoded_offset = 0.0;
  expect(otgy_project_decode_formal_draft(
             formal_view, &raw_decoded, &decoded_bpm, &decoded_offset,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI formal decode failed");
  auto decoded = handle<otgy_chart, otgy_chart_destroy>(raw_decoded);
  expect(otgy_chart_equal(edited.get(), decoded.get()) == 1U &&
             same_double(decoded_bpm, bpm) &&
             same_double(decoded_offset, offset),
         "formal project round-trip differs");

  const auto direct_recovery =
      otgy::core::encode_recovery_snapshot(direct_editor.draft());
  otgy_bytes* raw_recovery = nullptr;
  expect(otgy_project_encode_recovery_snapshot(
             edited.get(), &raw_recovery, &raw_error) == OTGY_STATUS_OK,
         "C ABI recovery encode failed");
  auto recovery = handle<otgy_bytes, otgy_bytes_destroy>(raw_recovery);
  const auto recovery_view = otgy_bytes_get_view(recovery.get());
  expect(recovery_view.length == direct_recovery.bytes()->size() &&
             std::memcmp(recovery_view.data, direct_recovery.bytes()->data(),
                         direct_recovery.bytes()->size()) == 0,
         "recovery bytes differ");
  expect(otgy_project_decode_recovery_snapshot(
             recovery_view, &raw_decoded, &raw_error) == OTGY_STATUS_OK,
         "C ABI recovery decode failed");
  decoded.reset(raw_decoded);
  expect(otgy_chart_equal(edited.get(), decoded.get()) == 1U,
         "recovery chart differs");

  const auto direct_validation =
      otgy::core::validate_finished_chart(direct_editor.draft());
  otgy_chart_validation* raw_validation = nullptr;
  expect(otgy_chart_validate_finished(edited.get(), &raw_validation,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI draft validation failed");
  auto validation =
      handle<otgy_chart_validation, otgy_chart_validation_destroy>(
          raw_validation);
  expect(otgy_chart_validation_is_finished(validation.get()) == 0U &&
             otgy_chart_validation_get_diagnostic_count(validation.get()) ==
                 direct_validation.diagnostics()->size(),
         "draft validation state differs");
  const auto* abi_diagnostics =
      otgy_chart_validation_get_diagnostics(validation.get());
  for (std::size_t index = 0U;
       index < direct_validation.diagnostics()->size(); ++index) {
    const auto& direct = direct_validation.diagnostics()->at(index);
    expect(abi_diagnostics[index].code ==
                   static_cast<std::uint32_t>(direct.code) &&
               abi_diagnostics[index].object_id == direct.object_id &&
               abi_diagnostics[index].related_id == direct.related_id &&
               abi_diagnostics[index].row == direct.row,
           "chart diagnostic differs");
  }
  otgy_finished_chart* unavailable = nullptr;
  expect(otgy_chart_validation_copy_finished(
             validation.get(), &unavailable, &raw_error) ==
             OTGY_STATUS_INVALID_ARGUMENT,
         "unfinished chart exposed a finished snapshot");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_RESULT_NOT_AVAILABLE,
               "requested C ABI result is not available");

  otgy_chart* raw_clone = nullptr;
  expect(otgy_chart_clone(edited.get(), &raw_clone, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI chart clone failed");
  auto clone = handle<otgy_chart, otgy_chart_destroy>(raw_clone);
  expect(otgy_chart_equal(edited.get(), clone.get()) == 1U,
         "chart clone differs");

  otgy_chart_editor* raw_empty_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_empty_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI empty editor create failed");
  auto empty_editor =
      handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_empty_editor);
  otgy::core::ChartEditor direct_empty_editor(empty_chart());
  const auto direct_clear = direct_empty_editor.clear();
  otgy_chart_edit_result abi_clear{};
  expect(otgy_chart_editor_clear(empty_editor.get(), &abi_clear, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI clear failed");
  compare_edit(direct_clear, abi_clear);

  // A Showtime is deleted together with the edit that makes it stale, and the C
  // ABI reports the same results as the core. This runs on its own editor pair
  // so the chart copies compared above stay untouched.
  otgy_chart* raw_stale_chart = nullptr;
  expect(otgy_chart_create_empty(&raw_stale_chart, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI stale chart failed");
  auto stale_chart = handle<otgy_chart, otgy_chart_destroy>(raw_stale_chart);
  otgy_chart_editor* raw_stale_editor = nullptr;
  expect(otgy_chart_editor_create(stale_chart.get(), &raw_stale_editor,
                                  &raw_error) == OTGY_STATUS_OK,
         "C ABI stale editor create failed");
  auto stale_editor =
      handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_stale_editor);
  otgy::core::ChartEditor direct_stale(empty_chart());
  const otgy::core::ChartColumn stale_columns[] = {
      otgy::core::ChartColumn::Npc, otgy::core::ChartColumn::Player,
      otgy::core::ChartColumn::Showtime};
  const std::uint32_t stale_abi_columns[] = {
      OTGY_CHART_COLUMN_NPC, OTGY_CHART_COLUMN_PLAYER,
      OTGY_CHART_COLUMN_SHOWTIME};
  const auto stale_click = [&](std::size_t column, std::uint64_t row) {
    const auto direct = direct_stale.click(stale_columns[column], row);
    otgy_chart_edit_result abi{};
    expect(otgy_chart_editor_click(stale_editor.get(), stale_abi_columns[column],
                                   row, &abi, &raw_error) == OTGY_STATUS_OK,
           "C ABI stale click failed");
    compare_edit(direct, abi);
    return direct;
  };
  stale_click(0U, 0U);
  stale_click(0U, 4U);
  stale_click(0U, 8U);
  stale_click(0U, 8U);
  stale_click(1U, 12U);
  stale_click(1U, 16U);
  const auto stale_completion = stale_click(2U, 32U);
  expect(stale_completion.code == otgy::core::ChartEditCode::Success &&
             direct_stale.draft().showtimes().size() == 1U,
         "stale fixture Showtime was not generated");

  otgy_chart* raw_stale_copy = nullptr;
  expect(otgy_chart_editor_copy_chart(stale_editor.get(), &raw_stale_copy,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI stale chart copy failed");
  auto stale_copy = handle<otgy_chart, otgy_chart_destroy>(raw_stale_copy);
  const auto stale_markers =
      otgy::core::project_chart_markers(direct_stale.draft());
  expect(static_cast<bool>(stale_markers), "direct Showtime projection failed");
  otgy_chart_markers* raw_stale_markers = nullptr;
  expect(otgy_chart_project_markers(stale_copy.get(), &raw_stale_markers,
                                    &raw_error) == OTGY_STATUS_OK,
         "C ABI Showtime projection failed");
  auto stale_projection =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_stale_markers);
  compare_markers(*stale_markers.markers(), stale_projection.get());
  expect(otgy_chart_markers_get_count(stale_projection.get()) == 8U,
         "Showtime did not add its two markers");

  const auto stale_break = stale_click(0U, 0U);
  expect(stale_break.code == otgy::core::ChartEditCode::Success &&
             direct_stale.draft().showtimes().empty() &&
             direct_stale.draft().showtime_start_events().empty() &&
             direct_stale.draft().showtime_move_events().empty(),
         "stale Showtime was not deleted with its edit");
  stale_copy.reset();
  expect(otgy_chart_editor_copy_chart(stale_editor.get(), &raw_stale_copy,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI stale chart copy failed");
  stale_copy = handle<otgy_chart, otgy_chart_destroy>(raw_stale_copy);
  const auto broken_markers =
      otgy::core::project_chart_markers(direct_stale.draft());
  expect(static_cast<bool>(broken_markers), "direct broken projection failed");
  expect(otgy_chart_project_markers(stale_copy.get(), &raw_stale_markers,
                                    &raw_error) == OTGY_STATUS_OK,
         "C ABI broken projection failed");
  stale_projection =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_stale_markers);
  compare_markers(*broken_markers.markers(), stale_projection.get());
  expect(otgy_chart_markers_get_count(stale_projection.get()) == 0U,
         "NPC deletion left linked player actions or stale Showtime markers");

  const auto direct_reset = direct_stale.clear();
  otgy_chart_edit_result abi_reset{};
  expect(otgy_chart_editor_clear(stale_editor.get(), &abi_reset, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI group fixture reset failed");
  compare_edit(direct_reset, abi_reset);
  const auto group_click = [&](std::size_t column, std::uint64_t row) {
    const auto result = stale_click(column, row);
    expect(result.changed, "group fixture click did not change the chart");
  };
  for (const auto row : {2U, 3U, 4U, 5U}) {
    group_click(0U, row);
  }
  for (const auto row : {6U, 7U}) {
    group_click(1U, row);
  }
  for (const auto row : {10U, 11U, 12U, 12U}) {
    group_click(0U, row);
  }
  for (const auto row : {13U, 14U}) {
    group_click(1U, row);
  }
  group_click(2U, 16U);
  const auto before_group_delete = direct_stale.draft();
  group_click(0U, 2U);
  expect(direct_stale.draft().drill_groups().size() == 1U &&
             direct_stale.draft().dance_move_drills().front()
                     .drill_group_ids.size() == 1U,
         "eventless group or its drill reference survived deletion");
  const auto after_group_delete = direct_stale.draft();
  const auto compare_group_chart = [&] {
    otgy_chart* raw_copy = nullptr;
    expect(otgy_chart_editor_copy_chart(stale_editor.get(), &raw_copy,
                                        &raw_error) == OTGY_STATUS_OK,
           "C ABI group chart copy failed");
    auto copy = handle<otgy_chart, otgy_chart_destroy>(raw_copy);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(copy.get(), &raw_semantics, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI group semantics failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct_stale.draft(), semantics.get());
  };
  compare_group_chart();
  const auto direct_group_undo = direct_stale.undo();
  otgy_chart_edit_result abi_group_undo{};
  expect(otgy_chart_editor_undo(stale_editor.get(), &abi_group_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI group undo failed");
  compare_edit(direct_group_undo, abi_group_undo);
  expect(otgy::core::chart_drafts_equal(direct_stale.draft(), before_group_delete),
         "group deletion undo did not restore the draft");
  compare_group_chart();
  const auto direct_group_redo = direct_stale.redo();
  otgy_chart_edit_result abi_group_redo{};
  expect(otgy_chart_editor_redo(stale_editor.get(), &abi_group_redo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI group redo failed");
  compare_edit(direct_group_redo, abi_group_redo);
  expect(otgy::core::chart_drafts_equal(direct_stale.draft(), after_group_delete),
         "group deletion redo did not restore the draft");
  compare_group_chart();
  group_click(2U, 16U);
  expect(direct_stale.draft().showtime_move_events().size() == 1U &&
             direct_stale.draft().showtime_move_events().front().row == 17U &&
             direct_stale.draft().showtime_move_events().front().show_n_count ==
                 1U,
         "remaining group did not regenerate Showtime");
  compare_group_chart();
  return edited;
}

void test_split_merge_consistency() {
  otgy::core::ChartEditor direct(empty_chart());
  otgy_error* raw_error = nullptr;
  otgy_chart* raw_chart = nullptr;
  expect(otgy_chart_create_empty(&raw_chart, &raw_error) == OTGY_STATUS_OK,
         "C ABI split fixture chart failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  otgy_chart_editor* raw_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI split fixture editor failed");
  auto editor = handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

  const otgy::core::ChartColumn columns[] = {
      otgy::core::ChartColumn::Npc, otgy::core::ChartColumn::Player,
      otgy::core::ChartColumn::Showtime};
  const std::uint32_t abi_columns[] = {
      OTGY_CHART_COLUMN_NPC, OTGY_CHART_COLUMN_PLAYER,
      OTGY_CHART_COLUMN_SHOWTIME};
  const auto click = [&](std::size_t column, std::uint64_t row) {
    const auto direct_result = direct.click(columns[column], row);
    otgy_chart_edit_result abi_result{};
    expect(otgy_chart_editor_click(editor.get(), abi_columns[column], row,
                                   &abi_result, &raw_error) == OTGY_STATUS_OK,
           "C ABI split fixture click failed");
    compare_edit(direct_result, abi_result);
    return direct_result;
  };
  const auto compare_chart = [&]() {
    otgy_chart* raw_copy = nullptr;
    expect(otgy_chart_editor_copy_chart(editor.get(), &raw_copy, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI split fixture chart copy failed");
    auto copy = handle<otgy_chart, otgy_chart_destroy>(raw_copy);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(copy.get(), &raw_semantics, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI split fixture semantics failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct.draft(), semantics.get());
  };

  for (const auto row : {2U, 3U, 4U, 5U}) {
    expect(static_cast<bool>(click(0U, row)), "split fixture NPC click failed");
  }
  for (const auto row : {6U, 7U}) {
    expect(static_cast<bool>(click(1U, row)), "split fixture player click failed");
  }
  for (const auto row : {10U, 11U, 12U, 12U}) {
    expect(static_cast<bool>(click(0U, row)), "split fixture NPC click failed");
  }
  for (const auto row : {13U, 14U}) {
    expect(static_cast<bool>(click(1U, row)), "split fixture player click failed");
  }
  expect(static_cast<bool>(click(2U, 16U)),
         "split fixture Showtime generation failed");
  expect(static_cast<bool>(click(0U, 5U)), "dance move split failed");
  expect(direct.draft().dance_move_drills().size() == 2U &&
             direct.draft().showtimes().empty(),
         "dance move split topology differs");
  compare_chart();

  const auto split = direct.draft();
  const auto direct_undo = direct.undo();
  otgy_chart_edit_result abi_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI dance move split undo failed");
  compare_edit(direct_undo, abi_undo);
  compare_chart();
  const auto direct_redo = direct.redo();
  otgy_chart_edit_result abi_redo{};
  expect(otgy_chart_editor_redo(editor.get(), &abi_redo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI dance move split redo failed");
  compare_edit(direct_redo, abi_redo);
  expect(otgy::core::chart_drafts_equal(direct.draft(), split),
         "dance move split redo changed the draft");
  compare_chart();

  expect(static_cast<bool>(click(2U, 16U)),
         "split Showtime regeneration failed");
  expect(direct.draft().showtime_move_events().size() == 2U,
         "split Showtime did not contain two moves");
  compare_chart();

  expect(static_cast<bool>(click(0U, 5U)), "dance move merge failed");
  expect(direct.draft().dance_move_drills().size() == 1U &&
             direct.draft().showtimes().empty(),
         "dance move merge topology differs");
  compare_chart();
  expect(static_cast<bool>(click(2U, 16U)),
         "merged Showtime regeneration failed");
  expect(direct.draft().showtime_move_events().size() == 1U &&
             direct.draft().showtime_move_events().front().show_n_count == 3U,
         "merged Showtime did not contain one complete move");
  compare_chart();

  const auto direct_reset = direct.clear();
  otgy_chart_edit_result abi_reset{};
  expect(otgy_chart_editor_clear(editor.get(), &abi_reset, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI nonterminal NPC-S fixture reset failed");
  compare_edit(direct_reset, abi_reset);
  for (const auto row : {2U, 3U, 4U, 5U}) {
    expect(static_cast<bool>(click(0U, row)), "first NPC group failed");
  }
  for (const auto row : {6U, 7U}) {
    expect(static_cast<bool>(click(1U, row)), "first player group failed");
  }
  for (const auto row : {10U, 11U, 12U}) {
    expect(static_cast<bool>(click(0U, row)), "second NPC group failed");
  }
  for (const auto row : {13U, 14U}) {
    expect(static_cast<bool>(click(1U, row)), "second player group failed");
  }
  for (const auto row : {18U, 19U, 20U}) {
    expect(static_cast<bool>(click(0U, row)), "third NPC group failed");
  }
  for (const auto row : {21U, 22U}) {
    expect(static_cast<bool>(click(1U, row)), "third player group failed");
  }
  expect(static_cast<bool>(click(0U, 4U)),
         "first boundary conversion failed");
  expect(static_cast<bool>(click(0U, 5U)),
         "later boundary conversion failed");
  expect(static_cast<bool>(click(0U, 4U)),
         "first boundary deletion and merge failed");
  expect(direct.draft().dance_move_drills().size() == 1U,
         "multi-boundary merge did not retain one dance move");
  compare_chart();
  const auto before_nonterminal_delete = direct.draft();
  const auto nonterminal_delete = click(0U, 5U);
  expect(nonterminal_delete.code == otgy::core::ChartEditCode::Success &&
             nonterminal_delete.changed,
         "nonterminal NPC-S deletion failed");
  expect(std::none_of(direct.draft().role_events().begin(),
                      direct.draft().role_events().end(),
                      [](const otgy::core::ChartRoleEvent& event) {
                        return event.row == 5U &&
                               event.type ==
                                   otgy::core::ChartRoleEventType::MoveEnd;
                      }),
         "nonterminal NPC-S survived its deletion");
  compare_chart();
  const auto direct_nonterminal_undo = direct.undo();
  otgy_chart_edit_result abi_nonterminal_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_nonterminal_undo,
                                &raw_error) == OTGY_STATUS_OK,
         "C ABI nonterminal NPC-S undo failed");
  compare_edit(direct_nonterminal_undo, abi_nonterminal_undo);
  expect(otgy::core::chart_drafts_equal(direct.draft(),
                                        before_nonterminal_delete),
         "nonterminal NPC-S undo did not restore its event");
  compare_chart();
  const auto direct_nonterminal_redo = direct.redo();
  otgy_chart_edit_result abi_nonterminal_redo{};
  expect(otgy_chart_editor_redo(editor.get(), &abi_nonterminal_redo,
                                &raw_error) == OTGY_STATUS_OK,
         "C ABI nonterminal NPC-S redo failed");
  compare_edit(direct_nonterminal_redo, abi_nonterminal_redo);
  compare_chart();
}

void test_cut_showtime_consistency() {
  auto generated_editor = build_two_complete_moves();
  expect(static_cast<bool>(generated_editor.click(
             otgy::core::ChartColumn::Showtime, 52U)),
         "Showtime cut fixture generation failed");
  const auto generated = generated_editor.draft();

  otgy::core::ChartEditor direct(generated);
  const auto encoded = otgy::core::encode_recovery_snapshot(generated);
  expect(static_cast<bool>(encoded), "Showtime cut fixture encode failed");
  otgy_error* raw_error = nullptr;
  otgy_chart* raw_chart = nullptr;
  const otgy_byte_view bytes{encoded.bytes()->data(),
                             static_cast<std::uint64_t>(encoded.bytes()->size())};
  expect(otgy_project_decode_recovery_snapshot(bytes, &raw_chart, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Showtime cut fixture decode failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  otgy_chart_editor* raw_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Showtime cut editor create failed");
  auto editor = handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

  const auto compare_chart = [&]() {
    otgy_chart* raw_copy = nullptr;
    expect(otgy_chart_editor_copy_chart(editor.get(), &raw_copy, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI Showtime cut chart copy failed");
    auto copy = handle<otgy_chart, otgy_chart_destroy>(raw_copy);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(copy.get(), &raw_semantics, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI Showtime cut semantics failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct.draft(), semantics.get());
  };

  const auto direct_select = direct.select(otgy::core::ChartColumn::Npc, 0U);
  otgy_chart_session_result abi_select{};
  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 0U,
                                  &abi_select, &raw_error) == OTGY_STATUS_OK,
         "C ABI Showtime move select failed");
  compare_session(direct_select, abi_select);

  const auto history_before_cut = direct.undo_count();
  const auto direct_cut = direct.cut_selection();
  otgy_chart_session_result abi_cut{};
  expect(otgy_chart_editor_cut_selection(editor.get(), &abi_cut, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Showtime move cut preparation failed");
  compare_session(direct_cut, abi_cut);
  expect(direct_cut.changed && direct.undo_count() == history_before_cut &&
             otgy::core::chart_drafts_equal(direct.draft(), generated) &&
             direct.has_pending_cut(),
         "cut preparation removed the source or its Showtime");
  compare_session_state(direct, editor.get());
  compare_chart();

  const auto direct_paste = direct.paste(200U);
  otgy_chart_edit_result abi_paste{};
  expect(otgy_chart_editor_paste(editor.get(), 200U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Showtime cut placement failed");
  compare_edit(direct_paste, abi_paste);
  expect(direct_paste.changed && direct.undo_count() == history_before_cut + 1U &&
             direct.draft().dance_move_drills().size() == 2U &&
             direct.draft().showtimes().empty() &&
             direct.draft().showtime_start_events().empty() &&
             direct.draft().showtime_move_events().empty() &&
             !direct.has_pending_cut(),
         "cut placement did not atomically remove the referenced Showtime");
  compare_session_state(direct, editor.get());
  compare_chart();

  const auto direct_undo = direct.undo();
  otgy_chart_edit_result abi_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI Showtime cut undo failed");
  compare_edit(direct_undo, abi_undo);
  expect(otgy::core::chart_drafts_equal(direct.draft(), generated) &&
             !direct.has_pending_cut() && direct.has_clipboard(),
         "one undo did not restore the cut move and Showtime without cut intent");
  compare_session_state(direct, editor.get());
  expect(otgy_chart_editor_get_redo_count(editor.get()) == direct.redo_count(),
         "Showtime cut redo history differs");
  compare_chart();
}

void test_stale_showtime_consistency() {
  auto generated_editor = build_two_complete_moves();
  expect(static_cast<bool>(generated_editor.click(
             otgy::core::ChartColumn::Showtime, 52U)),
         "Showtime fixture generation failed");
  const auto generated = generated_editor.draft();
  expect(generated.dance_move_drills().size() == 2U &&
             generated.dance_move_drills()[0].move_n_count == 2U &&
             generated.dance_move_drills()[1].move_n_count == 7U &&
             generated.showtime_move_events().size() == 2U &&
             generated.showtimes().size() == 1U,
         "Showtime fixture is incomplete");

  const auto check_pruning = [&](otgy::core::ChartDraft malformed,
                                 std::string_view context) {
    otgy::core::ChartEditor direct(malformed);
    const auto encoded = otgy::core::encode_recovery_snapshot(malformed);
    expect(static_cast<bool>(encoded), "malformed fixture encode failed");

    otgy_error* raw_error = nullptr;
    otgy_chart* raw_chart = nullptr;
    const otgy_byte_view bytes{encoded.bytes()->data(),
                               static_cast<std::uint64_t>(encoded.bytes()->size())};
    expect(otgy_project_decode_recovery_snapshot(bytes, &raw_chart, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI malformed fixture decode failed");
    auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
    otgy_chart_editor* raw_editor = nullptr;
    expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI malformed editor create failed");
    auto editor =
        handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

    const auto direct_edit = direct.click(otgy::core::ChartColumn::Npc, 200U);
    otgy_chart_edit_result abi_edit{};
    expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 200U,
                                   &abi_edit, &raw_error) == OTGY_STATUS_OK,
           "C ABI stale Showtime edit failed");
    compare_edit(direct_edit, abi_edit);
    expect(direct_edit.changed && direct.draft().showtimes().empty() &&
               direct.draft().showtime_start_events().empty() &&
               direct.draft().showtime_move_events().empty(),
           context);
    expect(otgy_chart_editor_get_undo_count(editor.get()) == direct.undo_count(),
           "stale Showtime history differs");

    otgy_chart* raw_copy = nullptr;
    expect(otgy_chart_editor_copy_chart(editor.get(), &raw_copy, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI stale Showtime chart copy failed");
    auto copy = handle<otgy_chart, otgy_chart_destroy>(raw_copy);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(copy.get(), &raw_semantics, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI stale Showtime semantics failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct.draft(), semantics.get());

    const auto direct_undo = direct.undo();
    otgy_chart_edit_result abi_undo{};
    expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI stale Showtime undo failed");
    compare_edit(direct_undo, abi_undo);
    expect(otgy::core::chart_drafts_equal(direct.draft(), malformed),
           "stale Showtime undo did not restore malformed draft");
    expect(otgy_chart_editor_get_redo_count(editor.get()) == direct.redo_count(),
           "stale Showtime redo history differs");
    otgy_chart* raw_restored = nullptr;
    expect(otgy_chart_editor_copy_chart(editor.get(), &raw_restored, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI stale Showtime restored chart copy failed");
    auto restored = handle<otgy_chart, otgy_chart_destroy>(raw_restored);
    otgy_chart_semantics* raw_restored_semantics = nullptr;
    expect(otgy_chart_copy_semantics(restored.get(), &raw_restored_semantics,
                                     &raw_error) == OTGY_STATUS_OK,
           "C ABI stale Showtime restored semantics failed");
    auto restored_semantics = handle<otgy_chart_semantics,
                                     otgy_chart_semantics_destroy>(
        raw_restored_semantics);
    compare_semantics(direct.draft(), restored_semantics.get());
  };

  auto wrong_moves = generated.showtime_move_events();
  wrong_moves[0].dance_move_drill_id = generated.dance_move_drills()[1].id;
  check_pruning(
      otgy::core::ChartDraft{generated.role_events(),
                             generated.showtime_start_events(),
                             std::move(wrong_moves), generated.drill_groups(),
                             generated.dance_move_drills(), generated.showtimes()},
      "SHOW-D was not checked against its own dance move reference");

  auto missing_moves = generated.showtime_move_events();
  auto missing_showtimes = generated.showtimes();
  const auto missing_id = missing_moves[1].id.value;
  missing_moves.erase(missing_moves.begin() + 1);
  missing_showtimes.front().dance_move_drill_ids.erase(
      missing_showtimes.front().dance_move_drill_ids.begin() + 1);
  expect(missing_showtimes.front().show_move_event_ids[1].value == missing_id,
         "missing SHOW-D fixture lost its registration");
  check_pruning(
      otgy::core::ChartDraft{
          generated.role_events(), generated.showtime_start_events(),
          std::move(missing_moves), generated.drill_groups(),
          generated.dance_move_drills(), std::move(missing_showtimes)},
      "missing registered SHOW-D past recorded moves was not pruned");
}

void test_failed_paste_consistency() {
  const auto generated = build_two_complete_moves().draft();
  otgy::core::ChartEditor direct(generated);
  otgy::core::ChartEditor control(generated);

  const auto encoded = otgy::core::encode_recovery_snapshot(generated);
  expect(static_cast<bool>(encoded), "failed paste fixture encode failed");
  otgy_error* raw_error = nullptr;
  otgy_chart* raw_chart = nullptr;
  const otgy_byte_view bytes{encoded.bytes()->data(),
                             static_cast<std::uint64_t>(encoded.bytes()->size())};
  expect(otgy_project_decode_recovery_snapshot(bytes, &raw_chart, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste fixture decode failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  otgy_chart_editor* raw_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste editor create failed");
  auto editor = handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

  const auto compare_chart = [&]() {
    otgy_chart* raw_copy = nullptr;
    expect(otgy_chart_editor_copy_chart(editor.get(), &raw_copy, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI failed paste chart copy failed");
    auto copy = handle<otgy_chart, otgy_chart_destroy>(raw_copy);
    otgy_chart_semantics* raw_semantics = nullptr;
    expect(otgy_chart_copy_semantics(copy.get(), &raw_semantics, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI failed paste semantics failed");
    auto semantics =
        handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(raw_semantics);
    compare_semantics(direct.draft(), semantics.get());
  };

  const auto direct_select = direct.select(otgy::core::ChartColumn::Npc, 0U);
  otgy_chart_session_result abi_session{};
  expect(otgy_chart_editor_select(editor.get(), OTGY_CHART_COLUMN_NPC, 0U,
                                  &abi_session, &raw_error) == OTGY_STATUS_OK,
         "C ABI failed paste select failed");
  compare_session(direct_select, abi_session);
  expect(static_cast<bool>(control.select(otgy::core::ChartColumn::Npc, 0U)),
         "failed paste control select failed");

  const auto direct_copy = direct.copy_selection();
  expect(otgy_chart_editor_copy_selection(editor.get(), &abi_session,
                                          &raw_error) == OTGY_STATUS_OK,
         "C ABI failed paste copy failed");
  compare_session(direct_copy, abi_session);
  expect(static_cast<bool>(control.copy_selection()),
         "failed paste control copy failed");

  const auto direct_edit = direct.click(otgy::core::ChartColumn::Npc, 208U);
  otgy_chart_edit_result abi_edit{};
  expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 208U,
                                 &abi_edit, &raw_error) == OTGY_STATUS_OK,
         "C ABI failed paste setup edit failed");
  compare_edit(direct_edit, abi_edit);
  expect(static_cast<bool>(control.click(otgy::core::ChartColumn::Npc, 208U)),
         "failed paste control setup edit failed");

  const auto direct_undo = direct.undo();
  otgy_chart_edit_result abi_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste setup undo failed");
  compare_edit(direct_undo, abi_undo);
  expect(static_cast<bool>(control.undo()), "failed paste control setup undo failed");
  expect(direct.can_redo() && control.can_redo() &&
             otgy_chart_editor_can_redo(editor.get()) != 0U,
         "failed paste setup did not create a redo branch");

  const auto before = direct.draft();
  const auto undo_count = direct.undo_count();
  const auto redo_count = direct.redo_count();
  const auto selection_before = direct.selections();
  abi_edit = {7U, 7U, 7U};
  const auto direct_failed = direct.paste(8U);
  expect(otgy_chart_editor_paste(editor.get(), 8U, &abi_edit, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste refusal failed");
  compare_edit(direct_failed, abi_edit);
  expect(direct_failed.code == otgy::core::ChartEditCode::PasteRangeOccupied &&
             !direct_failed.changed &&
             otgy::core::chart_drafts_equal(direct.draft(), before) &&
             direct.undo_count() == undo_count && direct.redo_count() == redo_count &&
             direct.can_redo() &&
             otgy_chart_editor_get_undo_count(editor.get()) == undo_count &&
             otgy_chart_editor_get_redo_count(editor.get()) == redo_count &&
             otgy_chart_editor_can_redo(editor.get()) != 0U &&
             direct.selections().size() == selection_before.size() &&
             otgy_chart_editor_has_clipboard(editor.get()) != 0U,
         "failed paste changed draft, history, selection or clipboard");
  otgy_chart_selections* raw_selections = nullptr;
  expect(otgy_chart_editor_copy_selections(editor.get(), &raw_selections,
                                          &raw_error) == OTGY_STATUS_OK,
         "C ABI failed paste selections read-back failed");
  auto selections =
      handle<otgy_chart_selections, otgy_chart_selections_destroy>(raw_selections);
  compare_selections(selection_before, selections.get());
  compare_session_state(direct, editor.get());
  compare_chart();

  const auto direct_redo = direct.redo();
  otgy_chart_edit_result abi_redo{};
  expect(otgy_chart_editor_redo(editor.get(), &abi_redo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste redo failed");
  compare_edit(direct_redo, abi_redo);
  expect(static_cast<bool>(control.redo()), "failed paste control redo failed");
  expect(otgy::core::chart_drafts_equal(direct.draft(), control.draft()),
         "failed paste refusal changed redo result");
  compare_chart();

  const auto direct_paste = direct.paste(300U);
  otgy_chart_edit_result abi_paste{};
  expect(otgy_chart_editor_paste(editor.get(), 300U, &abi_paste, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI failed paste retry failed");
  compare_edit(direct_paste, abi_paste);
  expect(static_cast<bool>(control.paste(300U)),
         "failed paste control retry failed");
  expect(otgy::core::chart_drafts_equal(direct.draft(), control.draft()),
         "failed paste refusal changed clipboard allocation state");
  compare_chart();
}

void test_player_sync_click_effect_consistency() {
  EditorPair pair(empty_chart());
  auto& direct = pair.direct;
  const auto click = [&](otgy::core::ChartColumn column, std::uint64_t row,
                         bool expected_clear = false) {
    const auto before = direct.draft();
    const auto undo_count = direct.undo_count();
    const auto redo_count = direct.redo_count();
    const auto result = direct.click(column, row);
    otgy_chart_edit_result projected{};
    otgy_error* error = nullptr;
    std::uint8_t cleared = 9U;
    expect(otgy_chart_editor_click_with_effects(
               pair.abi.get(), static_cast<std::uint32_t>(column), row,
               &projected, &cleared, &error) == OTGY_STATUS_OK &&
               error == nullptr,
           "C ABI click effects edit failed");
    compare_edit(result, projected);
    expect(cleared == static_cast<std::uint8_t>(result.player_actions_cleared) &&
               result.player_actions_cleared == expected_clear,
           "C ABI player clear effect differs");
    pair.compare();
    if (result.changed) {
      expect(direct.undo_count() == undo_count + 1U &&
                 direct.redo_count() == 0U,
             "click effects command did not create exactly one history entry");
    } else {
      expect(direct.undo_count() == undo_count &&
                 direct.redo_count() == redo_count &&
                 otgy::core::chart_drafts_equal(direct.draft(), before),
             "failed click effects command changed draft or history");
    }
    return result;
  };
  using Column = otgy::core::ChartColumn;
  for (const auto row : {0U, 10U, 14U, 18U, 18U}) {
    expect(static_cast<bool>(click(Column::Npc, row)),
           "click effects NPC fixture failed");
  }
  expect(static_cast<bool>(click(Column::Player, 20U)),
         "click effects player C failed");
  expect(static_cast<bool>(click(Column::Player, 30U)),
         "click effects player generation failed");
  const auto original = direct.draft();
  const auto inserted = click(Column::Npc, 8U, false);
  expect(inserted.code == otgy::core::ChartEditCode::Success &&
             inserted.changed && !inserted.player_actions_cleared,
         "inserting the new first NPC-N did not report successful sync");
  const auto synced = direct.draft();
  const auto projection = otgy::core::project_chart_markers(synced);
  expect(static_cast<bool>(projection), "synced projection failed");
  std::vector<std::uint64_t> player_rows;
  for (const auto& marker : *projection.markers()) {
    if (marker.column == Column::Player) {
      player_rows.push_back(marker.row);
    }
  }
  expect(player_rows == std::vector<std::uint64_t>({20U, 28U, 30U, 34U, 38U}),
         "synchronized player rows differ");
  const auto failed = click(Column::Npc, 19U);
  expect(failed.code == otgy::core::ChartEditCode::CannotAppendAfterEnd,
         "failed NPC command returned the wrong result");
  const auto converted_effect = click(Column::Npc, 8U, false);
  expect(converted_effect.code == otgy::core::ChartEditCode::Success &&
             converted_effect.changed && !converted_effect.player_actions_cleared,
         "converting the first NPC-N did not report synchronized player actions");
  const auto converted_snapshot = direct.draft();
  pair.history(true);
  expect(otgy::core::chart_drafts_equal(direct.draft(), synced),
         "one undo did not restore synchronized actions and relationships");
  expect(!click(Column::Player, 14U).changed,
         "wrong-column click unexpectedly changed the draft");
  pair.history(false);
  expect(otgy::core::chart_drafts_equal(direct.draft(), converted_snapshot),
         "redo did not restore the exact converted snapshot");
  pair.history(true);
  pair.history(true);
  expect(otgy::core::chart_drafts_equal(direct.draft(), original),
         "synchronization undo did not restore the original snapshot");
  expect(click(Column::Player, 30U).changed,
         "manual player clear failed");
  expect(click(Column::Npc, 8U).changed,
         "NPC edit with a C-only player block failed");
  const auto c_only_projection = otgy::core::project_chart_markers(direct.draft());
  expect(static_cast<bool>(c_only_projection), "C-only projection failed");
  expect(std::count_if(c_only_projection.markers()->begin(),
                       c_only_projection.markers()->end(), [](const auto& marker) {
                         return marker.column == Column::Player;
                       }) == 1,
         "NPC edit refilled an intentionally cleared player block");
  const auto before_delete = direct.draft();
  expect(click(Column::Npc, 0U).changed && direct.draft().role_events().empty(),
         "NPC-C deletion left its corresponding C-only player block");
  pair.history(true);
  expect(otgy::core::chart_drafts_equal(direct.draft(), before_delete),
         "one undo did not restore the C-only block");
  pair.history(false);
  expect(direct.draft().role_events().empty(),
         "redo did not delete the corresponding block");
}

void test_slk_finished_boundary(const std::vector<std::uint8_t>& bytes) {
  const auto direct_import =
      otgy::core::import_finished_slk(bytes.data(), bytes.size());
  expect(direct_import.has_value(), "direct SLK import failed");
  otgy_error* raw_error = nullptr;
  otgy_slk_document* raw_document = nullptr;
  expect(otgy_slk_import(view(bytes), &raw_document, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI SLK import failed");
  auto document =
      handle<otgy_slk_document, otgy_slk_document_destroy>(raw_document);
  const auto direct_projection =
      otgy::core::project_finished_slk_to_chart_draft(
          *direct_import.document());
  expect(direct_projection.has_value(), "direct SLK projection failed");
  otgy_chart* raw_chart = nullptr;
  expect(otgy_slk_document_project_chart(document.get(), &raw_chart,
                                         &raw_error) == OTGY_STATUS_OK,
         "C ABI SLK projection failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  document.reset();

  otgy_chart_semantics* raw_semantics = nullptr;
  expect(otgy_chart_copy_semantics(chart.get(), &raw_semantics, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI chart semantic snapshot failed");
  auto semantics =
      handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(
          raw_semantics);
  compare_semantics(*direct_projection.draft(), semantics.get());

  const otgy::core::TimingParameters range_timing{100.0, 0.0};
  const auto direct_range = otgy::core::calculate_chart_timeline_range(
      *direct_projection.draft(), range_timing, std::nullopt);
  otgy_timeline_range abi_range{};
  expect(otgy_chart_calculate_timeline_range(
             chart.get(), {100.0, 0.0}, 0U, 0.0, &abi_range,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI imported chart range failed");
  expect(same_double(abi_range.end_seconds,
                     direct_range.range()->end_seconds()) &&
             same_double(abi_range.end_sixteenth_row,
                         direct_range.range()->end_sixteenth_row()),
         "imported chart timeline range differs");

  const auto direct_markers =
      otgy::core::project_chart_markers(*direct_projection.draft());
  otgy_chart_markers* raw_markers = nullptr;
  expect(otgy_chart_project_markers(chart.get(), &raw_markers, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI imported marker projection failed");
  auto markers =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_markers);
  compare_markers(*direct_markers.markers(), markers.get());

  const auto direct_moves =
      otgy::core::project_showtime_moves(*direct_projection.draft());
  otgy_showtime_moves* raw_moves = nullptr;
  expect(otgy_chart_project_showtime_moves(chart.get(), &raw_moves,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI Showtime projection failed");
  auto moves =
      handle<otgy_showtime_moves, otgy_showtime_moves_destroy>(raw_moves);
  compare_showtime_moves(*direct_moves.moves(), moves.get());

  for (const auto& marker : *direct_markers.markers()) {
    if (marker.type == otgy::core::ChartMarkerType::NpcNote) {
      const auto direct = otgy::core::chart_note_ordinal(
          *direct_projection.draft(), marker.id);
      std::uint64_t abi = 0U;
      expect(otgy_chart_note_ordinal(chart.get(), marker.id.value, &abi,
                                     &raw_error) == OTGY_STATUS_OK &&
                 abi == direct.ordinal,
             "imported note ordinal differs");
      break;
    }
  }

  auto direct_validation =
      otgy::core::validate_finished_chart(*direct_projection.draft());
  expect(direct_validation.is_finished(), "direct chart validation failed");
  otgy_chart_validation* raw_validation = nullptr;
  expect(otgy_chart_validate_finished(chart.get(), &raw_validation,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI finished validation failed");
  auto validation =
      handle<otgy_chart_validation, otgy_chart_validation_destroy>(
          raw_validation);
  expect(otgy_chart_validation_is_finished(validation.get()) == 1U &&
             otgy_chart_validation_get_diagnostic_count(validation.get()) ==
                 0U,
         "finished validation state differs");
  otgy_finished_chart* raw_finished = nullptr;
  expect(otgy_chart_validation_copy_finished(
             validation.get(), &raw_finished, &raw_error) == OTGY_STATUS_OK,
         "C ABI finished chart copy failed");
  auto finished =
      handle<otgy_finished_chart, otgy_finished_chart_destroy>(raw_finished);
  validation.reset();
  const auto direct_export = otgy::core::export_finished_chart_to_slk(
      *direct_validation.finished_chart());
  otgy_bytes* raw_export = nullptr;
  expect(otgy_finished_chart_export_slk(finished.get(), &raw_export,
                                        &raw_error) == OTGY_STATUS_OK,
         "C ABI SLK export failed");
  auto export_bytes = handle<otgy_bytes, otgy_bytes_destroy>(raw_export);
  const auto abi_export = otgy_bytes_get_view(export_bytes.get());
  expect(abi_export.length == direct_export.bytes()->size() &&
             std::memcmp(abi_export.data, direct_export.bytes()->data(),
                         direct_export.bytes()->size()) == 0 &&
             abi_export.length == bytes.size() &&
             std::memcmp(abi_export.data, bytes.data(), bytes.size()) == 0,
         "canonical SLK bytes differ");

  auto direct_empty = otgy::core::import_finished_slk(nullptr, 0U);
  otgy_slk_document* failed_document = reinterpret_cast<otgy_slk_document*>(1);
  expect(otgy_slk_import({nullptr, 0U}, &failed_document, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "empty SLK error status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(failed_document == nullptr, "failed SLK exposed a document");
  expect_error(error.get(), OTGY_ERROR_DOMAIN_SLK_IMPORT,
               static_cast<std::uint32_t>(direct_empty.failure()->error),
               otgy::core::slk_import_error_message(
                   direct_empty.failure()->error));

  otgy_slk_document_destroy(nullptr);
  otgy_chart_validation_destroy(nullptr);
  otgy_finished_chart_destroy(nullptr);
}

void test_slk_position() {
  otgy_error* raw_error = nullptr;
  const auto direct = otgy::core::compose_slk_position(12U, 3U, 2U);
  otgy_slk_position abi{};
  expect(otgy_compose_slk_position(12U, 3U, 2U, &abi, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI SLK position compose failed");
  expect(abi.measure == direct.position()->measure &&
             abi.beat == direct.position()->beat &&
             abi.sixteenth == direct.position()->sixteenth &&
             abi.total_sixteenth == direct.position()->total_sixteenth,
         "SLK position compose differs");
  const auto decomposed =
      otgy::core::decompose_slk_position(direct.position()->total_sixteenth);
  expect(otgy_decompose_slk_position(abi.total_sixteenth, &abi, &raw_error) ==
             OTGY_STATUS_OK &&
             abi.measure == decomposed.position()->measure,
         "SLK position decompose differs");
  const auto validated = otgy::core::validate_slk_position(
      abi.measure, abi.beat, abi.sixteenth, abi.total_sixteenth);
  expect(otgy_validate_slk_position(abi.measure, abi.beat, abi.sixteenth,
                                    abi.total_sixteenth, &abi,
                                    &raw_error) == OTGY_STATUS_OK &&
             abi.total_sixteenth == validated.position()->total_sixteenth,
         "SLK position validate differs");

  const auto direct_error = otgy::core::compose_slk_position(0U, 4U, 0U);
  expect(otgy_compose_slk_position(0U, 4U, 0U, &abi, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "SLK position error status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_SLK_POSITION,
               static_cast<std::uint32_t>(direct_error.error()),
               otgy::core::slk_position_error_message(direct_error.error()));
}

}  // namespace

int main() {
  try {
    test_timing_and_coordination();
    auto edited = test_chart_edit_and_persistence();
    expect(edited != nullptr, "edited chart ownership was lost");
    test_batch_selection_and_preview();
    test_selection_order_and_lifecycle();
    test_deferred_cut_consistency();
    test_split_merge_consistency();
    test_cut_showtime_consistency();
    test_stale_showtime_consistency();
    test_failed_paste_consistency();
    test_player_sync_click_effect_consistency();
    test_slk_finished_boundary(read_file(OTGY_C_API_SLK_FIXTURE));
    test_slk_position();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
