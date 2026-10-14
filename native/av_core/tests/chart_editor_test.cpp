// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"
#include "av_core/project/project_serialization.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartColumn;
using otgy::core::ChartDraft;
using otgy::core::ChartEditCode;
using otgy::core::ChartEditor;
using otgy::core::ChartMarker;
using otgy::core::ChartMarkerType;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;

[[nodiscard]] ChartMarker marker_of(const ChartDraft& draft,
                                    ChartMarkerType type,
                                    std::size_t occurrence = 0U) {
  const auto projection = otgy::core::project_chart_markers(draft);
  assert(projection);
  for (const auto& marker : *projection.markers()) {
    if (marker.type == type) {
      if (occurrence == 0U) {
        return marker;
      }
      --occurrence;
    }
  }
  assert(false);
  return projection.markers()->front();
}

void test_npc_context_rules_and_history() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  const auto empty = editor.draft();
  const auto c = editor.click(ChartColumn::Npc, 0U);
  assert(c && c.changed);
  assert(editor.undo_count() == 1U);
  assert(editor.draft().role_events().size() == 1U);
  assert(editor.draft().drill_groups().size() == 1U);
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(marker_of(editor.draft(), ChartMarkerType::NpcBlockStart).row == 0U);

  assert(editor.click(ChartColumn::Npc, 10U));
  assert(editor.click(ChartColumn::Npc, 20U));
  const auto original_second_note =
      marker_of(editor.draft(), ChartMarkerType::NpcNote, 1U).id;
  const auto before_conversion = editor.draft();
  assert(editor.click(ChartColumn::Npc, 20U));
  assert(marker_of(editor.draft(), ChartMarkerType::NpcMoveEnd).id.value ==
         original_second_note.value);
  assert(editor.draft().dance_move_drills().front().move_n_count == 1U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before_conversion));
  assert(editor.redo());
  assert(editor.click(ChartColumn::Npc, 20U));
  assert(editor.draft().role_events().size() == 2U);

  while (editor.can_undo()) {
    assert(editor.undo());
  }
  assert(otgy::core::chart_drafts_equal(editor.draft(), empty));
  assert(editor.can_redo());
  assert(editor.click(ChartColumn::Npc, 5U));
  assert(!editor.can_redo());
  assert(editor.redo().code == ChartEditCode::NothingToRedo);
}

void test_npc_limit_insertion_and_block_deletion() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(editor.click(ChartColumn::Npc, 0U));
  for (std::uint64_t index = 1U; index <= 11U; ++index) {
    assert(editor.click(ChartColumn::Npc, index * 10U));
  }
  assert(editor.click(ChartColumn::Npc, 130U));
  assert(marker_of(editor.draft(), ChartMarkerType::NpcMoveEnd).row == 130U);
  assert(editor.click(ChartColumn::Npc, 120U));
  assert(marker_of(editor.draft(), ChartMarkerType::NpcNote, 11U).row == 120U);
  assert(editor.draft().dance_move_drills().front().move_n_count == 12U);
  const auto history_before_failure = editor.undo_count();
  const auto failure = editor.click(ChartColumn::Npc, 140U);
  assert(failure.code == ChartEditCode::CannotAppendAfterEnd);
  assert(!failure.changed);
  assert(editor.undo_count() == history_before_failure);

  assert(editor.click(ChartColumn::Player, 140U));
  const auto player_c = marker_of(editor.draft(),
                                  ChartMarkerType::PlayerBlockStart);
  const auto before_delete = editor.draft();
  assert(editor.click(ChartColumn::Npc, 0U));
  assert(editor.draft().role_events().size() == 1U);
  assert(editor.draft().role_events().front().id.value == player_c.id.value);
  assert(editor.draft().drill_groups().front().npc_event_ids.empty());
  assert(editor.draft().drill_groups().front().player_event_ids.size() == 1U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before_delete));
}

[[nodiscard]] ChartEditor build_player_source() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(editor.click(ChartColumn::Npc, 0U));
  assert(editor.click(ChartColumn::Npc, 10U));
  assert(editor.click(ChartColumn::Npc, 25U));
  assert(editor.click(ChartColumn::Npc, 40U));
  assert(editor.click(ChartColumn::Npc, 40U));
  assert(editor.click(ChartColumn::Player, 50U));
  return editor;
}

void test_player_generation_is_atomic_and_detached() {
  auto editor = build_player_source();
  const auto before_generation = editor.draft();
  const auto history_before = editor.undo_count();
  const auto generation = editor.click(ChartColumn::Player, 100U);
  assert(generation);
  assert(editor.undo_count() == history_before + 1U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerNote).row == 100U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerNote, 1U).row ==
         115U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerMoveEnd).row ==
         130U);
  assert(editor.draft().drill_groups().front().player_event_ids.size() == 4U);

  const auto generated = editor.draft();
  assert(editor.click(ChartColumn::Npc, 25U));
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerNote).row == 100U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerNote, 1U).row ==
         115U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerMoveEnd).row ==
         130U);
  assert(editor.draft().dance_move_drills().front().move_n_count == 1U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));

  assert(editor.click(ChartColumn::Player, 115U).code ==
         ChartEditCode::AutoGeneratedMarkerLocked);
  assert(editor.click(ChartColumn::Player, 130U).code ==
         ChartEditCode::AutoGeneratedMarkerLocked);
  assert(editor.click(ChartColumn::Player, 100U));
  assert(editor.draft().drill_groups().front().player_event_ids.size() == 1U);
  assert(marker_of(editor.draft(), ChartMarkerType::PlayerBlockStart).row ==
         50U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));
  assert(editor.redo());
  assert(editor.undo());

  assert(editor.click(ChartColumn::Player, 50U));
  assert(editor.draft().drill_groups().front().player_event_ids.empty());
  assert(editor.draft().role_events().size() == 4U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));

  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before_generation));
}

void test_player_failures_preserve_draft_and_history() {
  ChartEditor missing{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(missing.click(ChartColumn::Npc, 0U));
  assert(missing.click(ChartColumn::Player, 10U));
  const auto missing_before = missing.draft();
  const auto missing_history = missing.undo_count();
  const auto no_source = missing.click(ChartColumn::Player, 20U);
  assert(no_source.code == ChartEditCode::NoNpcSource);
  assert(!no_source.changed);
  assert(otgy::core::chart_drafts_equal(missing.draft(), missing_before));
  assert(missing.undo_count() == missing_history);

  auto source = build_player_source();
  auto role_events = source.draft().role_events();
  auto start_events = source.draft().showtime_start_events();
  start_events.push_back({{999U}, 120U, {999U}});
  ChartDraft occupied{std::move(role_events), std::move(start_events),
                      source.draft().showtime_move_events(),
                      source.draft().drill_groups(),
                      source.draft().dance_move_drills(),
                      source.draft().showtimes()};
  ChartEditor conflict{occupied};
  const auto conflict_before = conflict.draft();
  const auto blocked = conflict.click(ChartColumn::Player, 100U);
  assert(blocked.code == ChartEditCode::GenerationRangeOccupied);
  assert(!blocked.changed);
  assert(otgy::core::chart_drafts_equal(conflict.draft(), conflict_before));

  const auto occupied_row = conflict.click(ChartColumn::Player, 10U);
  assert(occupied_row.code == ChartEditCode::PositionOccupied);
  assert(!occupied_row.changed);
}

[[nodiscard]] ChartEditor build_two_complete_moves();

[[nodiscard]] std::vector<std::uint64_t> drill_rows(
    const ChartDraft& draft, otgy::core::ChartDanceMoveDrillId drill_id) {
  std::vector<std::uint64_t> rows;
  for (const auto& event : draft.role_events()) {
    if (event.dance_move_drill_id.value == drill_id.value) {
      rows.push_back(event.row);
    }
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

[[nodiscard]] std::vector<std::uint64_t> drill_event_ids(
    const ChartDraft& draft, otgy::core::ChartDanceMoveDrillId drill_id) {
  std::vector<std::uint64_t> ids;
  for (const auto& event : draft.role_events()) {
    if (event.dance_move_drill_id.value == drill_id.value) {
      ids.push_back(event.id.value);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

void test_paste_is_independent_and_atomic() {
  // Two pastes of one fragment produce two dance moves with disjoint
  // identifiers and exactly the same relative row geometry. Each paste is a
  // single history entry and one undo removes the whole move.
  {
    auto editor = build_two_complete_moves();
    const auto before = editor.draft();
    const auto source_drill = before.dance_move_drills()[0].id;
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto fragment_rows = drill_rows(editor.clipboard().content, source_drill);
    assert(fragment_rows ==
           std::vector<std::uint64_t>({0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U}));

    const auto history = editor.undo_count();
    const auto first = editor.paste(200U);
    assert(first);
    assert(first.changed);
    assert(editor.undo_count() == history + 1U);
    assert(!editor.can_redo());
    assert(editor.draft().dance_move_drills().size() == 3U);
    const auto first_drill = editor.draft().dance_move_drills()[2].id;
    assert(editor.draft().dance_move_drills()[2].move_n_count == 2U);
    assert(drill_rows(editor.draft(), first_drill) ==
           std::vector<std::uint64_t>({200U, 202U, 204U, 206U, 208U, 210U,
                                       212U, 214U}));
    assert(first.primary_event_id.value ==
           drill_event_ids(editor.draft(), first_drill).front());
    assert(editor.selection().active);
    assert(editor.selection().drill_id.value == first_drill.value);
    assert(editor.selection().first_row == 200U);
    assert(editor.selection().last_row == 214U);

    const auto second = editor.paste(300U);
    assert(second);
    assert(editor.undo_count() == history + 2U);
    assert(editor.draft().dance_move_drills().size() == 4U);
    const auto second_drill = editor.draft().dance_move_drills()[3].id;
    assert(drill_rows(editor.draft(), second_drill) ==
           std::vector<std::uint64_t>({300U, 302U, 304U, 306U, 308U, 310U,
                                       312U, 314U}));
    const auto source_ids = drill_event_ids(editor.draft(), source_drill);
    const auto first_ids = drill_event_ids(editor.draft(), first_drill);
    const auto second_ids = drill_event_ids(editor.draft(), second_drill);
    for (const auto id : first_ids) {
      assert(std::find(source_ids.begin(), source_ids.end(), id) ==
             source_ids.end());
      assert(std::find(second_ids.begin(), second_ids.end(), id) ==
             second_ids.end());
    }

    assert(editor.undo());
    assert(editor.draft().dance_move_drills().size() == 3U);
    assert(editor.draft().dance_move_drills().back().id.value ==
           first_drill.value);
    assert(editor.can_redo());
    assert(editor.redo());
    assert(editor.draft().dance_move_drills().size() == 4U);
    assert(editor.draft().dance_move_drills().back().id.value ==
           second_drill.value);
  }

  // A stale group vector, a missing MoveEnd and an inflated move_n_count all
  // survive the round trip verbatim: paste transports, it never recomputes.
  {
    ChartDraft draft{
        std::vector<ChartRoleEvent>{
            {{1U}, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart, {1U}, {1U}},
            {{2U}, 2U, ChartRole::Npc, ChartRoleEventType::Note, {1U}, {1U}},
            {{5U}, 4U, ChartRole::Npc, ChartRoleEventType::Note, {3U}, {1U}},
            {{3U}, 8U, ChartRole::Npc, ChartRoleEventType::BlockStart, {2U}, {2U}},
            {{4U}, 10U, ChartRole::Npc, ChartRoleEventType::Note, {2U}, {2U}},
        },
        {},
        {},
        std::vector<otgy::core::ChartDrillGroup>{
            {{1U}, {1U}, {{1U}, {2U}, {3U}}, {}},
            {{2U}, {2U}, {{3U}, {4U}}, {}},
            {{3U}, {1U}, {{5U}}, {}},
        },
        std::vector<otgy::core::ChartDanceMoveDrill>{
            {{1U}, 5U, {{1U}}},
            {{2U}, 1U, {{2U}}},
        },
        {}};
    ChartEditor editor{std::move(draft)};
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    assert(editor.paste(200U));
    const auto& pasted = editor.draft().dance_move_drills().back();
    assert(pasted.move_n_count == 5U);
    assert(pasted.drill_group_ids.size() == 2U);
    assert(drill_rows(editor.draft(), pasted.id) ==
           std::vector<std::uint64_t>({200U, 202U, 204U}));
    assert(editor.draft().role_events().size() == 8U);
    bool has_npc_end = false;
    for (const auto& event : editor.draft().role_events()) {
      if (event.dance_move_drill_id.value == pasted.id.value &&
          event.role == ChartRole::Npc &&
          event.type == ChartRoleEventType::MoveEnd) {
        has_npc_end = true;
      }
    }
    assert(!has_npc_end);
    for (const auto& group : editor.draft().drill_groups()) {
      if (group.dance_move_drill_id.value == pasted.id.value) {
        assert(!group.npc_event_ids.empty());
      }
    }
  }
}

void test_paste_failures_preserve_state() {
  // Occupancy is checked over the whole closed target interval, so a single
  // marker in the middle rejects the paste without a partial result.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    assert(editor.click(ChartColumn::Npc, 208U));
    const auto before = editor.draft();
    const auto history = editor.undo_count();

    const auto occupied = editor.paste(200U);
    assert(occupied.code == ChartEditCode::PasteRangeOccupied);
    assert(!occupied.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(!editor.can_redo());
    assert(editor.draft().dance_move_drills().size() ==
           before.dance_move_drills().size());
    assert(editor.draft().dance_move_drills().size() == 3U);

    const auto both_ends = editor.paste(0U);
    assert(both_ends.code == ChartEditCode::PasteRangeOccupied);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
  }

  // The anchor row itself is part of the protected interval.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto occupied = editor.paste(8U);
    assert(occupied.code == ChartEditCode::PasteRangeOccupied);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
  }

  // Row overflow is detected before any allocation.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto overflow = editor.paste(std::numeric_limits<std::uint64_t>::max());
    assert(overflow.code == ChartEditCode::RangeOverflow);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
  }

  // A pasted NPC block start obeys the Showtime display range, with the final
  // half measure of the last move still available.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto blocked = editor.paste(100U);
    assert(blocked.code == ChartEditCode::ShowtimeRangeOccupied);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);

    const auto allowed = editor.paste(106U);
    assert(allowed);
    assert(editor.undo_count() == history + 1U);
    assert(editor.draft().dance_move_drills().size() == 3U);
  }

  // An empty clipboard and an empty editor both refuse without touching state.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    assert(!editor.has_clipboard());
    const auto empty = editor.paste(0U);
    assert(empty.code == ChartEditCode::ClipboardEmpty);
    assert(!empty.changed);
    assert(!editor.can_undo());

    auto loaded = build_two_complete_moves();
    const auto before = loaded.draft();
    const auto history = loaded.undo_count();
    assert(!loaded.has_clipboard());
    const auto refused = loaded.paste(200U);
    assert(refused.code == ChartEditCode::ClipboardEmpty);
    assert(otgy::core::chart_drafts_equal(loaded.draft(), before));
    assert(loaded.undo_count() == history);
  }

  // Identifier exhaustion is reported before anything is built, and the
  // clipboard survives the refusal so the user can retry in another chart.
  {
    constexpr std::uint64_t kExhausted =
        std::numeric_limits<std::uint64_t>::max();
    ChartDraft draft{
        std::vector<ChartRoleEvent>{
            {{1U}, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart, {1U}, {1U}},
            {{2U}, 2U, ChartRole::Npc, ChartRoleEventType::Note, {1U}, {1U}},
            {{kExhausted}, 10U, ChartRole::Npc, ChartRoleEventType::BlockStart,
             {2U}, {2U}},
        },
        {},
        {},
        std::vector<otgy::core::ChartDrillGroup>{
            {{1U}, {1U}, {{1U}, {2U}}, {}},
            {{2U}, {2U}, {{kExhausted}}, {}},
        },
        std::vector<otgy::core::ChartDanceMoveDrill>{
            {{1U}, 1U, {{1U}}},
            {{2U}, 1U, {{2U}}},
        },
        {}};
    ChartEditor editor{std::move(draft)};
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto exhausted = editor.paste(200U);
    assert(exhausted.code == ChartEditCode::IdExhausted);
    assert(!exhausted.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(editor.has_clipboard());
    assert(editor.selection().active);
  }
}

void test_paste_round_trip_through_formal_payload() {
  auto editor = build_two_complete_moves();
  assert(editor.select(ChartColumn::Npc, 16U).selection.active);
  assert(editor.copy_selection());
  assert(editor.paste(200U));
  const auto pasted = editor.draft();

  const auto encoded = otgy::core::encode_formal_draft(pasted, 100.0, 0.0);
  assert(encoded);
  const auto decoded = otgy::core::decode_formal_draft(encoded.bytes()->data(),
                                                       encoded.bytes()->size());
  assert(decoded);
  assert(otgy::core::chart_drafts_equal(decoded.payload()->chart, pasted));
  const auto again = otgy::core::encode_formal_draft(
      decoded.payload()->chart, decoded.payload()->bpm,
      decoded.payload()->offset_seconds);
  assert(again);
  assert(*again.bytes() == *encoded.bytes());
}

[[nodiscard]] std::vector<std::uint64_t> fragment_rows(
    const otgy::core::ChartClipboardFragment& fragment) {
  std::vector<std::uint64_t> rows;
  for (const auto& event : fragment.content.role_events()) {
    rows.push_back(event.row);
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

/// Cutting copies the fragment and deletes exactly the objects that fragment
/// owns. It is not blocked by a Showtime that refers to the dance move: the
/// reference simply becomes dangling, which the user resolves by deleting and
/// rebuilding the Showtime.
void test_cut_selection_removes_exactly_its_own_objects() {
  // (a) A dance move no Showtime refers to is removed together with its drill
  // and its drill groups, and the remaining dance move keeps every value.
  {
    auto editor = build_two_complete_moves();
    const auto before = editor.draft();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    const auto selected_drill = editor.selection().drill_id.value;
    assert(selected_drill == before.dance_move_drills().front().id.value);
    const auto cut_rows = drill_rows(before, {selected_drill});
    const auto cut_groups = before.dance_move_drills().front().drill_group_ids;
    const auto history = editor.undo_count();

    const auto cut = editor.cut_selection();
    assert(cut);
    assert(cut.changed);
    const auto cut_events = drill_event_ids(before, {selected_drill});
    assert(std::find(cut_events.begin(), cut_events.end(),
                     cut.primary_event_id.value) != cut_events.end());
    assert(editor.undo_count() == history + 1U);
    const auto after = editor.draft();
    assert(after.dance_move_drills().size() == 1U);
    assert(after.dance_move_drills().front().id.value ==
           before.dance_move_drills()[1].id.value);
    assert(after.dance_move_drills().front().move_n_count ==
           before.dance_move_drills()[1].move_n_count);
    for (const auto row : cut_rows) {
      for (const auto& event : after.role_events()) {
        assert(!(event.row == row &&
                 event.dance_move_drill_id.value == selected_drill));
      }
    }
    for (const auto id : cut_groups) {
      for (const auto& group : after.drill_groups()) {
        assert(group.id.value != id.value);
      }
      for (const auto& drill : after.dance_move_drills()) {
        for (const auto group_id : drill.drill_group_ids) {
          assert(group_id.value != id.value);
        }
      }
    }
    for (const auto& event : after.role_events()) {
      assert(event.dance_move_drill_id.value != selected_drill);
    }
    assert(!editor.selection().active);
    assert(editor.has_clipboard());
    assert(fragment_rows(editor.clipboard()).front() == 0U);

    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.can_redo());
  }

  // (b) The clipboard holds the cut dance move, so pasting it elsewhere
  // rebuilds equivalent content with entirely new identifiers.
  {
    auto editor = build_two_complete_moves();
    const auto before = editor.draft();
    const auto selected = editor.select(ChartColumn::Npc, 16U);
    assert(selected);
    const auto source_drill = editor.selection().drill_id.value;
    assert(source_drill == before.dance_move_drills()[1].id.value);
    const auto source_rows = drill_rows(before, {source_drill});
    const auto source_events = drill_event_ids(before, {source_drill});
    assert(editor.cut_selection());
    assert(editor.has_clipboard());
    const auto clipboard_rows = fragment_rows(editor.clipboard());
    assert(clipboard_rows.size() == source_rows.size());
    const std::uint64_t base = source_rows.front();
    for (std::size_t index = 0U; index < source_rows.size(); ++index) {
      assert(clipboard_rows[index] == source_rows[index] - base);
    }

    const auto pasted = editor.paste(200U);
    assert(pasted);
    const auto after = editor.draft();
    assert(after.dance_move_drills().size() == 2U);
    const auto pasted_drill = editor.selection().drill_id.value;
    assert(pasted_drill != source_drill);
    const auto pasted_rows = drill_rows(after, {pasted_drill});
    assert(pasted_rows.size() == source_rows.size());
    for (std::size_t index = 0U; index < pasted_rows.size(); ++index) {
      assert(pasted_rows[index] == 200U + (source_rows[index] - base));
    }
    for (const auto id : drill_event_ids(after, {pasted_drill})) {
      for (const auto source : source_events) {
        assert(id != source);
      }
    }
  }

  // (c) A Showtime that refers to the dance move does not block the cut. The cut
  // removes only the dance move's own objects, and the Showtime that was built
  // from it becomes stale the moment that dance move is gone: the whole segment
  // goes in the same history entry, so one undo brings both back together.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    const auto before = editor.draft();
    assert(before.showtimes().size() == 1U);
    const auto referenced = before.showtimes().front().dance_move_drill_ids;
    assert(referenced.size() == 2U);
    const auto selected = editor.select(ChartColumn::Npc, 16U);
    assert(selected);
    assert(editor.selection().drill_id.value == referenced[1].value);
    const auto history = editor.undo_count();

    const auto cut = editor.cut_selection();
    assert(cut);
    assert(cut.changed);
    assert(editor.undo_count() == history + 1U);
    const auto after = editor.draft();
    assert(after.dance_move_drills().size() == 1U);
    assert(after.showtime_start_events().empty());
    assert(after.showtime_move_events().empty());
    assert(after.showtimes().empty());
    for (const auto& drill : after.dance_move_drills()) {
      assert(drill.id.value != referenced[1].value);
    }
    for (const auto& event : after.drill_groups()) {
      assert(event.dance_move_drill_id.value != referenced[1].value);
    }
    for (const auto& event : after.role_events()) {
      assert(event.dance_move_drill_id.value != referenced[1].value);
    }
    assert(editor.has_clipboard());

    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  }

  // (d) Failures leave the draft, the history and the clipboard untouched.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto clipboard_rows = fragment_rows(editor.clipboard());
    assert(editor.clear_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();

    const auto no_selection = editor.cut_selection();
    assert(no_selection.code == ChartEditCode::SelectionUnavailable);
    assert(!no_selection.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(fragment_rows(editor.clipboard()) == clipboard_rows);
  }

  // (e) Clearing the clipboard drops the payload without touching the draft,
  // the history or the selection.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    assert(editor.has_clipboard());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto drill = editor.selection().drill_id.value;

    const auto cleared = editor.clear_clipboard();
    assert(cleared);
    assert(cleared.changed);
    assert(!editor.has_clipboard());
    assert(editor.clipboard().content.role_events().empty());
    assert(editor.selection().active);
    assert(editor.selection().drill_id.value == drill);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(editor.paste(200U).code == ChartEditCode::ClipboardEmpty);

    const auto again = editor.clear_clipboard();
    assert(again);
    assert(!again.changed);
    assert(editor.paste(200U).code == ChartEditCode::ClipboardEmpty);
  }
}

void test_copy_selection_fragment() {
  // The fragment carries exactly the selected dance move with both roles'
  // events rebased so the smallest offset is zero. Copying never touches the
  // draft, the history or the redo branch.
  {
    auto editor = build_two_complete_moves();
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    const auto copied = editor.copy_selection();
    assert(copied);
    assert(copied.changed);
    assert(copied.selection.active);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(!editor.can_redo());

    const auto& fragment = editor.clipboard();
    assert(fragment.base_row == 0U);
    assert(fragment_rows(fragment) ==
           std::vector<std::uint64_t>({0U, 2U, 4U, 6U, 8U, 10U, 12U, 14U}));
    const auto& content = fragment.content;
    assert(content.dance_move_drills().size() == 1U);
    assert(content.dance_move_drills().front().id.value ==
           before.dance_move_drills()[0].id.value);
    assert(content.dance_move_drills().front().move_n_count == 2U);
    assert(content.drill_groups().size() == 1U);
    assert(content.showtime_start_events().empty());
    assert(content.showtime_move_events().empty());
    assert(content.showtimes().empty());
    for (const auto& event : content.role_events()) {
      assert(event.dance_move_drill_id.value ==
             content.dance_move_drills().front().id.value);
      assert(event.drill_group_id.value == content.drill_groups()[0].id.value);
      assert(event.id.value != 0U);
    }
    assert(content.drill_groups()[0].npc_event_ids.size() == 4U);
    assert(content.drill_groups()[0].player_event_ids.size() == 4U);
  }

  // A later dance move keeps a non-zero base row and the same relative spacing.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 16U).selection.active);
    assert(editor.selection().first_row == 16U);
    assert(editor.selection().last_row == 50U);
    assert(editor.copy_selection());
    const auto& fragment = editor.clipboard();
    assert(fragment.base_row == 16U);
    assert(fragment.content.role_events().size() == 18U);
    assert(fragment.content.dance_move_drills().front().move_n_count == 7U);
    std::vector<std::uint64_t> offsets = fragment_rows(fragment);
    assert(offsets.front() == 0U);
    assert(offsets.back() == 34U);
    assert(fragment.content.drill_groups().size() == 1U);
    assert(fragment.content.drill_groups()[0].npc_event_ids.size() == 9U);
    assert(fragment.content.drill_groups()[0].player_event_ids.size() == 9U);
  }

  // Selecting through a Showtime move copies that move's dance move and still
  // leaves every Showtime vector empty: a display range is derived, not
  // transportable content.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    assert(editor.draft().showtimes().size() == 1U);
    assert(editor.select(ChartColumn::Showtime, 80U).selection.active);
    assert(editor.selection().drill_id.value ==
           editor.draft().dance_move_drills()[1].id.value);
    assert(editor.copy_selection());
    const auto& content = editor.clipboard().content;
    assert(content.dance_move_drills().size() == 1U);
    assert(content.dance_move_drills().front().move_n_count == 7U);
    assert(content.drill_groups().size() == 1U);
    assert(content.showtime_start_events().empty());
    assert(content.showtime_move_events().empty());
    assert(content.showtimes().empty());
  }

  // Fragment membership is rebuilt from the drill reference: a stale group
  // vector loses the foreign event, and a group bound to the selected dance
  // move joins even when the drill never listed it. move_n_count is copied
  // verbatim and is never recomputed.
  {
    ChartDraft draft{
        std::vector<ChartRoleEvent>{
            {{1U}, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart, {1U}, {1U}},
            {{2U}, 2U, ChartRole::Npc, ChartRoleEventType::Note, {1U}, {1U}},
            {{5U}, 4U, ChartRole::Npc, ChartRoleEventType::Note, {3U}, {1U}},
            {{3U}, 8U, ChartRole::Npc, ChartRoleEventType::BlockStart, {2U}, {2U}},
            {{4U}, 10U, ChartRole::Npc, ChartRoleEventType::Note, {2U}, {2U}},
        },
        {},
        {},
        std::vector<otgy::core::ChartDrillGroup>{
            {{1U}, {1U}, {{1U}, {2U}, {3U}}, {}},
            {{2U}, {2U}, {{3U}, {4U}}, {}},
            {{3U}, {1U}, {{5U}}, {}},
        },
        std::vector<otgy::core::ChartDanceMoveDrill>{
            {{1U}, 5U, {{1U}}},
            {{2U}, 1U, {{2U}}},
        },
        {}};
    ChartEditor editor{std::move(draft)};
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.copy_selection());
    const auto& fragment = editor.clipboard();
    assert(fragment.base_row == 0U);
    assert(fragment_rows(fragment) == std::vector<std::uint64_t>({0U, 2U, 4U}));
    const auto& content = fragment.content;
    assert(content.dance_move_drills().size() == 1U);
    assert(content.dance_move_drills().front().move_n_count == 5U);
    assert(content.drill_groups().size() == 2U);
    assert(content.drill_groups()[0].id.value == 1U);
    assert(content.drill_groups()[0].npc_event_ids.size() == 2U);
    assert(content.drill_groups()[1].id.value == 3U);
    assert(content.drill_groups()[1].npc_event_ids.size() == 1U);
    assert(content.dance_move_drills().front().drill_group_ids.size() == 2U);
    assert(content.dance_move_drills().front().drill_group_ids[0].value == 1U);
    assert(content.dance_move_drills().front().drill_group_ids[1].value == 3U);
    for (const auto& group : content.drill_groups()) {
      assert(group.dance_move_drill_id.value ==
             content.dance_move_drills().front().id.value);
    }
  }

  // A copy without a selection fails and keeps the previous clipboard.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    const auto empty = editor.copy_selection();
    assert(empty.code == ChartEditCode::SelectionUnavailable);
    assert(!empty.changed);
    assert(editor.clipboard().content.role_events().empty());

    auto loaded = build_two_complete_moves();
    assert(loaded.select(ChartColumn::Npc, 0U).selection.active);
    assert(loaded.copy_selection());
    const auto kept = loaded.clipboard();
    assert(loaded.clear_selection());
    const auto refused = loaded.copy_selection();
    assert(refused.code == ChartEditCode::SelectionUnavailable);
    assert(!refused.changed);
    assert(otgy::core::chart_drafts_equal(loaded.clipboard().content,
                                          kept.content));
    assert(loaded.clipboard().base_row == kept.base_row);
  }
}

void test_selection_resolves_each_marker_type() {
  auto editor = build_two_complete_moves();
  assert(editor.click(ChartColumn::Showtime, 52U));
  const auto before = editor.draft();
  const auto history = editor.undo_count();
  assert(before.dance_move_drills().size() == 2U);
  const auto first_drill = before.dance_move_drills()[0].id;
  const auto second_drill = before.dance_move_drills()[1].id;

  const auto select_marker = [&](ChartMarkerType type, std::size_t occurrence,
                                 ChartColumn column) {
    const auto row = marker_of(editor.draft(), type, occurrence).row;
    const auto result = editor.select(column, row);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    return result;
  };

  const auto npc_start =
      select_marker(ChartMarkerType::NpcBlockStart, 0U, ChartColumn::Npc);
  assert(npc_start);
  assert(npc_start.changed);
  assert(npc_start.selection.active);
  assert(npc_start.selection.drill_id.value == first_drill.value);
  assert(npc_start.selection.first_row == 0U);
  assert(npc_start.selection.last_row == 14U);

  const auto npc_note =
      select_marker(ChartMarkerType::NpcNote, 0U, ChartColumn::Npc);
  assert(npc_note.selection.drill_id.value == first_drill.value);
  const auto npc_end =
      select_marker(ChartMarkerType::NpcMoveEnd, 0U, ChartColumn::Npc);
  assert(npc_end.selection.drill_id.value == first_drill.value);
  const auto player_start =
      select_marker(ChartMarkerType::PlayerBlockStart, 0U, ChartColumn::Player);
  assert(player_start.selection.drill_id.value == first_drill.value);
  const auto player_note =
      select_marker(ChartMarkerType::PlayerNote, 0U, ChartColumn::Player);
  assert(player_note.selection.drill_id.value == first_drill.value);
  const auto player_end =
      select_marker(ChartMarkerType::PlayerMoveEnd, 0U, ChartColumn::Player);
  assert(player_end.selection.drill_id.value == first_drill.value);

  const auto showtime_move =
      select_marker(ChartMarkerType::ShowtimeMove, 0U, ChartColumn::Showtime);
  assert(showtime_move.selection.drill_id.value == first_drill.value);
  assert(showtime_move.selection.first_row == 0U);
  assert(showtime_move.selection.last_row == 14U);
  const auto showtime_move_second =
      select_marker(ChartMarkerType::ShowtimeMove, 1U, ChartColumn::Showtime);
  assert(showtime_move_second.selection.drill_id.value == second_drill.value);
  assert(showtime_move_second.selection.first_row == 16U);
  assert(showtime_move_second.selection.last_row == 50U);

  // A Showtime start marker carries no dance move reference.
  const auto showtime_start =
      select_marker(ChartMarkerType::ShowtimeStart, 0U, ChartColumn::Showtime);
  assert(showtime_start.code == ChartEditCode::SelectionUnavailable);
  assert(!showtime_start.changed);
  assert(showtime_start.selection.drill_id.value == second_drill.value);

  // An empty row clears the selection.
  const auto empty = editor.select(ChartColumn::Npc, 5U);
  assert(empty);
  assert(empty.changed);
  assert(!empty.selection.active);

  // A row that holds a marker in another column also clears it.
  assert(editor.select(ChartColumn::Npc, 0U));
  const auto wrong_column = editor.select(ChartColumn::Player, 0U);
  assert(wrong_column);
  assert(wrong_column.changed);
  assert(!wrong_column.selection.active);

  // Clicking an empty row with no selection is a no-change success.
  const auto no_change = editor.select(ChartColumn::Npc, 5U);
  assert(no_change);
  assert(!no_change.changed);
  assert(!no_change.selection.active);
}

void test_selection_on_incomplete_draft() {
  // A dance move without its MoveEnd and without player content is selectable:
  // the draft is allowed to be incomplete.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.click(ChartColumn::Npc, 10U));
    assert(editor.draft().dance_move_drills().front().move_n_count == 1U);
    assert(editor.draft().drill_groups().front().player_event_ids.empty());
    const auto selected = editor.select(ChartColumn::Npc, 10U);
    assert(selected);
    assert(selected.selection.active);
    assert(selected.selection.first_row == 0U);
    assert(selected.selection.last_row == 10U);
    assert(selected.selection.drill_id.value ==
           editor.draft().dance_move_drills().front().id.value);
  }

  // A group whose id list was emptied by a deletion does not break the
  // selection: membership comes from the drill reference on the events.
  {
    auto editor = build_player_source();
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.draft().drill_groups().front().npc_event_ids.empty());
    const auto selected = editor.select(ChartColumn::Player, 50U);
    assert(selected);
    assert(selected.selection.active);
    assert(selected.selection.first_row == 50U);
    assert(selected.selection.last_row == 50U);
  }

  // One dance move may span several drill groups; the selection covers all of
  // them because it is bound to the drill rather than to one group.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.click(ChartColumn::Npc, 10U));
    assert(editor.click(ChartColumn::Player, 20U));
    assert(editor.click(ChartColumn::Npc, 30U));
    assert(editor.click(ChartColumn::Npc, 40U));
    assert(editor.click(ChartColumn::Npc, 40U));
    assert(editor.draft().dance_move_drills().size() == 1U);
    assert(editor.draft().dance_move_drills().front().drill_group_ids.size() ==
           2U);
    const auto selected = editor.select(ChartColumn::Npc, 30U);
    assert(selected);
    assert(selected.selection.active);
    assert(selected.selection.first_row == 0U);
    assert(selected.selection.last_row == 40U);
  }

  // A marker whose drill reference does not resolve cannot be selected, and the
  // failed command changes nothing.
  {
    const ChartRoleEvent dangling{{1U}, 0U, ChartRole::Npc,
                                  ChartRoleEventType::BlockStart, {3U}, {2U}};
    ChartEditor editor{ChartDraft{{dangling}, {}, {}, {}, {}, {}}};
    const auto before = editor.draft();
    const auto selected = editor.select(ChartColumn::Npc, 0U);
    assert(selected.code == ChartEditCode::SelectionUnavailable);
    assert(!selected.changed);
    assert(!selected.selection.active);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(!editor.can_undo());
  }

  // A selected dance move drops out of the selection once its last bound event
  // is deleted, and the leftover dance move object goes with it: with no event
  // left there is nothing to click, and the leftover would still fail the
  // finished-chart rules even without any Showtime.
  {
    auto editor = build_player_source();
    assert(editor.select(ChartColumn::Player, 50U).selection.active);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selection().active);
    assert(editor.click(ChartColumn::Player, 50U));
    assert(!editor.selection().active);
    assert(editor.draft().dance_move_drills().empty());
    assert(editor.draft().drill_groups().empty());
  }
}

void test_selection_lifetime_across_commands() {
  // An unrelated insertion keeps the selection; history navigation re-derives
  // the span and never clears the redo stack.
  {
    auto editor = build_two_complete_moves();
    const auto first_drill = editor.draft().dance_move_drills()[0].id;
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.selection().drill_id.value == first_drill.value);

    const auto history = editor.undo_count();
    assert(editor.click(ChartColumn::Npc, 100U));
    assert(editor.undo_count() == history + 1U);
    assert(editor.selection().active);
    assert(editor.selection().drill_id.value == first_drill.value);
    assert(editor.selection().first_row == 0U);
    assert(editor.selection().last_row == 14U);

    assert(editor.undo());
    assert(editor.can_redo());
    assert(editor.selection().active);
    assert(editor.select(ChartColumn::Player, 8U).selection.active);
    assert(editor.can_redo());
    assert(editor.redo());
    assert(editor.selection().drill_id.value == first_drill.value);
  }

  // Deleting one role's events shrinks the span to the surviving role.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U).selection.active);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selection().active);
    assert(editor.selection().first_row == 8U);
    assert(editor.selection().last_row == 14U);
    assert(editor.undo());
    assert(editor.selection().active);
    assert(editor.selection().first_row == 0U);
    assert(editor.selection().last_row == 14U);
  }

  // Undo restores the draft but not the selection: the selection is view state,
  // not a history entry.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Player, 8U).selection.active);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selection().active);
    assert(editor.click(ChartColumn::Player, 8U));
    assert(!editor.selection().active);
    assert(editor.undo());
    assert(!editor.selection().active);
  }

  // clear() empties the draft and the selection.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 16U).selection.active);
    assert(editor.clear());
    assert(!editor.selection().active);
  }

  // Session commands on an empty editor succeed without touching history.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    const auto cleared = editor.clear_selection();
    assert(cleared);
    assert(!cleared.changed);
    assert(!cleared.selection.active);
    assert(!editor.can_undo());
    assert(!editor.can_redo());
    const auto empty = editor.select(ChartColumn::Npc, 0U);
    assert(empty);
    assert(!empty.changed);
    assert(!editor.selection().active);
    assert(!editor.can_undo());
  }
}

[[nodiscard]] ChartEditor build_two_complete_moves() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(editor.click(ChartColumn::Npc, 0U));
  assert(editor.click(ChartColumn::Npc, 2U));
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(editor.click(ChartColumn::Npc, 6U));
  assert(editor.click(ChartColumn::Npc, 6U));
  assert(editor.click(ChartColumn::Player, 8U));
  assert(editor.click(ChartColumn::Player, 10U));

  assert(editor.click(ChartColumn::Npc, 16U));
  for (std::uint64_t row = 18U; row <= 32U; row += 2U) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  assert(editor.click(ChartColumn::Npc, 32U));
  assert(editor.click(ChartColumn::Player, 34U));
  assert(editor.click(ChartColumn::Player, 36U));
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.draft().dance_move_drills()[0].move_n_count == 2U);
  assert(editor.draft().dance_move_drills()[1].move_n_count == 7U);
  return editor;
}

void test_showtime_completion_layout_snapshot_and_deletion() {
  auto editor = build_two_complete_moves();
  const auto before = editor.draft();
  const auto history = editor.undo_count();
  const auto completion = editor.click(ChartColumn::Showtime, 52U);
  assert(completion);
  assert(editor.undo_count() == history + 1U);
  assert(editor.draft().showtime_start_events().size() == 1U);
  assert(editor.draft().showtime_move_events().size() == 2U);
  assert(editor.draft().showtimes().size() == 1U);
  assert(marker_of(editor.draft(), ChartMarkerType::ShowtimeStart).row == 52U);
  const auto first_d = marker_of(editor.draft(), ChartMarkerType::ShowtimeMove);
  const auto second_d =
      marker_of(editor.draft(), ChartMarkerType::ShowtimeMove, 1U);
  assert(first_d.row == 64U && first_d.show_n_count == 2U);
  assert(second_d.row == 80U && second_d.show_n_count == 7U);
  const auto ranges = otgy::core::project_showtime_moves(editor.draft());
  assert(ranges);
  assert(ranges.moves()->at(0U).range_end == 80U);
  assert(ranges.moves()->at(1U).range_end == 112U);

  const auto generated = editor.draft();
  // Editing a dance move the Showtime was generated from leaves that Showtime
  // stale: the whole segment goes in the same edit, and one undo brings the
  // edit and the segment back together.
  const auto edit_history = editor.undo_count();
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(editor.undo_count() == edit_history + 1U);
  assert(editor.draft().showtime_start_events().empty());
  assert(editor.draft().showtime_move_events().empty());
  assert(editor.draft().showtimes().empty());
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));

  const auto no_more = editor.click(ChartColumn::Showtime, 112U);
  assert(no_more.code == ChartEditCode::NoRemainingDanceMoves);
  assert(!no_more.changed);

  assert(editor.click(ChartColumn::Showtime, first_d.row));
  assert(editor.draft().showtime_move_events().empty());
  assert(editor.draft().showtime_start_events().size() == 1U);
  assert(editor.draft().showtimes().front().show_move_event_ids.empty());
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));

  assert(editor.click(ChartColumn::Showtime, second_d.row));
  assert(editor.draft().showtime_move_events().size() == 1U);
  assert(editor.click(ChartColumn::Showtime, 80U));
  assert(editor.draft().showtime_move_events().size() == 2U);
  assert(marker_of(editor.draft(), ChartMarkerType::ShowtimeMove, 1U)
             .show_n_count == 7U);
  assert(editor.undo());
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));

  assert(editor.click(ChartColumn::Showtime, 52U));
  assert(editor.draft().showtime_start_events().empty());
  assert(editor.draft().showtime_move_events().empty());
  assert(editor.draft().showtimes().empty());
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), generated));
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
}

void test_showtime_conflicts_position_and_retained_start() {
  auto conflict = build_two_complete_moves();
  assert(conflict.click(ChartColumn::Npc, 96U));
  const auto history = conflict.undo_count();
  const auto blocked = conflict.click(ChartColumn::Showtime, 52U);
  assert(blocked.code == ChartEditCode::ShowtimeRangeOccupied);
  assert(blocked.changed);
  assert(conflict.undo_count() == history + 1U);
  assert(conflict.draft().showtime_start_events().size() == 1U);
  assert(conflict.draft().showtime_move_events().empty());
  const auto invalid = conflict.click(ChartColumn::Showtime, 53U);
  assert(invalid.code == ChartEditCode::InvalidShowtimePosition);
  assert(!invalid.changed);
  assert(conflict.undo());
  assert(conflict.draft().showtime_start_events().empty());

  auto no_moves = build_player_source();
  assert(no_moves.click(ChartColumn::Player, 100U));
  assert(no_moves.click(ChartColumn::Npc, 25U));
  const auto no_moves_history = no_moves.undo_count();
  const auto retained = no_moves.click(ChartColumn::Showtime, 132U);
  assert(retained.code == ChartEditCode::NoRemainingDanceMoves);
  assert(retained.changed);
  assert(no_moves.undo_count() == no_moves_history + 1U);
  assert(no_moves.draft().showtime_start_events().size() == 1U);
  assert(no_moves.draft().showtime_move_events().empty());
}

void test_showtime_manual_position_boundaries() {
  const ChartDraft measure_start{
      {}, {{{1U}, 0U, {10U}}}, {}, {}, {},
      {{{10U}, {1U}, {}, {}}}};
  ChartEditor invalid_first{measure_start};
  assert(invalid_first.click(ChartColumn::Showtime, 2U).code ==
         ChartEditCode::InvalidShowtimePosition);
  ChartEditor plus_one{measure_start};
  assert(plus_one.click(ChartColumn::Showtime, 1U).code ==
         ChartEditCode::NoRemainingDanceMoves);
  ChartEditor later_measure{measure_start};
  assert(later_measure.click(ChartColumn::Showtime, 16U).code ==
         ChartEditCode::NoRemainingDanceMoves);

  const ChartDraft non_measure_start{
      {}, {{{1U}, 3U, {10U}}}, {}, {}, {},
      {{{10U}, {1U}, {}, {}}}};
  ChartEditor invalid_non_measure{non_measure_start};
  assert(invalid_non_measure.click(ChartColumn::Showtime, 4U).code ==
         ChartEditCode::InvalidShowtimePosition);
  ChartEditor next_measure{non_measure_start};
  assert(next_measure.click(ChartColumn::Showtime, 16U).code ==
         ChartEditCode::NoRemainingDanceMoves);

  const ChartDraft long_first_move{
      {},
      {{{1U}, 0U, {10U}}},
      {{{2U}, 1U, {10U}, {20U}, 7U}},
      {},
      {},
      {{{10U}, {1U}, {{20U}}, {{2U}}}}};
  ChartEditor inside_previous_range{long_first_move};
  assert(inside_previous_range.click(ChartColumn::Showtime, 16U).code ==
         ChartEditCode::InvalidShowtimePosition);
}

void test_showtime_final_open_range_exceptions() {
  auto base = build_two_complete_moves();
  assert(base.click(ChartColumn::Showtime, 52U));
  const auto completed = base.draft();

  ChartEditor first_range{completed};
  assert(first_range.click(ChartColumn::Npc, 72U).code ==
         ChartEditCode::ShowtimeRangeOccupied);
  ChartEditor last_closed{completed};
  assert(last_closed.click(ChartColumn::Npc, 96U).code ==
         ChartEditCode::ShowtimeRangeOccupied);
  ChartEditor final_open{completed};
  assert(final_open.click(ChartColumn::Npc, 104U));
  assert(marker_of(final_open.draft(), ChartMarkerType::NpcBlockStart, 2U)
             .row == 104U);
  ChartEditor range_end{completed};
  assert(range_end.click(ChartColumn::Npc, 112U));

  auto allowed_during_completion = build_two_complete_moves();
  assert(allowed_during_completion.click(ChartColumn::Npc, 104U));
  assert(allowed_during_completion.click(ChartColumn::Showtime, 52U));
  assert(allowed_during_completion.draft().showtime_move_events().size() ==
         2U);
}

/// A chart whose two complete dance moves are far apart, with a Showtime that
/// was generated from both. The gap is wide enough to insert a third complete
/// dance move in front of the second one.
[[nodiscard]] ChartEditor build_showtime_over_a_gap() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(editor.click(ChartColumn::Npc, 0U));
  assert(editor.click(ChartColumn::Npc, 2U));
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(editor.click(ChartColumn::Npc, 6U));
  assert(editor.click(ChartColumn::Npc, 6U));
  assert(editor.click(ChartColumn::Player, 8U));
  assert(editor.click(ChartColumn::Player, 10U));

  assert(editor.click(ChartColumn::Npc, 40U));
  assert(editor.click(ChartColumn::Npc, 42U));
  assert(editor.click(ChartColumn::Npc, 44U));
  assert(editor.click(ChartColumn::Npc, 46U));
  assert(editor.click(ChartColumn::Npc, 48U));
  assert(editor.click(ChartColumn::Npc, 48U));
  assert(editor.click(ChartColumn::Player, 50U));
  assert(editor.click(ChartColumn::Player, 52U));

  assert(editor.click(ChartColumn::Showtime, 64U));
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.draft().showtimes().size() == 1U);
  assert(editor.draft().showtime_move_events().size() == 2U);
  return editor;
}

/// Adds a complete dance move in [first_row, first_row + 14].
void add_dance_move(ChartEditor& editor, std::uint64_t first_row) {
  assert(editor.click(ChartColumn::Npc, first_row));
  assert(editor.click(ChartColumn::Npc, first_row + 2U));
  assert(editor.click(ChartColumn::Npc, first_row + 4U));
  assert(editor.click(ChartColumn::Npc, first_row + 6U));
  assert(editor.click(ChartColumn::Npc, first_row + 6U));
  assert(editor.click(ChartColumn::Player, first_row + 8U));
  assert(editor.click(ChartColumn::Player, first_row + 10U));
}

/// A Showtime is deleted together with the edit that makes it stale, because the
/// cleanup runs inside the single conversion every draft-changing command ends
/// with. A Showtime that still holds consistent content is never touched, even
/// when it can no longer be extended.
void test_stale_showtime_pruning() {
  // (a) A Showtime generated from two dance moves goes away with the edit that
  // makes one of them incomplete, as part of the same history entry, and what
  // is left is exactly the draft the same edit produces without a Showtime.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    const auto generated = editor.draft();
    assert(generated.showtimes().size() == 1U);
    const auto history = editor.undo_count();

    // Deleting the whole NPC block of the second dance move drops its note
    // count to zero, so the dance move is no longer complete.
    assert(editor.click(ChartColumn::Npc, 16U));
    assert(editor.undo_count() == history + 1U);
    const auto after = editor.draft();
    assert(after.showtimes().empty());
    assert(after.showtime_start_events().empty());
    assert(after.showtime_move_events().empty());
    // Only the Showtime left: the dance move keeps its player events and is not
    // removed, so its drill and drill group stay in the draft.
    assert(after.dance_move_drills().size() == 2U);
    assert(after.drill_groups().size() == 2U);

    auto plain = build_two_complete_moves();
    assert(plain.click(ChartColumn::Npc, 16U));
    assert(otgy::core::chart_drafts_equal(after, plain.draft()));

    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), generated));
    assert(editor.redo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), after));
  }

  // (b) With two Showtimes only the stale one goes. The surviving Showtime is
  // left value-for-value as it was, and the result matches a chart in which the
  // second Showtime was never placed.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    add_dance_move(editor, 112U);
    const auto drafts = editor.draft();
    assert(drafts.showtime_start_events().size() == 1U);
    // The third dance move starts after the first Showtime's display range, so
    // a second Showtime is placed for it alone.
    assert(editor.click(ChartColumn::Showtime, 144U));
    const auto both = editor.draft();
    assert(both.showtime_start_events().size() == 2U);
    assert(both.showtime_move_events().size() == 3U);
    assert(both.showtimes().size() == 2U);
    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), drafts));
    assert(editor.redo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), both));

    const auto first_start = both.showtimes().front().start_event_id.value;
    assert(editor.click(ChartColumn::Npc, 112U));
    const auto after = editor.draft();
    assert(after.showtime_start_events().size() == 1U);
    assert(after.showtime_move_events().size() == 2U);
    assert(after.showtimes().size() == 1U);
    assert(after.showtimes().front().start_event_id.value == first_start);
    assert(after.showtimes().front().dance_move_drill_ids.size() == 2U);
    assert(after.showtimes().front().show_move_event_ids.size() == 2U);
    assert(after.dance_move_drills().size() == 3U);

    auto plain = build_two_complete_moves();
    assert(plain.click(ChartColumn::Showtime, 52U));
    add_dance_move(plain, 112U);
    assert(plain.click(ChartColumn::Npc, 112U));
    assert(otgy::core::chart_drafts_equal(after, plain.draft()));
  }

  // (c) A stored SHOW-D count that no longer matches its dance move is stale on
  // its own. Only a persisted or imported draft can hold that state, so the
  // test builds it directly. The cleanup runs when a command commits, and never
  // when history is replayed: undo brings the stored draft back verbatim.
  {
    auto source = build_two_complete_moves();
    assert(source.click(ChartColumn::Showtime, 52U));
    const auto generated = source.draft();
    auto moves = generated.showtime_move_events();
    assert(moves.front().show_n_count == 2U);
    moves.front().show_n_count = 11U;
    ChartEditor editor{ChartDraft{generated.role_events(),
                                 generated.showtime_start_events(),
                                 std::move(moves), generated.drill_groups(),
                                 generated.dance_move_drills(),
                                 generated.showtimes()}};
    assert(editor.draft().showtimes().size() == 1U);

    assert(editor.click(ChartColumn::Npc, 200U));
    assert(editor.draft().showtimes().empty());
    assert(editor.draft().showtime_start_events().empty());
    assert(editor.draft().showtime_move_events().empty());
    assert(editor.undo());
    assert(editor.draft().showtimes().size() == 1U);
    assert(editor.draft().showtime_move_events().size() == 2U);
    assert(editor.redo());
    assert(editor.draft().showtimes().empty());
  }

  // (d) A Showtime whose candidate dance moves moved on is not stale: its own
  // content is still consistent, so it stays and only continuing it is refused.
  {
    auto editor = build_showtime_over_a_gap();
    // A third complete dance move lands in front of the second one, which
    // shifts the candidate sequence the Showtime was generated from.
    add_dance_move(editor, 16U);
    assert(editor.draft().showtimes().size() == 1U);
    assert(editor.draft().showtime_start_events().size() == 1U);
    assert(editor.draft().showtime_move_events().size() == 2U);
    const auto before = editor.draft();
    const auto history = editor.undo_count();

    const auto refused = editor.click(ChartColumn::Showtime, 112U);
    assert(refused.code == ChartEditCode::ShowtimeContentStale);
    assert(!refused.changed);
    assert(editor.undo_count() == history);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  }

  // (e) An imported Showtime may carry fewer move markers than dance moves.
  // It is equally kept: only continuing it is refused.
  {
    auto source = build_two_complete_moves();
    assert(source.click(ChartColumn::Showtime, 52U));
    const auto generated = source.draft();
    auto moves = generated.showtime_move_events();
    auto showtimes = generated.showtimes();
    assert(moves.size() == 2U && showtimes.front().show_move_event_ids.size() == 2U);
    moves.erase(moves.begin() + 1);
    showtimes.front().show_move_event_ids.erase(
        showtimes.front().show_move_event_ids.begin() + 1);
    ChartEditor editor{ChartDraft{generated.role_events(),
                                 generated.showtime_start_events(),
                                 std::move(moves), generated.drill_groups(),
                                 generated.dance_move_drills(),
                                 std::move(showtimes)}};
    assert(editor.draft().showtimes().front().dance_move_drill_ids.size() == 2U);
    assert(editor.draft().showtimes().front().show_move_event_ids.size() == 1U);

    assert(editor.click(ChartColumn::Npc, 200U));
    assert(editor.draft().showtimes().size() == 1U);
    const auto before = editor.draft();
    const auto refused = editor.click(ChartColumn::Showtime, 128U);
    assert(refused.code == ChartEditCode::ShowtimeContentStale);
    assert(!refused.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  }

  // (f) clear() still empties the whole chart, Showtime included, in one entry.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    const auto generated = editor.draft();
    assert(editor.clear());
    assert(editor.draft().role_events().empty());
    assert(editor.draft().drill_groups().empty());
    assert(editor.draft().dance_move_drills().empty());
    assert(editor.draft().showtime_start_events().empty());
    assert(editor.draft().showtime_move_events().empty());
    assert(editor.draft().showtimes().empty());
    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), generated));
  }
}

void test_clear_and_invalid_identity() {
  auto editor = build_player_source();
  const auto before = editor.draft();
  assert(editor.clear());
  assert(editor.draft().role_events().empty());
  assert(editor.draft().drill_groups().empty());
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  assert(editor.redo());
  assert(editor.clear().code == ChartEditCode::NoChange);

  const ChartRoleEvent first{{1U}, 0U, ChartRole::Npc,
                             ChartRoleEventType::BlockStart, {3U}, {2U}};
  const ChartRoleEvent duplicate_row{{2U}, 0U, ChartRole::Player,
                                     ChartRoleEventType::BlockStart, {3U},
                                     {2U}};
  ChartEditor invalid{ChartDraft{{first, duplicate_row}, {}, {}, {}, {}, {}}};
  assert(invalid.click(ChartColumn::Npc, 5U).code ==
         ChartEditCode::InvalidDraft);
  assert(!invalid.can_undo());

  const ChartRoleEvent exhausted{
      {std::numeric_limits<std::uint64_t>::max()},
      0U,
      ChartRole::Npc,
      ChartRoleEventType::BlockStart,
      {3U},
      {2U}};
  ChartEditor no_ids{ChartDraft{{exhausted}, {}, {},
                               {{{3U}, {2U}, {{exhausted.id}}, {}}},
                               {{{2U}, 0U, {{3U}}}}, {}}};
  assert(no_ids.click(ChartColumn::Npc, 10U).code ==
         ChartEditCode::IdExhausted);
}

void test_empty_group_does_not_block_showtime() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  for (const auto row : {2U, 3U, 4U, 5U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.click(ChartColumn::Player, 7U));
  for (const auto row : {10U, 11U, 12U, 12U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  assert(editor.click(ChartColumn::Player, 13U));
  assert(editor.click(ChartColumn::Player, 14U));
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(editor.draft().drill_groups().size() == 2U);
  assert(editor.click(ChartColumn::Showtime, 16U));
  assert(editor.draft().showtime_move_events().size() == 1U);

  // Removing the NPC block keeps the group's surviving player block. Removing
  // that last block must also drop its now unclickable group and drill reference.
  assert(editor.click(ChartColumn::Npc, 2U));
  assert(editor.draft().showtimes().empty());
  assert(editor.draft().drill_groups().size() == 2U);
  const auto before = editor.draft();
  const auto history = editor.undo_count();
  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.undo_count() == history + 1U);
  assert(editor.draft().drill_groups().size() == 1U);
  assert(editor.draft().dance_move_drills().front().drill_group_ids.size() == 1U);
  const auto remaining = editor.draft();
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  assert(editor.redo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), remaining));
  assert(editor.click(ChartColumn::Showtime, 16U));
  assert(editor.draft().showtime_move_events().size() == 1U);
  assert(editor.draft().showtime_move_events().front().show_n_count == 1U);

  // Older recovery snapshots can already carry the leftover. Decoding must
  // preserve that snapshot; the next successful edit clears the empty group.
  auto groups = remaining.drill_groups();
  groups.insert(groups.begin(), {{999U}, groups.front().dance_move_drill_id,
                                 {}, {}});
  auto drills = remaining.dance_move_drills();
  drills.front().drill_group_ids.insert(drills.front().drill_group_ids.begin(),
                                      {999U});
  ChartDraft legacy{remaining.role_events(), {}, {}, std::move(groups),
                    std::move(drills), {}};
  ChartEditor recovered{legacy};
  const auto placed = recovered.click(ChartColumn::Showtime, 16U);
  assert(placed.changed);
  assert(placed.code == ChartEditCode::Success);
  assert(recovered.draft().drill_groups().size() == 1U);
  assert(recovered.draft().showtime_move_events().size() == 1U);
  assert(recovered.undo());
  assert(otgy::core::chart_drafts_equal(recovered.draft(), legacy));
  const auto failed = recovered.click(ChartColumn::Player, 10U);
  assert(!failed.changed);
  assert(otgy::core::chart_drafts_equal(recovered.draft(), legacy));
  assert(recovered.redo());
  assert(recovered.draft().showtime_move_events().size() == 1U);

  // Empty reference lists do not make a group eventless: bound events still
  // belong to an incomplete draft and must survive an unrelated successful edit.
  auto incomplete_groups = remaining.drill_groups();
  incomplete_groups.front().npc_event_ids.clear();
  incomplete_groups.front().player_event_ids.clear();
  ChartDraft incomplete{remaining.role_events(), {}, {},
                        std::move(incomplete_groups),
                        remaining.dance_move_drills(), {}};
  ChartEditor retained{incomplete};
  assert(retained.click(ChartColumn::Npc, 32U));
  assert(retained.draft().drill_groups().size() == 2U);
  assert(retained.draft().drill_groups().front().id.value ==
         incomplete.drill_groups().front().id.value);
  const auto& retained_ids =
      retained.draft().dance_move_drills().front().drill_group_ids;
  const auto& original_ids =
      incomplete.dance_move_drills().front().drill_group_ids;
  assert(retained_ids.size() == original_ids.size());
  for (std::size_t i = 0U; i < original_ids.size(); ++i) {
    assert(retained_ids[i].value == original_ids[i].value);
  }
  assert(retained.undo());
  assert(otgy::core::chart_drafts_equal(retained.draft(), incomplete));
}

void test_result_messages() {
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(ChartEditCode::ShowtimeContentStale);
       ++raw) {
    assert(!otgy::core::chart_edit_code_message(
                static_cast<ChartEditCode>(raw))
                .empty());
  }
  assert(otgy::core::chart_edit_code_message(ChartEditCode::Success).empty());
  assert(!otgy::core::chart_edit_code_message(
              static_cast<ChartEditCode>(255))
              .empty());
}

}  // namespace

int main() {
  test_npc_context_rules_and_history();
  test_npc_limit_insertion_and_block_deletion();
  test_player_generation_is_atomic_and_detached();
  test_player_failures_preserve_draft_and_history();
  test_showtime_completion_layout_snapshot_and_deletion();
  test_showtime_conflicts_position_and_retained_start();
  test_showtime_manual_position_boundaries();
  test_showtime_final_open_range_exceptions();
  test_stale_showtime_pruning();
  test_empty_group_does_not_block_showtime();
  test_clear_and_invalid_identity();
  test_selection_resolves_each_marker_type();
  test_selection_on_incomplete_draft();
  test_selection_lifetime_across_commands();
  test_copy_selection_fragment();
  test_paste_is_independent_and_atomic();
  test_paste_failures_preserve_state();
  test_paste_round_trip_through_formal_payload();
  test_cut_selection_removes_exactly_its_own_objects();
  test_result_messages();
  return 0;
}
