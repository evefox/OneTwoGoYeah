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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().drill_id.value == first_drill.value);
    assert(editor.selections().front().first_row == 200U);
    assert(editor.selections().front().last_row == 214U);

    const auto second = editor.paste(300U);
    assert(second);
    assert(editor.undo_count() == history + 2U);
    assert(editor.draft().dance_move_drills().size() == 4U);
    const auto second_drill = editor.draft().dance_move_drills()[3].id;
    assert(drill_rows(editor.draft(), second_drill) ==
           std::vector<std::uint64_t>({300U, 302U, 304U, 306U, 308U, 310U,
                                       312U, 314U}));
    const auto& after = editor.draft();
    const auto source_ids = drill_event_ids(after, source_drill);
    const auto first_ids = drill_event_ids(after, first_drill);
    const auto second_ids = drill_event_ids(after, second_drill);
    const auto assert_disjoint = [](const auto& left, const auto& right) {
      for (const auto id : left) {
        assert(std::find(right.begin(), right.end(), id) == right.end());
      }
    };
    assert_disjoint(source_ids, first_ids);
    assert_disjoint(source_ids, second_ids);
    assert_disjoint(first_ids, second_ids);
    assert(source_drill.value != first_drill.value);
    assert(source_drill.value != second_drill.value);
    assert(first_drill.value != second_drill.value);

    const auto group_ids = [&](std::size_t drill_index) {
      std::vector<std::uint64_t> ids;
      for (const auto id : after.dance_move_drills()[drill_index].drill_group_ids) {
        ids.push_back(id.value);
      }
      return ids;
    };
    const auto source_groups = group_ids(0U);
    const auto first_groups = group_ids(2U);
    const auto second_groups = group_ids(3U);
    assert_disjoint(source_groups, first_groups);
    assert_disjoint(source_groups, second_groups);
    assert_disjoint(first_groups, second_groups);

    // Each copied relationship points inside its own pasted move. Compare
    // both role vectors through the event-ID mapping, not only marker rows.
    const auto assert_relationships = [&](std::size_t drill_index,
                                          std::uint64_t anchor) {
      const auto& source = after.dance_move_drills()[0];
      const auto& pasted = after.dance_move_drills()[drill_index];
      assert(pasted.move_n_count == source.move_n_count);
      assert(pasted.drill_group_ids.size() == source.drill_group_ids.size());
      std::vector<const ChartRoleEvent*> source_events;
      std::vector<const ChartRoleEvent*> pasted_events;
      for (const auto& event : after.role_events()) {
        if (event.dance_move_drill_id.value == source.id.value) {
          source_events.push_back(&event);
        }
        if (event.dance_move_drill_id.value == pasted.id.value) {
          pasted_events.push_back(&event);
        }
      }
      assert(pasted_events.size() == source_events.size());
      const auto base = source_events.front()->row;
      for (std::size_t index = 0U; index < source_events.size(); ++index) {
        const auto& original = *source_events[index];
        const auto& copy = *pasted_events[index];
        assert(copy.row == anchor + original.row - base);
        assert(copy.role == original.role);
        assert(copy.type == original.type);
        const auto group = std::find_if(
            source.drill_group_ids.begin(), source.drill_group_ids.end(),
            [&](const auto id) { return id.value == original.drill_group_id.value; });
        assert(group != source.drill_group_ids.end());
        const auto group_index = static_cast<std::size_t>(
            group - source.drill_group_ids.begin());
        assert(copy.drill_group_id.value == pasted.drill_group_ids[group_index].value);
      }
      for (std::size_t index = 0U; index < source.drill_group_ids.size(); ++index) {
        const auto original = std::find_if(
            after.drill_groups().begin(), after.drill_groups().end(),
            [&](const auto& group) {
              return group.id.value == source.drill_group_ids[index].value;
            });
        const auto copy = std::find_if(
            after.drill_groups().begin(), after.drill_groups().end(),
            [&](const auto& group) {
              return group.id.value == pasted.drill_group_ids[index].value;
            });
        assert(original != after.drill_groups().end());
        assert(copy != after.drill_groups().end());
        assert(copy->dance_move_drill_id.value == pasted.id.value);
        const auto assert_event_mapping = [&](const auto& original_ids,
                                               const auto& copied_ids) {
          assert(copied_ids.size() == original_ids.size());
          for (std::size_t event_index = 0U; event_index < original_ids.size();
               ++event_index) {
            const auto event = std::find_if(
                source_events.begin(), source_events.end(),
                [&](const auto* value) {
                  return value->id.value == original_ids[event_index].value;
                });
            assert(event != source_events.end());
            const auto mapped_index = static_cast<std::size_t>(event - source_events.begin());
            assert(copied_ids[event_index].value ==
                   pasted_events[mapped_index]->id.value);
          }
        };
        assert_event_mapping(original->npc_event_ids, copy->npc_event_ids);
        assert_event_mapping(original->player_event_ids, copy->player_event_ids);
      }
    };
    assert_relationships(2U, 200U);
    assert_relationships(3U, 300U);

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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto blocked_preview = editor.preview_paste(100U);
    assert(blocked_preview.code == ChartEditCode::ShowtimeRangeOccupied);
    assert(!blocked_preview.spans.empty());
    assert(!blocked_preview.ghost_markers.empty());
    const auto blocked = editor.paste(100U);
    assert(blocked.code == blocked_preview.code);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto exhausted_preview = editor.preview_paste(200U);
    assert(exhausted_preview.code == ChartEditCode::IdExhausted);
    assert(exhausted_preview.spans.size() == 1U);
    assert(exhausted_preview.ghost_markers.size() == 2U);
    const auto exhausted = editor.paste(200U);
    assert(exhausted.code == exhausted_preview.code);
    assert(!exhausted.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(editor.has_clipboard());
    assert(editor.selections().size() == 1U);
  }

  // A refusal is not an edit, so a redo branch that was already there stays
  // replayable and still restores the same draft.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.copy_selection());
    assert(editor.click(ChartColumn::Npc, 208U));
    const auto edited = editor.draft();
    assert(editor.undo());
    assert(editor.can_redo());
    const auto history = editor.undo_count();
    const auto redo_count = editor.redo_count();
    const auto before = editor.draft();

    assert(editor.paste(8U).code == ChartEditCode::PasteRangeOccupied);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(editor.redo_count() == redo_count);
    assert(editor.can_redo());
    assert(editor.redo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), edited));
  }

  // A refusal leaves the clipboard content itself intact: pasting the same
  // fragment after the refusal produces exactly the draft an editor that never
  // failed produces, identifiers included.
  {
    auto editor = build_two_complete_moves();
    auto control = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.copy_selection());
    assert(control.select(ChartColumn::Npc, 0U));
    assert(control.selections().size() == 1U);
    assert(control.copy_selection());

    assert(editor.paste(8U).code == ChartEditCode::PasteRangeOccupied);
    assert(editor.paste(300U));
    assert(control.paste(300U));
    assert(otgy::core::chart_drafts_equal(editor.draft(), control.draft()));
  }

  // An exhaustion found after some of the new identifiers were already handed
  // out is still a full refusal: the draft, the history, the clipboard and the
  // allocation cursor all stay put, so the next command allocates exactly the
  // identifiers it allocates in an editor that never failed.
  {
    constexpr std::uint64_t kNearEnd =
        std::numeric_limits<std::uint64_t>::max() - 2U;
    ChartDraft draft{
        std::vector<ChartRoleEvent>{
            {{1U}, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart, {1U}, {1U}},
            {{2U}, 2U, ChartRole::Npc, ChartRoleEventType::Note, {1U}, {1U}},
            {{kNearEnd}, 10U, ChartRole::Npc, ChartRoleEventType::BlockStart,
             {2U}, {2U}},
        },
        {},
        {},
        std::vector<otgy::core::ChartDrillGroup>{
            {{1U}, {1U}, {{1U}, {2U}}, {}},
            {{2U}, {2U}, {{kNearEnd}}, {}},
        },
        std::vector<otgy::core::ChartDanceMoveDrill>{
            {{1U}, 1U, {{1U}}},
            {{2U}, 1U, {{2U}}},
        },
        {}};
    ChartEditor editor{draft};
    ChartEditor control{draft};
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.copy_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    // The fragment needs a dance move, a drill group and two events, but only
    // two identifiers are left, so the refusal lands after the first two.
    const auto exhausted_preview = editor.preview_paste(200U);
    assert(exhausted_preview.code == ChartEditCode::IdExhausted);
    assert(exhausted_preview.spans.size() == 1U);
    assert(exhausted_preview.ghost_markers.size() == 2U);
    const auto exhausted = editor.paste(200U);
    assert(exhausted.code == exhausted_preview.code);
    assert(!exhausted.changed);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(!editor.can_redo());
    assert(editor.has_clipboard());
    assert(editor.selections().size() == 1U);

    assert(editor.click(ChartColumn::Npc, 20U));
    assert(control.click(ChartColumn::Npc, 20U));
    assert(otgy::core::chart_drafts_equal(editor.draft(), control.draft()));
  }
}

void test_paste_round_trip_through_formal_payload() {
  auto editor = build_two_complete_moves();
  assert(editor.select(ChartColumn::Npc, 16U));
  assert(editor.selections().size() == 1U);
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

/// Cut preparation retains the chart; only a successful destination commits the
/// move and its dependent Showtime cleanup as one undoable edit.
void test_cut_selection_removes_exactly_its_own_objects() {
  {
    auto editor = build_two_complete_moves();
    const auto before = editor.draft();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    const auto source_id = editor.selections().front().drill_id;
    const auto history = editor.undo_count();
    const otgy::core::ChartSessionResult cut = editor.cut_selection();
    assert(cut && cut.changed);
    assert(editor.has_pending_cut());
    assert(editor.undo_count() == history);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.selections().front().drill_id.value == source_id.value);
    assert(fragment_rows(editor.clipboard()).front() == 0U);

    const auto moved = editor.paste(200U);
    assert(moved && moved.changed);
    assert(!editor.has_pending_cut());
    assert(editor.undo_count() == history + 1U);
    assert(editor.draft().dance_move_drills().size() == 2U);
    assert(editor.draft().dance_move_drills().front().id.value ==
           before.dance_move_drills()[1].id.value);
    assert(drill_rows(editor.draft(), source_id).empty());
    assert(editor.selections().size() == 1U);
    assert(drill_rows(editor.draft(), editor.selections().front().drill_id) ==
           std::vector<std::uint64_t>({200U, 202U, 204U, 206U, 208U, 210U,
                                       212U, 214U}));
    for (const auto& group : editor.draft().drill_groups()) {
      assert(group.dance_move_drill_id.value != source_id.value);
    }
    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(!editor.has_pending_cut());
    assert(editor.has_clipboard());
    assert(editor.redo());
    assert(editor.draft().dance_move_drills().size() == 2U);
  }

  // The first move invalidates the source Showtime before checking its rows.
  {
    auto editor = build_two_complete_moves();
    assert(editor.click(ChartColumn::Showtime, 52U));
    const auto before = editor.draft();
    assert(editor.select(ChartColumn::Npc, 16U));
    assert(editor.selections().size() == 1U);
    const auto source_id = editor.selections().front().drill_id;
    const auto history = editor.undo_count();
    assert(editor.cut_selection());
    assert(editor.draft().showtimes().size() == 1U);
    const auto preview = editor.preview_paste(64U);
    assert(preview);
    assert(preview.last_row == 98U);
    assert(editor.paste(64U));
    assert(editor.undo_count() == history + 1U);
    assert(editor.draft().dance_move_drills().size() == 2U);
    assert(editor.draft().showtimes().empty());
    assert(editor.draft().showtime_start_events().empty());
    assert(editor.draft().showtime_move_events().empty());
    assert(drill_rows(editor.draft(), source_id).empty());
    assert(editor.undo());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(!editor.has_pending_cut());
    // Undo restores sources and Showtime, but the clipboard is now a copy.
    assert(editor.paste(200U));
    assert(editor.draft().dance_move_drills().size() == 3U);
    assert(editor.draft().showtimes().size() == 1U);
  }

  // Missing selection does not discard the previous pending cut or its data.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.cut_selection());
    const auto clipboard = editor.clipboard();
    assert(editor.clear_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    const auto refused = editor.cut_selection();
    assert(refused.code == ChartEditCode::SelectionUnavailable);
    assert(!refused.changed);
    assert(editor.has_pending_cut());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(otgy::core::chart_drafts_equal(editor.clipboard().content,
                                          clipboard.content));
    assert(editor.paste(200U));
  }

  // Canceling the payload retains the original chart and selection.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.cut_selection());
    const auto before = editor.draft();
    const auto history = editor.undo_count();
    assert(editor.selections().size() == 1U);
    const auto selected = editor.selections().front().drill_id;
    const auto cleared = editor.clear_clipboard();
    assert(cleared && cleared.changed);
    assert(!editor.has_clipboard());
    assert(!editor.has_pending_cut());
    assert(editor.clipboard().content.role_events().empty());
    assert(editor.selections().front().drill_id.value == selected.value);
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
    assert(editor.paste(200U).code == ChartEditCode::ClipboardEmpty);
    assert(!editor.clear_clipboard().changed);
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    const auto copied = editor.copy_selection();
    assert(copied);
    assert(copied.changed);
    assert(editor.selections().size() == 1U);
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
    assert(editor.select(ChartColumn::Npc, 16U));
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 16U);
    assert(editor.selections().front().last_row == 50U);
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
    assert(editor.select(ChartColumn::Showtime, 80U));
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().drill_id.value ==
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
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
    assert(loaded.select(ChartColumn::Npc, 0U));
    assert(loaded.selections().size() == 1U);
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
  const auto first_drill = before.dance_move_drills()[0].id;
  const auto second_drill = before.dance_move_drills()[1].id;
  for (const auto type : {ChartMarkerType::NpcBlockStart,
                          ChartMarkerType::NpcNote,
                          ChartMarkerType::NpcMoveEnd,
                          ChartMarkerType::PlayerBlockStart,
                          ChartMarkerType::PlayerNote,
                          ChartMarkerType::PlayerMoveEnd,
                          ChartMarkerType::ShowtimeMove}) {
    const auto marker = marker_of(before, type);
    for (const auto column : {ChartColumn::Npc, ChartColumn::Player,
                              ChartColumn::Showtime}) {
      const auto selected = editor.select(column, marker.row);
      assert(selected && selected.changed);
      assert(editor.selections().size() == 1U);
      assert(editor.selections().front().drill_id.value == first_drill.value);
      assert(editor.selections().front().first_row == 0U);
      assert(editor.selections().front().last_row == 14U);
      const auto removal_column =
          column == ChartColumn::Npc ? ChartColumn::Player : ChartColumn::Npc;
      const auto removed = editor.select(removal_column, marker.row);
      assert(removed && removed.changed);
      assert(editor.selections().empty());
    }
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(editor.undo_count() == history);
  }

  // Insertion order does not affect the stable row/identity ordering.
  assert(editor.select(ChartColumn::Showtime, 80U));
  assert(editor.selections().size() == 1U);
  assert(editor.selections().front().drill_id.value == second_drill.value);
  assert(editor.select(ChartColumn::Npc, 0U));
  assert(editor.selections().size() == 2U);
  assert(editor.selections()[0].drill_id.value == first_drill.value);
  assert(editor.selections()[1].drill_id.value == second_drill.value);
  assert(editor.selections()[1].first_row == 16U);
  assert(editor.selections()[1].last_row == 50U);

  const auto start = marker_of(before, ChartMarkerType::ShowtimeStart);
  for (const auto column : {ChartColumn::Npc, ChartColumn::Player,
                            ChartColumn::Showtime}) {
    const auto refused = editor.select(column, start.row);
    assert(refused.code == ChartEditCode::SelectionUnavailable);
    assert(!refused.changed);
  }
  for (const auto column : {ChartColumn::Npc, ChartColumn::Player,
                            ChartColumn::Showtime}) {
    assert(!editor.select(column, 5U).changed);
    assert(!editor.select(column, 999U).changed);
    assert(editor.selections().size() == 2U);
  }
  assert(!editor.select(static_cast<ChartColumn>(3U), 0U).changed);
  assert(!editor.select(static_cast<ChartColumn>(255U), 0U).changed);
  assert(editor.selections().size() == 2U);
  assert(editor.click(ChartColumn::Player, 0U).code ==
         ChartEditCode::PositionOccupied);
  assert(editor.click(ChartColumn::Showtime, 8U).code ==
         ChartEditCode::PositionOccupied);
  assert(editor.select(ChartColumn::Player, 0U));
  assert(editor.selections().size() == 1U);
  assert(editor.selections().front().drill_id.value == second_drill.value);
  assert(editor.select(ChartColumn::Showtime, 0U));
  assert(editor.selections().size() == 2U);
  assert(editor.select(ChartColumn::Player, 8U));
  assert(editor.selections().size() == 1U);
  assert(editor.selections().front().drill_id.value == second_drill.value);
  assert(editor.clear_selection().changed);
  assert(editor.selections().empty());
  assert(!editor.clear_selection().changed);
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  assert(editor.undo_count() == history);
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
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 0U);
    assert(editor.selections().front().last_row == 10U);
    assert(editor.selections().front().drill_id.value ==
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
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 50U);
    assert(editor.selections().front().last_row == 50U);
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
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 0U);
    assert(editor.selections().front().last_row == 40U);
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
    assert(editor.selections().empty());
    assert(otgy::core::chart_drafts_equal(editor.draft(), before));
    assert(!editor.can_undo());
  }

  // A selected dance move drops out of the selection once its last bound event
  // is deleted, and the leftover dance move object goes with it: with no event
  // left there is nothing to click, and the leftover would still fail the
  // finished-chart rules even without any Showtime.
  {
    auto editor = build_player_source();
    assert(editor.select(ChartColumn::Player, 50U));
    assert(editor.selections().size() == 1U);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.click(ChartColumn::Player, 50U));
    assert(editor.selections().empty());
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
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().drill_id.value == first_drill.value);

    const auto history = editor.undo_count();
    assert(editor.click(ChartColumn::Npc, 100U));
    assert(editor.undo_count() == history + 1U);
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().drill_id.value == first_drill.value);
    assert(editor.selections().front().first_row == 0U);
    assert(editor.selections().front().last_row == 14U);

    assert(editor.undo());
    assert(editor.can_redo());
    assert(editor.selections().size() == 1U);
    const auto deselected = editor.select(ChartColumn::Player, 8U);
    assert(deselected && deselected.changed);
    assert(editor.selections().empty());
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.can_redo());
    assert(editor.redo());
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().drill_id.value == first_drill.value);
  }

  // Deleting one role's events shrinks the span to the surviving role.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 8U);
    assert(editor.selections().front().last_row == 14U);
    assert(editor.undo());
    assert(editor.selections().size() == 1U);
    assert(editor.selections().front().first_row == 0U);
    assert(editor.selections().front().last_row == 14U);
  }

  // Undo restores the draft but not the selection: the selection is view state,
  // not a history entry.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Player, 8U));
    assert(editor.selections().size() == 1U);
    assert(editor.click(ChartColumn::Npc, 0U));
    assert(editor.selections().size() == 1U);
    assert(editor.click(ChartColumn::Player, 8U));
    assert(editor.selections().empty());
    assert(editor.undo());
    assert(editor.selections().empty());
  }

  // clear() empties the draft and the selection.
  {
    auto editor = build_two_complete_moves();
    assert(editor.select(ChartColumn::Npc, 16U));
    assert(editor.selections().size() == 1U);
    assert(editor.clear());
    assert(editor.selections().empty());
  }

  // Session commands on an empty editor succeed without touching history.
  {
    ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
    const auto cleared = editor.clear_selection();
    assert(cleared);
    assert(!cleared.changed);
    assert(editor.selections().empty());
    assert(!editor.can_undo());
    assert(!editor.can_redo());
    const auto empty = editor.select(ChartColumn::Npc, 0U);
    assert(empty);
    assert(!empty.changed);
    assert(editor.selections().empty());
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

  // (g) A stored SHOW-D is compared against the dance move it points at itself,
  // not against the recorded dance move at the same position. Here the first
  // marker points at the second dance move while its count still comes from the
  // first one, so the recorded dance move at its position matches its count and
  // only the marker reveals the stale content. Only a persisted or imported
  // draft can hold that mismatch, so the test builds it directly.
  {
    auto source = build_two_complete_moves();
    assert(source.click(ChartColumn::Showtime, 52U));
    const auto generated = source.draft();
    assert(generated.dance_move_drills().size() == 2U);
    assert(generated.dance_move_drills()[0].move_n_count == 2U);
    assert(generated.dance_move_drills()[1].move_n_count == 7U);
    auto moves = generated.showtime_move_events();
    assert(moves.size() == 2U);
    assert(moves[0].show_n_count == 2U && moves[1].show_n_count == 7U);
    moves[0].dance_move_drill_id = generated.dance_move_drills()[1].id;
    ChartEditor editor{ChartDraft{generated.role_events(),
                                 generated.showtime_start_events(),
                                 std::move(moves), generated.drill_groups(),
                                 generated.dance_move_drills(),
                                 generated.showtimes()}};
    assert(editor.draft().showtimes().size() == 1U);
    assert(editor.draft().showtimes().front().dance_move_drill_ids[0].value ==
           generated.dance_move_drills()[0].id.value);

    assert(editor.click(ChartColumn::Npc, 200U));
    assert(editor.draft().showtimes().empty());
    assert(editor.draft().showtime_start_events().empty());
    assert(editor.draft().showtime_move_events().empty());
    assert(editor.undo());
    assert(editor.draft().showtimes().size() == 1U);
    assert(editor.draft().showtime_move_events().size() == 2U);
  }

  // (h) A registered SHOW-D event that no longer exists makes the Showtime
  // stale even when the registration reaches past the recorded dance moves,
  // where a walk over the recorded dance moves alone would stop early.
  {
    auto source = build_two_complete_moves();
    assert(source.click(ChartColumn::Showtime, 52U));
    const auto generated = source.draft();
    auto moves = generated.showtime_move_events();
    auto showtimes = generated.showtimes();
    assert(moves.size() == 2U);
    assert(showtimes.front().dance_move_drill_ids.size() == 2U);
    assert(showtimes.front().show_move_event_ids.size() == 2U);
    const auto dropped = moves[1].id.value;
    moves.erase(moves.begin() + 1);
    showtimes.front().dance_move_drill_ids.erase(
        showtimes.front().dance_move_drill_ids.begin() + 1);
    ChartEditor editor{ChartDraft{generated.role_events(),
                                 generated.showtime_start_events(),
                                 std::move(moves), generated.drill_groups(),
                                 generated.dance_move_drills(),
                                 std::move(showtimes)}};
    assert(editor.draft().showtime_move_events().size() == 1U);
    assert(editor.draft().showtimes().front().show_move_event_ids.size() == 2U);
    assert(editor.draft().showtimes().front().show_move_event_ids[1].value ==
           dropped);
    assert(editor.draft().showtimes().front().dance_move_drill_ids.size() == 1U);

    assert(editor.click(ChartColumn::Npc, 200U));
    assert(editor.draft().showtimes().empty());
    assert(editor.draft().showtime_start_events().empty());
    assert(editor.draft().showtime_move_events().empty());
    assert(editor.undo());
    assert(editor.draft().showtime_move_events().size() == 1U);
    assert(editor.draft().showtimes().size() == 1U);
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

void test_split_dance_move_regenerates_showtime() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};

  // Build one complete dance move from two drill groups.
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
  assert(editor.draft().showtime_start_events().size() == 1U);
  assert(editor.draft().showtime_move_events().size() == 1U);

  // Closing the first group splits the trailing group into a second dance move.
  // Rebuild only the first group's generated player block; the second group and
  // all of its identities must survive the split unchanged.
  const auto before_split = editor.draft();
  const auto trailing_group = before_split.drill_groups()[1];
  const auto trailing_event_ids = drill_event_ids(
      before_split, before_split.dance_move_drills().front().id);
  assert(editor.click(ChartColumn::Npc, 5U));
  assert(editor.draft().showtimes().empty());
  assert(editor.draft().showtime_start_events().empty());
  assert(editor.draft().showtime_move_events().empty());
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.draft().drill_groups()[1].id.value == trailing_group.id.value);
  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.click(ChartColumn::Player, 7U));
  const auto split = editor.draft();
  assert(split.dance_move_drills().size() == 2U);
  assert(split.drill_groups().size() == 2U);
  assert(split.dance_move_drills()[0].move_n_count == 2U);
  assert(split.dance_move_drills()[1].move_n_count == 1U);
  assert(split.dance_move_drills()[0].drill_group_ids.size() == 1U);
  assert(split.dance_move_drills()[1].drill_group_ids.size() == 1U);
  assert(split.dance_move_drills()[1].drill_group_ids.front().value ==
         trailing_group.id.value);
  assert(split.drill_groups()[1].dance_move_drill_id.value ==
         split.dance_move_drills()[1].id.value);
  for (const auto& event : split.role_events()) {
    if (event.drill_group_id.value == trailing_group.id.value) {
      assert(event.dance_move_drill_id.value ==
             split.dance_move_drills()[1].id.value);
      assert(std::find(trailing_event_ids.begin(), trailing_event_ids.end(),
                       event.id.value) != trailing_event_ids.end());
    }
  }

  auto malformed_events = split.role_events();
  const auto source_drill_id = split.dance_move_drills()[1].id;
  const auto misplaced = std::find_if(
      malformed_events.begin(), malformed_events.end(),
      [trailing_group](const ChartRoleEvent& event) {
        return event.role == ChartRole::Player &&
               event.drill_group_id.value != trailing_group.id.value;
      });
  assert(misplaced != malformed_events.end());
  misplaced->dance_move_drill_id = source_drill_id;
  ChartDraft malformed{std::move(malformed_events),
                       split.showtime_start_events(),
                       split.showtime_move_events(), split.drill_groups(),
                       split.dance_move_drills(), split.showtimes()};
  ChartEditor malformed_editor{malformed};
  const auto malformed_result = malformed_editor.click(ChartColumn::Npc, 5U);
  assert(malformed_result.code == ChartEditCode::InvalidDraft);
  assert(!malformed_result.changed);
  assert(!malformed_editor.can_undo());
  assert(otgy::core::chart_drafts_equal(malformed_editor.draft(), malformed));

  auto mismatched_events = split.role_events();
  const auto destination_drill_id = split.dance_move_drills()[0].id;
  const auto mismatched = std::find_if(
      mismatched_events.begin(), mismatched_events.end(),
      [trailing_group](const ChartRoleEvent& event) {
        return event.drill_group_id.value == trailing_group.id.value;
      });
  assert(mismatched != mismatched_events.end());
  mismatched->dance_move_drill_id = destination_drill_id;
  ChartDraft mismatched_draft{
      std::move(mismatched_events), split.showtime_start_events(),
      split.showtime_move_events(), split.drill_groups(),
      split.dance_move_drills(), split.showtimes()};
  ChartEditor mismatched_editor{mismatched_draft};
  const auto mismatched_result =
      mismatched_editor.click(ChartColumn::Npc, 5U);
  assert(mismatched_result.code == ChartEditCode::InvalidDraft);
  assert(!mismatched_result.changed);
  assert(!mismatched_editor.can_undo());
  assert(otgy::core::chart_drafts_equal(mismatched_editor.draft(),
                                        mismatched_draft));

  const auto history = editor.undo_count();
  const auto generated = editor.click(ChartColumn::Showtime, 16U);
  assert(generated.code == ChartEditCode::Success);
  assert(generated.changed);
  assert(editor.undo_count() == history + 1U);
  const auto after = editor.draft();
  assert(after.showtime_start_events().size() == 1U);
  assert(after.showtime_move_events().size() == 2U);
  assert(after.showtimes().size() == 1U);
  const auto& showtime = after.showtimes().front();
  assert(showtime.dance_move_drill_ids.size() == 2U);
  assert(showtime.show_move_event_ids.size() == 2U);
  for (std::size_t index = 0U; index < 2U; ++index) {
    assert(showtime.dance_move_drill_ids[index].value ==
           after.dance_move_drills()[index].id.value);
    assert(after.showtime_move_events()[index].dance_move_drill_id.value ==
           after.dance_move_drills()[index].id.value);
    assert(after.showtime_move_events()[index].show_n_count ==
           after.dance_move_drills()[index].move_n_count);
  }
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), split));
  assert(editor.redo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), after));

  // Removing the boundary merges the trailing drill back into the original.
  // Rebuild the first generated player block so both roles again describe one
  // complete dance move, then regenerate one SHOW-D for that merged move.
  const auto split_with_showtime = editor.draft();
  const auto original_drill_id = split_with_showtime.dance_move_drills()[0].id;
  const auto trailing_drill_id = split_with_showtime.dance_move_drills()[1].id;
  const auto merge_history = editor.undo_count();
  assert(editor.click(ChartColumn::Npc, 5U));
  assert(editor.undo_count() == merge_history + 1U);
  assert(editor.draft().showtimes().empty());
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(editor.draft().dance_move_drills().front().id.value ==
         original_drill_id.value);
  assert(editor.draft().dance_move_drills().front().move_n_count == 3U);
  assert(editor.draft().dance_move_drills().front().drill_group_ids.size() == 2U);
  for (const auto& group : editor.draft().drill_groups()) {
    assert(group.dance_move_drill_id.value == original_drill_id.value);
  }
  for (const auto& event : editor.draft().role_events()) {
    assert(event.dance_move_drill_id.value == original_drill_id.value);
  }
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), split_with_showtime));
  assert(editor.redo());
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(std::none_of(editor.draft().dance_move_drills().begin(),
                      editor.draft().dance_move_drills().end(),
                      [trailing_drill_id](const auto& drill) {
                        return drill.id.value == trailing_drill_id.value;
                      }));

  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.click(ChartColumn::Player, 6U));
  assert(editor.click(ChartColumn::Player, 7U));
  assert(editor.click(ChartColumn::Showtime, 16U));
  const auto merged = editor.draft();
  assert(merged.showtimes().size() == 1U);
  assert(merged.showtime_move_events().size() == 1U);
  assert(merged.showtime_move_events().front().dance_move_drill_id.value ==
         original_drill_id.value);
  assert(merged.showtime_move_events().front().show_n_count == 3U);

  const auto recovery = otgy::core::encode_recovery_snapshot(merged);
  assert(recovery);
  const auto recovered = otgy::core::decode_recovery_snapshot(
      recovery.bytes()->data(), recovery.bytes()->size());
  assert(recovered);
  assert(otgy::core::chart_drafts_equal(*recovered.chart(), merged));

  constexpr double bpm = 100.00000000000003;
  constexpr double offset = -0.12345678901234566;
  const auto formal = otgy::core::encode_formal_draft(merged, bpm, offset);
  assert(formal);
  const auto decoded = otgy::core::decode_formal_draft(
      formal.bytes()->data(), formal.bytes()->size());
  assert(decoded);
  assert(otgy::core::chart_drafts_equal(decoded.payload()->chart, merged));
  assert(decoded.payload()->bpm == bpm);
  assert(decoded.payload()->offset_seconds == offset);
}

void test_nonterminal_npc_move_end_deletes_after_merge() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  for (const auto row : {2U, 3U, 4U, 5U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  for (const auto row : {6U, 7U}) {
    assert(editor.click(ChartColumn::Player, row));
  }
  for (const auto row : {10U, 11U, 12U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  for (const auto row : {13U, 14U}) {
    assert(editor.click(ChartColumn::Player, row));
  }
  for (const auto row : {18U, 19U, 20U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  for (const auto row : {21U, 22U}) {
    assert(editor.click(ChartColumn::Player, row));
  }

  assert(editor.draft().dance_move_drills().size() == 1U);
  const auto original = editor.draft();
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.click(ChartColumn::Npc, 5U));
  assert(editor.draft().dance_move_drills().size() == 2U);
  const auto split_with_later_end = editor.draft();
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), split_with_later_end));
  assert(editor.redo());

  const auto before_nonterminal_delete = editor.draft();
  const auto history = editor.undo_count();
  const auto deleted = editor.click(ChartColumn::Npc, 5U);
  assert(deleted.code == ChartEditCode::Success);
  assert(deleted.changed);
  assert(editor.undo_count() == history + 1U);
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(std::none_of(editor.draft().role_events().begin(),
                      editor.draft().role_events().end(),
                      [](const ChartRoleEvent& event) {
                        return event.row == 5U;
                      }));
  const auto after_nonterminal_delete = editor.draft();
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(),
                                        before_nonterminal_delete));
  assert(editor.redo());
  assert(otgy::core::chart_drafts_equal(editor.draft(),
                                        after_nonterminal_delete));
  assert(original.role_events().size() > editor.draft().role_events().size());
}

void test_showtime_boundary_prevents_dance_move_merge() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  for (const auto row : {0U, 2U, 4U, 6U, 6U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  for (const auto row : {8U, 10U}) {
    assert(editor.click(ChartColumn::Player, row));
  }
  assert(editor.click(ChartColumn::Showtime, 15U));
  assert(editor.draft().showtime_move_events().size() == 1U);

  // The final half-measure exception permits the next NPC-C, but the Showtime
  // remains a semantic boundary between the two dance moves.
  for (const auto row : {24U, 25U, 26U, 26U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  for (const auto row : {28U, 29U}) {
    assert(editor.click(ChartColumn::Player, row));
  }
  assert(editor.draft().dance_move_drills().size() == 2U);
  const auto second_drill = editor.draft().dance_move_drills()[1].id;

  assert(editor.click(ChartColumn::Npc, 6U));
  assert(editor.draft().showtimes().empty());
  assert(editor.draft().dance_move_drills().size() == 2U);
  assert(editor.draft().dance_move_drills()[1].id.value == second_drill.value);
  for (const auto& group : editor.draft().drill_groups()) {
    if (group.id.value == editor.draft().drill_groups()[1].id.value) {
      assert(group.dance_move_drill_id.value == second_drill.value);
    }
  }
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

[[nodiscard]] ChartDraft sparse_batch_draft() {
  return ChartDraft{
      {
          {{1U}, 10U, ChartRole::Npc, ChartRoleEventType::BlockStart, {11U}, {21U}},
          {{2U}, 12U, ChartRole::Npc, ChartRoleEventType::Note, {11U}, {21U}},
          {{3U}, 16U, ChartRole::Player, ChartRoleEventType::BlockStart, {11U}, {21U}},
          {{4U}, 80U, ChartRole::Npc, ChartRoleEventType::BlockStart, {12U}, {22U}},
          {{5U}, 84U, ChartRole::Npc, ChartRoleEventType::Note, {12U}, {22U}},
          {{6U}, 88U, ChartRole::Player, ChartRoleEventType::BlockStart, {12U}, {22U}},
          {{7U}, 240U, ChartRole::Npc, ChartRoleEventType::BlockStart, {13U}, {23U}},
      },
      {}, {},
      {
          {{11U}, {21U}, {{1U}, {2U}}, {{3U}}},
          {{12U}, {22U}, {{4U}, {5U}}, {{6U}}},
          {{13U}, {23U}, {{7U}}, {}},
      },
      {
          {{21U}, 3U, {{11U}}},
          {{22U}, 7U, {{12U}}},
          {{23U}, 1U, {{13U}}},
      },
      {}};
}

void select_sparse_pair(ChartEditor& editor) {
  assert(editor.select(ChartColumn::Npc, 80U));
  assert(editor.select(ChartColumn::Npc, 10U));
  assert(editor.selections().size() == 2U);
  assert(editor.selections()[0].drill_id.value == 21U);
  assert(editor.selections()[1].drill_id.value == 22U);
}

void assert_same_selections(
    const std::vector<otgy::core::ChartSelection>& actual,
    const std::vector<otgy::core::ChartSelection>& expected) {
  assert(actual.size() == expected.size());
  for (std::size_t index = 0U; index < actual.size(); ++index) {
    assert(actual[index].active == expected[index].active);
    assert(actual[index].drill_id.value == expected[index].drill_id.value);
    assert(actual[index].first_row == expected[index].first_row);
    assert(actual[index].last_row == expected[index].last_row);
  }
}

void test_batch_clipboard_geometry_and_ids() {
  ChartEditor editor{sparse_batch_draft()};
  select_sparse_pair(editor);
  const auto original = editor.draft();
  assert(editor.copy_selection());
  const auto clipboard = editor.clipboard();
  assert(clipboard.base_row == 10U);
  assert(fragment_rows(clipboard) ==
         std::vector<std::uint64_t>({0U, 2U, 6U, 70U, 74U, 78U}));
  assert(clipboard.content.dance_move_drills().size() == 2U);
  assert(clipboard.content.drill_groups().size() == 2U);
  assert(clipboard.content.dance_move_drills()[0].move_n_count == 3U);
  assert(clipboard.content.dance_move_drills()[1].move_n_count == 7U);
  assert(otgy::core::chart_drafts_equal(editor.draft(), original));
  assert(editor.undo_count() == 0U);

  // A copy survives later source edits and repeated batch pastes. Every object
  // receives a globally fresh ID, including objects from different batches.
  assert(editor.click(ChartColumn::Npc, 12U));
  const auto before_paste = editor.draft();
  const auto history = editor.undo_count();
  std::vector<std::uint64_t> allocated;
  for (const auto anchor : {300U, 500U}) {
    assert(editor.paste(anchor));
    assert(editor.selections().size() == 2U);
    assert(editor.selections()[0].first_row == anchor);
    assert(editor.selections()[0].last_row == anchor + 6U);
    assert(editor.selections()[1].first_row == anchor + 70U);
    assert(editor.selections()[1].last_row == anchor + 78U);
    const auto fresh_id = [&allocated](std::uint64_t id) {
      assert(id > 23U);
      assert(std::find(allocated.begin(), allocated.end(), id) == allocated.end());
      allocated.push_back(id);
    };
    for (std::size_t index = 0U; index < 2U; ++index) {
      const auto drill_id = editor.selections()[index].drill_id;
      fresh_id(drill_id.value);
      const auto& source_drill = clipboard.content.dance_move_drills()[index];
      const auto drill = std::find_if(
          editor.draft().dance_move_drills().begin(),
          editor.draft().dance_move_drills().end(),
          [drill_id](const auto& item) { return item.id.value == drill_id.value; });
      assert(drill != editor.draft().dance_move_drills().end());
      assert(drill->move_n_count == source_drill.move_n_count);
      assert(drill->drill_group_ids.size() == 1U);
      const auto group_id = drill->drill_group_ids.front();
      fresh_id(group_id.value);
      const auto group = std::find_if(
          editor.draft().drill_groups().begin(), editor.draft().drill_groups().end(),
          [group_id](const auto& item) { return item.id.value == group_id.value; });
      assert(group != editor.draft().drill_groups().end());
      assert(group->dance_move_drill_id.value == drill_id.value);
      assert(group->npc_event_ids.size() == 2U);
      assert(group->player_event_ids.size() == 1U);
      for (const auto& source : clipboard.content.role_events()) {
        if (source.dance_move_drill_id.value != source_drill.id.value) {
          continue;
        }
        const auto event = std::find_if(
            editor.draft().role_events().begin(), editor.draft().role_events().end(),
            [anchor, &source](const auto& item) {
              return item.row == anchor + source.row;
            });
        assert(event != editor.draft().role_events().end());
        fresh_id(event->id.value);
        assert(event->role == source.role);
        assert(event->type == source.type);
        assert(event->drill_group_id.value == group_id.value);
        assert(event->dance_move_drill_id.value == drill_id.value);
        const auto& ids = event->role == ChartRole::Npc
                              ? group->npc_event_ids : group->player_event_ids;
        assert(std::any_of(ids.begin(), ids.end(), [&event](const auto id) {
          return id.value == event->id.value;
        }));
      }
    }
    assert(otgy::core::chart_drafts_equal(editor.clipboard().content,
                                          clipboard.content));
  }
  assert(editor.undo_count() == history + 2U);
  assert(editor.undo());
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before_paste));
  assert(editor.redo());
  assert(editor.selections().empty());

  // A stale registration of another selected drill's group must not duplicate
  // that group in the batch or bind both pasted drills to its replacement.
  auto stale_groups = original.dance_move_drills();
  stale_groups[0].drill_group_ids.push_back({12U});
  ChartEditor stale{ChartDraft{original.role_events(), {}, {},
                              original.drill_groups(), std::move(stale_groups), {}}};
  select_sparse_pair(stale);
  assert(stale.copy_selection());
  assert(stale.clipboard().content.drill_groups().size() == 2U);
  assert(stale.clipboard().content.dance_move_drills()[0].drill_group_ids.size() == 1U);
  assert(stale.paste(300U));
  assert(stale.draft().drill_groups().size() == 5U);
}

void test_batch_cut_overlap_and_failures() {
  ChartEditor editor{sparse_batch_draft()};
  ChartEditor control{sparse_batch_draft()};
  for (auto* item : {&editor, &control}) {
    select_sparse_pair(*item);
    assert(item->click(ChartColumn::Npc, 250U));
    assert(item->undo());
    assert(item->cut_selection());
  }
  const auto before = editor.draft();
  const auto clipboard = editor.clipboard();
  const auto selections = editor.selections();
  const auto history = editor.undo_count();
  const auto redo = editor.redo_count();
  // Row 240 lies only in the gap between the two target spans. The whole
  // envelope is reserved, even though no pasted marker would land there.
  const auto preview = editor.preview_paste(200U);
  assert(preview.code == ChartEditCode::PasteRangeOccupied);
  assert(preview.spans.size() == 2U);
  assert(preview.spans[0].last_row == 206U);
  assert(preview.spans[1].first_row == 270U);
  assert(editor.paste(200U).code == preview.code);
  assert(editor.has_pending_cut());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  assert(otgy::core::chart_drafts_equal(editor.clipboard().content, clipboard.content));
  assert_same_selections(editor.selections(), selections);
  assert(editor.undo_count() == history);
  assert(editor.redo_count() == redo);

  // Both original spans overlap this destination, but their sources are
  // removed before occupancy is checked. Failed plans consume no IDs.
  assert(editor.preview_paste(16U));
  assert(editor.paste(16U));
  assert(control.paste(16U));
  assert(otgy::core::chart_drafts_equal(editor.draft(), control.draft()));
  assert(editor.undo_count() == history + 1U);
  assert(!editor.can_redo());
  assert(!editor.has_pending_cut());
  assert(editor.draft().role_events().size() == before.role_events().size());
  assert(editor.selections()[0].first_row == 16U);
  assert(editor.selections()[1].first_row == 86U);
  for (const auto& event : editor.draft().role_events()) {
    assert(event.dance_move_drill_id.value != 21U);
    assert(event.dance_move_drill_id.value != 22U);
  }
  assert(editor.undo());
  assert(otgy::core::chart_drafts_equal(editor.draft(), before));
  assert(!editor.has_pending_cut());
  assert(editor.paste(300U));
  assert(editor.draft().role_events().size() == before.role_events().size() + 6U);
}

void test_pending_cut_source_changes_and_retry() {
  // Existing value changes, membership additions, role deletion and complete
  // source deletion all invalidate the saved raw source, even with two members.
  for (const auto row : {12U, 82U, 10U, 16U}) {
    ChartEditor editor{sparse_batch_draft()};
    select_sparse_pair(editor);
    assert(editor.cut_selection());
    const auto clipboard = editor.clipboard();
    const auto column = row == 16U ? ChartColumn::Player : ChartColumn::Npc;
    assert(editor.click(column, row));
    if (row == 10U) {
      assert(editor.click(ChartColumn::Player, 16U));
    }
    const auto changed = editor.draft();
    const auto history = editor.undo_count();
    const auto selections = editor.selections();
    const auto preview = editor.preview_paste(300U);
    assert(preview.code == ChartEditCode::CutSourceChanged);
    assert(preview.ghost_markers.size() == 6U);
    assert(preview.last_row == 378U);
    assert(editor.paste(300U).code == ChartEditCode::CutSourceChanged);
    assert(otgy::core::chart_drafts_equal(editor.draft(), changed));
    assert(otgy::core::chart_drafts_equal(editor.clipboard().content, clipboard.content));
    assert_same_selections(editor.selections(), selections);
    assert(editor.undo_count() == history);
    assert(editor.has_pending_cut());
    assert(editor.undo());
    if (row == 10U) {
      assert(editor.undo());
    }
    assert(editor.has_pending_cut());
    assert(editor.preview_paste(300U));
    assert(editor.paste(300U));
    assert(!editor.has_pending_cut());
  }

  // A split changes ownership and relation vectors; undo restores exactly the
  // raw source and makes the original deferred cut usable again.
  auto split = build_two_complete_moves();
  assert(split.select(ChartColumn::Npc, 0U));
  assert(split.select(ChartColumn::Npc, 16U));
  assert(split.cut_selection());
  assert(split.click(ChartColumn::Npc, 4U));
  assert(split.preview_paste(300U).code == ChartEditCode::CutSourceChanged);
  assert(split.paste(300U).code == ChartEditCode::CutSourceChanged);
  assert(split.undo());
  assert(split.paste(300U));

  ChartEditor copied{sparse_batch_draft()};
  select_sparse_pair(copied);
  assert(copied.cut_selection());
  assert(copied.copy_selection());
  assert(!copied.has_pending_cut());
  assert(copied.paste(300U));
  assert(copied.draft().role_events().size() == 13U);
}

void test_batch_selection_revalidation() {
  ChartEditor editor{sparse_batch_draft()};
  select_sparse_pair(editor);
  assert(editor.click(ChartColumn::Npc, 10U));
  assert(editor.selections().size() == 2U);
  assert(editor.selections()[0].first_row == 16U);
  assert(editor.selections()[0].last_row == 16U);
  assert(editor.selections()[1].first_row == 80U);
  assert(editor.undo());
  assert(editor.selections()[0].first_row == 10U);
  assert(editor.redo());
  assert(editor.selections()[0].first_row == 16U);
  assert(editor.click(ChartColumn::Player, 16U));
  assert(editor.selections().size() == 1U);
  assert(editor.selections().front().drill_id.value == 22U);
  assert(editor.undo());
  assert(editor.selections().size() == 1U);
  assert(editor.selections().front().drill_id.value == 22U);
  assert(editor.redo());
  assert(editor.selections().size() == 1U);
}

void test_paste_preview_is_read_only() {
  ChartEditor editor{sparse_batch_draft()};
  ChartEditor control{sparse_batch_draft()};
  for (auto* item : {&editor, &control}) {
    select_sparse_pair(*item);
    assert(item->copy_selection());
  }
  const auto original = editor.draft();
  const auto selections = editor.selections();
  const auto clipboard = editor.clipboard();
  const auto projected = otgy::core::project_chart_markers(clipboard.content);
  assert(projected);
  for (unsigned int repeat = 0U; repeat < 3U; ++repeat) {
    const auto preview = editor.preview_paste(300U);
    assert(preview);
    assert(preview.anchor_row == 300U);
    assert(preview.last_row == 378U);
    assert(preview.spans.size() == 2U);
    assert(preview.spans[0].drill_id.value == 21U);
    assert(preview.spans[0].first_row == 300U);
    assert(preview.spans[0].last_row == 306U);
    assert(preview.spans[1].drill_id.value == 22U);
    assert(preview.spans[1].first_row == 370U);
    assert(preview.spans[1].last_row == 378U);
    assert(preview.ghost_markers.size() == projected.markers()->size());
    for (std::size_t index = 0U; index < preview.ghost_markers.size(); ++index) {
      const auto& ghost = preview.ghost_markers[index];
      const auto& source = projected.markers()->at(index);
      assert(ghost.id.value == source.id.value);
      assert(ghost.row == source.row + 300U);
      assert(ghost.type == source.type);
      assert(ghost.column == source.column);
      assert(ghost.show_n_count == source.show_n_count);
    }
    assert(otgy::core::chart_drafts_equal(editor.draft(), original));
    assert(otgy::core::chart_drafts_equal(editor.clipboard().content, clipboard.content));
    assert_same_selections(editor.selections(), selections);
    assert(editor.undo_count() == 0U);
    assert(editor.redo_count() == 0U);
    assert(!editor.has_pending_cut());
  }
  for (const auto anchor :
       {std::uint64_t{200U}, std::numeric_limits<std::uint64_t>::max()}) {
    const auto preview = editor.preview_paste(anchor);
    assert(preview.code == (anchor == 200U ? ChartEditCode::PasteRangeOccupied
                                          : ChartEditCode::RangeOverflow));
    assert(editor.paste(anchor).code == preview.code);
  }
  assert(editor.paste(300U));
  assert(control.paste(300U));
  assert(otgy::core::chart_drafts_equal(editor.draft(), control.draft()));

  ChartEditor empty{ChartDraft{{}, {}, {}, {}, {}, {}}};
  const auto preview = empty.preview_paste(12U);
  assert(preview.code == ChartEditCode::ClipboardEmpty);
  assert(preview.anchor_row == 12U && preview.last_row == 12U);
  assert(preview.spans.empty() && preview.ghost_markers.empty());
  assert(empty.paste(12U).code == preview.code);
}

void test_result_messages() {
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(ChartEditCode::CutSourceChanged);
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
  test_split_dance_move_regenerates_showtime();
  test_nonterminal_npc_move_end_deletes_after_merge();
  test_showtime_boundary_prevents_dance_move_merge();
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
  test_batch_clipboard_geometry_and_ids();
  test_batch_cut_overlap_and_failures();
  test_pending_cut_source_changes_and_retry();
  test_batch_selection_revalidation();
  test_paste_preview_is_read_only();
  test_result_messages();
  return 0;
}
