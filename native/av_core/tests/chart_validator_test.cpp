// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_validator.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartDiagnosticCode;
using otgy::core::ChartDraft;
using otgy::core::ChartValidationResult;

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] ChartDraft draft_from_file(const std::string& path) {
  const auto bytes = read_file(path);
  auto import = otgy::core::import_finished_slk(bytes.data(), bytes.size());
  assert(import);
  const auto projection =
      otgy::core::project_finished_slk_to_chart_draft(*import.document());
  assert(projection);
  return *projection.draft();
}

[[nodiscard]] ChartDraft rebuild(
    const ChartDraft& draft,
    std::vector<otgy::core::ChartRoleEvent> role_events,
    std::vector<otgy::core::ChartShowtimeStartEvent> start_events,
    std::vector<otgy::core::ChartShowtimeMoveEvent> move_events,
    std::vector<otgy::core::ChartDrillGroup> groups,
    std::vector<otgy::core::ChartDanceMoveDrill> drills,
    std::vector<otgy::core::ChartShowtime> showtimes) {
  static_cast<void>(draft);
  return {std::move(role_events), std::move(start_events),
          std::move(move_events), std::move(groups),
          std::move(drills),      std::move(showtimes)};
}

[[nodiscard]] bool has_diagnostic(const ChartValidationResult& result,
                                  ChartDiagnosticCode code) {
  assert(!result.is_finished());
  assert(result.diagnostics() != nullptr);
  for (const auto& diagnostic : *result.diagnostics()) {
    if (diagnostic.code == code) {
      return true;
    }
  }
  return false;
}

void test_imported_drafts_validate_and_snapshot_ownership() {
  const auto canonical =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto canonical_result = otgy::core::validate_finished_chart(canonical);
  assert(canonical_result.is_finished());
  assert(canonical_result.finished_chart() != nullptr);
  assert(canonical_result.diagnostics() == nullptr);
  assert(canonical_result.error() == otgy::core::ChartValidationError::None);
  assert(canonical_result.finished_chart()->draft().role_events().size() ==
         408U);

  const auto snapshot_copy = *canonical_result.finished_chart();
  const auto snapshot_move = std::move(canonical_result).take_finished_chart();
  assert(snapshot_copy.draft().role_events().data() !=
         canonical.role_events().data());
  assert(snapshot_copy.draft().role_events().data() !=
         snapshot_move.draft().role_events().data());

  const auto compatibility =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_result =
      otgy::core::validate_finished_chart(compatibility);
  assert(compatibility_result.is_finished());
  assert(compatibility_result.finished_chart()->draft().role_events().size() ==
         384U);
}

void test_empty_duplicate_and_orphan_diagnostics() {
  const ChartDraft empty{{}, {}, {}, {}, {}, {}};
  const auto empty_result = otgy::core::validate_finished_chart(empty);
  assert(has_diagnostic(empty_result, ChartDiagnosticCode::EmptyChart));

  const auto valid =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto role_events = valid.role_events();
  role_events.push_back(role_events.front());
  auto duplicate =
      rebuild(valid, std::move(role_events), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  const auto duplicate_result = otgy::core::validate_finished_chart(duplicate);
  assert(
      has_diagnostic(duplicate_result, ChartDiagnosticCode::DuplicateEventId));
  assert(
      has_diagnostic(duplicate_result, ChartDiagnosticCode::DuplicateEventRow));

  role_events = valid.role_events();
  auto orphan = role_events.front();
  orphan.id.value = 50'000U;
  orphan.row = 50'000U;
  role_events.push_back(orphan);
  const auto orphan_draft =
      rebuild(valid, std::move(role_events), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  assert(has_diagnostic(otgy::core::validate_finished_chart(orphan_draft),
                        ChartDiagnosticCode::OrphanEvent));

  auto move_events = valid.showtime_move_events();
  auto orphan_move = move_events.front();
  orphan_move.id.value = 60'001U;
  orphan_move.row = 60'001U;
  move_events.push_back(orphan_move);
  const auto orphan_move_draft =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              std::move(move_events), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  assert(has_diagnostic(
      otgy::core::validate_finished_chart(orphan_move_draft),
      ChartDiagnosticCode::OrphanEvent));
}

void test_block_dance_and_showtime_diagnostics_without_mutation() {
  const auto valid =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto original_first_row = valid.role_events().front().row;

  auto role_events = valid.role_events();
  role_events[1U].row += 1U;
  const auto bad_block =
      rebuild(valid, std::move(role_events), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  const auto block_result = otgy::core::validate_finished_chart(bad_block);
  assert(
      has_diagnostic(block_result, ChartDiagnosticCode::DrillBlockMismatch) ||
      has_diagnostic(block_result, ChartDiagnosticCode::InvalidDrillBlock));

  auto drills = valid.dance_move_drills();
  drills.front().move_n_count -= 1U;
  const auto bad_drill =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              std::move(drills), valid.showtimes());
  assert(has_diagnostic(otgy::core::validate_finished_chart(bad_drill),
                        ChartDiagnosticCode::InvalidDanceMoveDrill));

  auto move_events = valid.showtime_move_events();
  move_events.front().show_n_count -= 1U;
  const auto bad_showtime =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              std::move(move_events), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  assert(has_diagnostic(otgy::core::validate_finished_chart(bad_showtime),
                        ChartDiagnosticCode::InvalidShowtime));

  auto groups = valid.drill_groups();
  auto invalid_group_id = groups.front().id;
  auto dance_relations = valid.dance_move_drills();
  dance_relations.front().drill_group_ids.erase(
      dance_relations.front().drill_group_ids.begin());
  const auto orphan_group =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              valid.showtime_move_events(), std::move(groups),
              std::move(dance_relations), valid.showtimes());
  const auto orphan_result = otgy::core::validate_finished_chart(orphan_group);
  assert(has_diagnostic(orphan_result, ChartDiagnosticCode::OrphanDrillGroup));
  assert(valid.drill_groups().front().id.value == invalid_group_id.value);
  assert(valid.role_events().front().row == original_first_row);
}

void test_finished_snapshot_requires_owned_and_ordered_showtime_events() {
  const auto valid =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");

  auto start_events = valid.showtime_start_events();
  auto orphan_start = start_events.front();
  orphan_start.id.value = 60'000U;
  orphan_start.row = 60'000U;
  start_events.push_back(orphan_start);
  const auto orphan_draft =
      rebuild(valid, valid.role_events(), std::move(start_events),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  assert(has_diagnostic(otgy::core::validate_finished_chart(orphan_draft),
                        ChartDiagnosticCode::OrphanEvent));

  auto showtimes = valid.showtimes();
  assert(showtimes.front().dance_move_drill_ids.size() > 1U);
  std::reverse(showtimes.front().dance_move_drill_ids.begin(),
               showtimes.front().dance_move_drill_ids.end());
  std::reverse(showtimes.front().show_move_event_ids.begin(),
               showtimes.front().show_move_event_ids.end());
  const auto out_of_order =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), std::move(showtimes));
  assert(has_diagnostic(otgy::core::validate_finished_chart(out_of_order),
                        ChartDiagnosticCode::InvalidEventRelation));

  auto overflowing_move_events = valid.showtime_move_events();
  overflowing_move_events.back().row =
      std::numeric_limits<std::uint64_t>::max();
  const auto overflowing_range =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              std::move(overflowing_move_events), valid.drill_groups(),
              valid.dance_move_drills(), valid.showtimes());
  assert(has_diagnostic(
      otgy::core::validate_finished_chart(overflowing_range),
      ChartDiagnosticCode::InvalidShowtime));
}

void test_showtime_less_trailing_drills_are_finished() {
  const auto valid =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto start_events = valid.showtime_start_events();
  auto move_events = valid.showtime_move_events();
  auto showtimes = valid.showtimes();
  const auto removed = showtimes.back();
  start_events.erase(
      std::remove_if(start_events.begin(), start_events.end(),
                     [&removed](const auto& event) {
                       return event.id.value == removed.start_event_id.value;
                     }),
      start_events.end());
  for (const auto move_id : removed.show_move_event_ids) {
    move_events.erase(
        std::remove_if(move_events.begin(), move_events.end(),
                       [move_id](const auto& event) {
                         return event.id.value == move_id.value;
                       }),
        move_events.end());
  }
  showtimes.pop_back();

  const auto trailing =
      rebuild(valid, valid.role_events(), std::move(start_events),
              std::move(move_events), valid.drill_groups(),
              valid.dance_move_drills(), std::move(showtimes));
  const auto trailing_result = otgy::core::validate_finished_chart(trailing);
  assert(trailing_result.is_finished());
  assert(trailing_result.finished_chart()->draft().showtimes().size() == 1U);
  assert(
      trailing_result.finished_chart()->draft().dance_move_drills().size() ==
      17U);

  auto earlier_start_events = valid.showtime_start_events();
  auto earlier_move_events = valid.showtime_move_events();
  auto earlier_showtimes = valid.showtimes();
  const auto removed_first = earlier_showtimes.front();
  earlier_start_events.erase(
      std::remove_if(earlier_start_events.begin(), earlier_start_events.end(),
                     [&removed_first](const auto& event) {
                       return event.id.value ==
                              removed_first.start_event_id.value;
                     }),
      earlier_start_events.end());
  for (const auto move_id : removed_first.show_move_event_ids) {
    earlier_move_events.erase(
        std::remove_if(earlier_move_events.begin(), earlier_move_events.end(),
                       [move_id](const auto& event) {
                         return event.id.value == move_id.value;
                       }),
        earlier_move_events.end());
  }
  earlier_showtimes.erase(earlier_showtimes.begin());
  // Deleting the first Showtime keeps its moves as free moves. They now lead
  // the remaining Showtime's stage, but that Showtime still records the later
  // moves, so its recorded list is no longer the stage prefix.
  const auto orphaned_stage =
      rebuild(valid, valid.role_events(), std::move(earlier_start_events),
              std::move(earlier_move_events), valid.drill_groups(),
              valid.dance_move_drills(), std::move(earlier_showtimes));
  assert(has_diagnostic(otgy::core::validate_finished_chart(orphaned_stage),
                        ChartDiagnosticCode::InvalidShowtime));

  auto double_owned = valid.showtimes();
  double_owned[1].dance_move_drill_ids.push_back(
      double_owned[0].dance_move_drill_ids.front());
  const auto double_owned_draft =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              valid.showtime_move_events(), valid.drill_groups(),
              valid.dance_move_drills(), std::move(double_owned));
  assert(has_diagnostic(
      otgy::core::validate_finished_chart(double_owned_draft),
      ChartDiagnosticCode::OrphanDanceMoveDrill));
}

/// A Showtime may reference fewer moves than its stage holds: the user can
/// delete trailing SHOW-D entries, leaving the earlier ones untouched. The
/// unshown moves stay free but must not sit between the recorded moves.
void test_reduced_showtime_allows_free_moves() {
  const auto valid =
      draft_from_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  assert(valid.showtimes().front().dance_move_drill_ids.size() > 1U);

  auto start_events = valid.showtime_start_events();
  auto move_events = valid.showtime_move_events();
  auto showtimes = valid.showtimes();
  const auto dropped_move = showtimes.front().show_move_event_ids.back();
  showtimes.front().show_move_event_ids.pop_back();
  showtimes.front().dance_move_drill_ids.pop_back();
  move_events.erase(
      std::remove_if(move_events.begin(), move_events.end(),
                     [&dropped_move](const auto& event) {
                       return event.id.value == dropped_move.value;
                     }),
      move_events.end());
  const auto reduced =
      rebuild(valid, valid.role_events(), std::move(start_events),
              std::move(move_events), valid.drill_groups(),
              valid.dance_move_drills(), std::move(showtimes));
  const auto reduced_result = otgy::core::validate_finished_chart(reduced);
  assert(reduced_result.is_finished());
  assert(reduced_result.finished_chart()->draft().dance_move_drills().size() ==
         valid.dance_move_drills().size());

  // Dropping the leading entry instead leaves a recorded move after a free
  // move, so the recorded list is no longer the stage prefix.
  auto middle_events = valid.showtime_move_events();
  auto middle_showtimes = valid.showtimes();
  const auto dropped_leading =
      middle_showtimes.front().show_move_event_ids.front();
  middle_showtimes.front().show_move_event_ids.erase(
      middle_showtimes.front().show_move_event_ids.begin());
  middle_showtimes.front().dance_move_drill_ids.erase(
      middle_showtimes.front().dance_move_drill_ids.begin());
  middle_events.erase(
      std::remove_if(middle_events.begin(), middle_events.end(),
                     [&dropped_leading](const auto& event) {
                       return event.id.value == dropped_leading.value;
                     }),
      middle_events.end());
  const auto broken_prefix =
      rebuild(valid, valid.role_events(), valid.showtime_start_events(),
              std::move(middle_events), valid.drill_groups(),
              valid.dance_move_drills(), std::move(middle_showtimes));
  assert(has_diagnostic(otgy::core::validate_finished_chart(broken_prefix),
                        ChartDiagnosticCode::InvalidShowtime));
}

void test_diagnostic_messages() {
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  ChartDiagnosticCode::OrphanDanceMoveDrill);
       ++raw) {
    assert(!otgy::core::chart_diagnostic_message(
                static_cast<ChartDiagnosticCode>(raw))
                .empty());
  }
  assert(!otgy::core::chart_diagnostic_message(
              static_cast<ChartDiagnosticCode>(255))
              .empty());
}

}  // namespace

int main() {
  test_imported_drafts_validate_and_snapshot_ownership();
  test_empty_duplicate_and_orphan_diagnostics();
  test_block_dance_and_showtime_diagnostics_without_mutation();
  test_finished_snapshot_requires_owned_and_ordered_showtime_events();
  test_showtime_less_trailing_drills_are_finished();
  test_reduced_showtime_allows_free_moves();
  test_diagnostic_messages();
  return 0;
}
