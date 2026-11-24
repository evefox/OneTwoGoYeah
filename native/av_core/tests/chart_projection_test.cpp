// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_projection.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartColumn;
using otgy::core::ChartDraft;
using otgy::core::ChartMarkerType;
using otgy::core::ChartProjectionError;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::ChartTimelineRangeError;

[[nodiscard]] ChartRoleEvent role_event(std::uint64_t id, std::uint64_t row,
                                        ChartRole role,
                                        ChartRoleEventType type) {
  return {{id}, row, role, type, {100U}, {200U}};
}

[[nodiscard]] ChartDraft sample_draft() {
  return {{{role_event(5U, 50U, ChartRole::Player,
                       ChartRoleEventType::MoveEnd),
            role_event(1U, 10U, ChartRole::Npc,
                       ChartRoleEventType::BlockStart),
            role_event(3U, 30U, ChartRole::Npc, ChartRoleEventType::Note),
            role_event(2U, 20U, ChartRole::Player,
                       ChartRoleEventType::BlockStart)}},
          {{{4U}, 40U, {400U}}},
          {{{7U}, 65U, {400U}, {200U}, 7U},
           {{6U}, 60U, {400U}, {200U}, 6U}},
          {},
          {},
          {}};
}

void test_global_projection_and_queries() {
  const auto result = otgy::core::project_chart_markers(sample_draft());
  assert(result);
  assert(result.error() == ChartProjectionError::None);
  assert(result.markers()->size() == 7U);
  const auto& markers = *result.markers();
  for (std::size_t index = 1U; index < markers.size(); ++index) {
    assert(markers[index - 1U].row < markers[index].row);
  }
  assert(markers[0].type == ChartMarkerType::NpcBlockStart);
  assert(markers[1].column == ChartColumn::Player);
  assert(markers[3].type == ChartMarkerType::ShowtimeStart);
  assert(markers[5].show_n_count == 6U);

  assert(otgy::core::previous_chart_marker(markers, 10U) == nullptr);
  assert(otgy::core::previous_chart_marker(markers, 30U)->id.value == 2U);
  assert(otgy::core::previous_chart_marker(markers, 31U)->id.value == 3U);
  assert(otgy::core::previous_chart_marker_in_column(
             markers, ChartColumn::Npc, 50U)
             ->id.value == 3U);
  assert(otgy::core::previous_chart_marker_in_column(
             markers, ChartColumn::Showtime, 40U) == nullptr);
  assert(otgy::core::next_chart_marker(markers, 40U)->id.value == 5U);
  assert(otgy::core::next_chart_marker(markers, 65U) == nullptr);
  assert(otgy::core::find_chart_marker(markers, {4U})->row == 40U);
  assert(otgy::core::find_chart_marker(markers, {999U}) == nullptr);

  const auto copy = *result.markers();
  auto moved = std::move(copy);
  assert(moved.size() == 7U);
  assert(moved.data() != result.markers()->data());
}

void test_showtime_ranges_and_sequence_roles() {
  ChartDraft draft{
      {{role_event(9U, 80U, ChartRole::Npc,
                   ChartRoleEventType::BlockStart)}},
      {{{1U}, 0U, {1U}}, {{8U}, 70U, {8U}}},
      {{{2U}, 1U, {1U}, {20U}, 1U},
       {{3U}, 16U, {1U}, {21U}, 6U},
       {{4U}, 32U, {1U}, {22U}, 7U},
       {{5U}, 64U, {1U}, {23U}, 12U},
       {{10U}, 96U, {8U}, {24U}, 2U}},
      {},
      {},
      {}};
  const auto result = otgy::core::project_showtime_moves(draft);
  assert(result);
  assert(result.moves()->size() == 5U);
  const auto& moves = *result.moves();
  assert(moves[0].is_first_in_sequence);
  assert(!moves[0].is_last_in_sequence);
  assert(moves[0].range_start == 0U && moves[0].range_end == 16U);
  assert(!moves[1].is_first_in_sequence);
  assert(moves[1].range_end == 48U);
  assert(moves[2].range_end == 64U);
  assert(moves[3].is_last_in_sequence);
  assert(moves[3].range_end == 96U);
  assert(moves[4].is_first_in_sequence && moves[4].is_last_in_sequence);
  assert(moves[4].range_end == 112U);

  draft = ChartDraft{{}, {}, {{{1U}, 0U, {1U}, {1U}, 0U}}, {}, {}, {}};
  assert(otgy::core::project_showtime_moves(draft).error() ==
         ChartProjectionError::InvalidShowNCount);

  draft = ChartDraft{
      {},
      {},
      {{{1U}, std::numeric_limits<std::uint64_t>::max(), {1U}, {1U}, 7U}},
      {},
      {},
      {}};
  assert(otgy::core::project_showtime_moves(draft).error() ==
         ChartProjectionError::RangeOverflow);
}

void test_showtime_duration_boundary() {
  const auto range_end = [](std::uint64_t count) {
    const ChartDraft draft{
        {}, {}, {{{1U}, 0U, {1U}, {2U}, count}}, {}, {}, {}};
    const auto result = otgy::core::project_showtime_moves(draft);
    assert(result);
    assert(result.moves()->size() == 1U);
    return result.moves()->front().range_end;
  };
  assert(range_end(1U) == 16U);
  assert(range_end(5U) == 16U);
  assert(range_end(6U) == 32U);
  assert(range_end(11U) == 32U);
  assert(range_end(12U) == 32U);
}

void test_note_ordinals_ignore_other_roles_and_block_starts() {
  const ChartDraft draft{
      {{role_event(8U, 80U, ChartRole::Npc, ChartRoleEventType::Note),
        role_event(1U, 10U, ChartRole::Npc, ChartRoleEventType::Note),
        role_event(4U, 40U, ChartRole::Npc, ChartRoleEventType::BlockStart),
        role_event(2U, 20U, ChartRole::Player, ChartRoleEventType::Note),
        role_event(3U, 30U, ChartRole::Npc, ChartRoleEventType::Note),
        role_event(5U, 50U, ChartRole::Npc, ChartRoleEventType::MoveEnd),
        role_event(7U, 70U, ChartRole::Player, ChartRoleEventType::Note),
        role_event(6U, 60U, ChartRole::Npc, ChartRoleEventType::Note)}},
      {{{20U}, 35U, {1U}}},
      {},
      {},
      {},
      {}};

  assert(otgy::core::chart_note_ordinal(draft, {1U}).ordinal == 1U);
  assert(otgy::core::chart_note_ordinal(draft, {3U}).ordinal == 2U);
  assert(otgy::core::chart_note_ordinal(draft, {6U}).ordinal == 1U);
  assert(otgy::core::chart_note_ordinal(draft, {8U}).ordinal == 2U);
  assert(otgy::core::chart_note_ordinal(draft, {7U}).ordinal == 2U);
  assert(!otgy::core::chart_note_ordinal(draft, {4U}));
  assert(!otgy::core::chart_note_ordinal(draft, {999U}));
}

void test_chart_timeline_range_scans_model_in_core() {
  const auto draft = sample_draft();
  const auto original = draft;
  const auto result = otgy::core::calculate_chart_timeline_range(
      draft, {100.0, 0.0}, std::nullopt);
  assert(result);
  assert(result.range()->end_sixteenth_row() == 96.0);
  const auto expected_seconds =
      otgy::core::sixteenth_row_to_seconds({100.0, 0.0}, 96.0);
  assert(expected_seconds);
  assert(result.range()->end_seconds() == *expected_seconds.value());
  assert(otgy::core::chart_drafts_equal(draft, original));

  const auto audio_wins = otgy::core::calculate_chart_timeline_range(
      draft, {100.0, 0.0}, 20.000000000000004);
  assert(audio_wins);
  assert(audio_wins.range()->end_seconds() == 20.000000000000004);

  const ChartDraft invalid_show_count{
      {}, {}, {{{1U}, 0U, {1U}, {1U}, 0U}}, {}, {}, {}};
  const auto invalid_projection =
      otgy::core::calculate_chart_timeline_range(
          invalid_show_count, {100.0, 0.0}, std::nullopt);
  assert(!invalid_projection);
  assert(invalid_projection.failure()->error ==
         ChartTimelineRangeError::ChartProjectionFailed);
  assert(invalid_projection.failure()->projection_error ==
         ChartProjectionError::InvalidShowNCount);

  const auto invalid_timing = otgy::core::calculate_chart_timeline_range(
      draft, {std::numeric_limits<double>::quiet_NaN(), 0.0}, std::nullopt);
  assert(!invalid_timing);
  assert(invalid_timing.failure()->error ==
         ChartTimelineRangeError::TimelineRangeFailed);
  assert(invalid_timing.failure()->timeline_error ==
         otgy::core::TimelineRangeError::InvalidTiming);
}

void test_error_messages() {
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(ChartProjectionError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::chart_projection_error_message(
        static_cast<ChartProjectionError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::chart_projection_error_message(
              static_cast<ChartProjectionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_global_projection_and_queries();
  test_showtime_ranges_and_sequence_roles();
  test_showtime_duration_boundary();
  test_note_ordinals_ignore_other_roles_and_block_starts();
  test_chart_timeline_range_scans_model_in_core();
  test_error_messages();
  return 0;
}
