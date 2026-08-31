// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_validator.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartDiagnosticCode;
using otgy::core::ChartDraft;
using otgy::core::ChartValidationResult;

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
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

void test_diagnostic_messages() {
  for (unsigned int raw = 0U;
       raw <=
       static_cast<unsigned int>(ChartDiagnosticCode::OrphanDanceMoveDrill);
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
  test_diagnostic_messages();
  return 0;
}
