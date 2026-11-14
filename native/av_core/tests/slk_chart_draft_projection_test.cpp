// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_validator.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartDraft;
using otgy::core::ChartEventId;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::FinishedSlkDocument;
using otgy::core::SlkChartDraftProjectionError;
using otgy::core::SlkChartDraftProjectionResult;
using otgy::core::SlkParsedRow;
using otgy::core::SlkRowType;

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] FinishedSlkDocument import_file(const std::string& path) {
  const auto bytes = read_file(path);
  auto result = otgy::core::import_finished_slk(bytes.data(), bytes.size());
  assert(result);
  return std::move(result).take_document();
}

[[nodiscard]] const ChartDraft& draft_of(
    const SlkChartDraftProjectionResult& result) {
  assert(result);
  assert(result.draft() != nullptr);
  assert(result.failure() == nullptr);
  return *result.draft();
}

[[nodiscard]] const ChartRoleEvent& role_event(const ChartDraft& draft,
                                               ChartEventId id) {
  for (const auto& event : draft.role_events()) {
    if (event.id.value == id.value) {
      return event;
    }
  }
  assert(false);
  return draft.role_events().front();
}

void assert_event_ids_unique_and_boundary_absent(
    const ChartDraft& draft, std::uint64_t boundary_sheet_row) {
  std::set<std::uint64_t> ids;
  for (const auto& event : draft.role_events()) {
    assert(ids.insert(event.id.value).second);
    assert(event.id.value != boundary_sheet_row);
  }
  for (const auto& event : draft.showtime_start_events()) {
    assert(ids.insert(event.id.value).second);
    assert(event.id.value != boundary_sheet_row);
  }
  for (const auto& event : draft.showtime_move_events()) {
    assert(ids.insert(event.id.value).second);
    assert(event.id.value != boundary_sheet_row);
  }
}

void assert_relations(const ChartDraft& draft) {
  for (const auto& group : draft.drill_groups()) {
    assert(!group.npc_event_ids.empty());
    assert(!group.player_event_ids.empty());
    for (const auto event_id : group.npc_event_ids) {
      const auto& event = role_event(draft, event_id);
      assert(event.role == ChartRole::Npc);
      assert(event.drill_group_id.value == group.id.value);
      assert(event.dance_move_drill_id.value ==
             group.dance_move_drill_id.value);
    }
    for (const auto event_id : group.player_event_ids) {
      const auto& event = role_event(draft, event_id);
      assert(event.role == ChartRole::Player);
      assert(event.drill_group_id.value == group.id.value);
      assert(event.dance_move_drill_id.value ==
             group.dance_move_drill_id.value);
    }
  }

  for (const auto& showtime : draft.showtimes()) {
    assert(!showtime.dance_move_drill_ids.empty());
    assert(!showtime.show_move_event_ids.empty());
    assert(showtime.show_move_event_ids.size() <=
           showtime.dance_move_drill_ids.size());
    for (std::size_t index = 0U; index < showtime.show_move_event_ids.size();
         ++index) {
      const auto event_id = showtime.show_move_event_ids[index];
      bool found = false;
      for (const auto& event : draft.showtime_move_events()) {
        if (event.id.value == event_id.value) {
          assert(event.showtime_id.value == showtime.id.value);
          assert(event.dance_move_drill_id.value ==
                 showtime.dance_move_drill_ids[index].value);
          found = true;
          break;
        }
      }
      assert(found);
    }
  }
}

void test_prefix_showtime_projection_and_validation() {
  const auto document =
      import_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto showtimes = document.semantics().showtimes();
  assert(showtimes.front().stage.drills().size() == 6U);
  assert(showtimes.front().show_d_rows.size() == 6U);
  showtimes.front().show_d_rows.resize(1U);
  FinishedSlkDocument prefix_document{otgy::core::SlkShowtimeAnalysis{
      std::move(showtimes), {}, document.semantics().boundary_row(),
      document.semantics().boundary_source_row_index()}};

  const auto result =
      otgy::core::project_finished_slk_to_chart_draft(prefix_document);
  const auto& draft = draft_of(result);
  assert(draft.showtimes().front().dance_move_drill_ids.size() == 6U);
  assert(draft.showtimes().front().show_move_event_ids.size() == 1U);
  const auto validation = otgy::core::validate_finished_chart(draft);
  assert(validation.is_finished());
}

void test_canonical_projection_and_ownership() {
  const auto document =
      import_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto result = otgy::core::project_finished_slk_to_chart_draft(document);
  auto draft = std::move(result).take_draft();

  assert(draft.role_events().size() == 408U);
  assert(draft.showtime_start_events().size() == 2U);
  assert(draft.showtime_move_events().size() == 17U);
  assert(draft.drill_groups().size() == 34U);
  assert(draft.dance_move_drills().size() == 17U);
  assert(draft.showtimes().size() == 2U);
  assert_event_ids_unique_and_boundary_absent(draft, 430U);
  assert_relations(draft);

  const auto& first = draft.role_events().front();
  assert(first.id.value == 3U);
  assert(first.row == 206U);
  assert(first.role == ChartRole::Npc);
  assert(first.type == ChartRoleEventType::BlockStart);
  assert(first.drill_group_id.value == 3U);
  assert(first.dance_move_drill_id.value == 3U);
  for (std::size_t index = 1U; index < draft.role_events().size(); ++index) {
    assert(draft.role_events()[index - 1U].row <
           draft.role_events()[index].row);
  }

  assert(draft.showtime_start_events()[0].id.value == 155U);
  assert(draft.showtime_start_events()[1].id.value == 418U);
  assert(draft.showtime_move_events()[0].show_n_count == 11U);
  assert(draft.showtime_move_events()[0].dance_move_drill_id.value == 3U);

  const auto copy = draft;
  const auto moved = std::move(draft);
  assert(copy.role_events().size() == 408U);
  assert(moved.role_events().size() == 408U);
  assert(copy.role_events().data() != moved.role_events().data());
  assert(copy.drill_groups()[0].npc_event_ids.data() !=
         moved.drill_groups()[0].npc_event_ids.data());
}

void test_compatibility_projection() {
  const auto document =
      import_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto result = otgy::core::project_finished_slk_to_chart_draft(document);
  const auto& draft = draft_of(result);
  assert(draft.role_events().size() == 384U);
  assert(draft.showtime_start_events().size() == 2U);
  assert(draft.showtime_move_events().size() == 17U);
  assert(draft.drill_groups().size() == 34U);
  assert(draft.dance_move_drills().size() == 17U);
  assert(draft.showtimes().size() == 2U);
  assert_event_ids_unique_and_boundary_absent(draft, 406U);
  assert_relations(draft);
  assert(draft.showtime_start_events()[0].id.value == 183U);
  assert(draft.showtime_start_events()[1].id.value == 396U);
}

void test_trailing_only_projection_and_validation() {
  const auto document =
      import_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto trailing = document.semantics().showtimes().front().stage.drills();
  assert(trailing.size() == 6U);
  FinishedSlkDocument trailing_document{otgy::core::SlkShowtimeAnalysis{
      {}, trailing, document.semantics().boundary_row(),
      document.semantics().boundary_source_row_index()}};

  const auto result =
      otgy::core::project_finished_slk_to_chart_draft(trailing_document);
  const auto& draft = draft_of(result);
  assert(draft.showtimes().empty());
  assert(draft.showtime_start_events().empty());
  assert(draft.showtime_move_events().empty());
  assert(draft.dance_move_drills().size() == 6U);
  assert(draft.drill_groups().size() == 14U);
  const auto validation = otgy::core::validate_finished_chart(draft);
  assert(validation.is_finished());
}

void test_invalid_finished_document() {
  const auto position = otgy::core::decompose_slk_position(0U);
  assert(position);
  const SlkParsedRow boundary{3U, *position.position(), SlkRowType::R, 0U};
  const FinishedSlkDocument empty{
      otgy::core::SlkShowtimeAnalysis{{}, {}, boundary, 0U}};
  const auto result = otgy::core::project_finished_slk_to_chart_draft(empty);
  assert(!result);
  assert(result.draft() == nullptr);
  assert(result.failure() != nullptr);
  assert(result.failure()->error ==
         SlkChartDraftProjectionError::InvalidFinishedDocument);
  assert(result.failure()->source_sheet_row == 3U);
}

void test_error_messages() {
  assert(otgy::core::slk_chart_draft_projection_error_message(
             SlkChartDraftProjectionError::None)
             .empty());
  assert(!otgy::core::slk_chart_draft_projection_error_message(
              SlkChartDraftProjectionError::InvalidFinishedDocument)
              .empty());
  assert(!otgy::core::slk_chart_draft_projection_error_message(
              SlkChartDraftProjectionError::AllocationFailure)
              .empty());
  assert(!otgy::core::slk_chart_draft_projection_error_message(
              static_cast<SlkChartDraftProjectionError>(255))
              .empty());
}

}  // namespace

int main() {
  test_canonical_projection_and_ownership();
  test_compatibility_projection();
  test_prefix_showtime_projection_and_validation();
  test_trailing_only_projection_and_validation();
  test_invalid_finished_document();
  test_error_messages();
  return 0;
}
