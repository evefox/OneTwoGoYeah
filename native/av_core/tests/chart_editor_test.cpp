// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
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
  assert(editor.click(ChartColumn::Npc, 4U));
  assert(marker_of(editor.draft(), ChartMarkerType::ShowtimeMove)
             .show_n_count == 2U);
  assert(marker_of(editor.draft(), ChartMarkerType::ShowtimeMove, 1U)
             .show_n_count == 7U);
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

void test_result_messages() {
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(ChartEditCode::NothingToRedo); ++raw) {
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
  test_clear_and_invalid_identity();
  test_result_messages();
  return 0;
}
