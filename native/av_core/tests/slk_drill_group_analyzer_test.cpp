// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_drill_group_analyzer.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::SlkDrillBlockRole;
using otgy::core::SlkDrillGroupAnalysis;
using otgy::core::SlkDrillGroupAnalysisResult;
using otgy::core::SlkDrillGroupError;
using otgy::core::SlkOrdinarySequenceEnd;
using otgy::core::SlkParsedRecords;
using otgy::core::SlkParsedRow;
using otgy::core::SlkRowType;

[[nodiscard]] SlkParsedRow row(std::uint64_t sheet_row,
                               std::uint64_t total_sixteenth, SlkRowType type,
                               std::uint64_t value) {
  const auto position = otgy::core::decompose_slk_position(total_sixteenth);
  assert(position);
  return {sheet_row, *position.position(), type, value};
}

[[nodiscard]] SlkParsedRecords records(std::vector<SlkParsedRow> rows) {
  return SlkParsedRecords{std::move(rows)};
}

[[nodiscard]] const SlkDrillGroupAnalysis& analysis_of(
    const SlkDrillGroupAnalysisResult& result) {
  assert(result);
  assert(result.analysis() != nullptr);
  assert(result.failure() == nullptr);
  return *result.analysis();
}

void assert_error(const SlkParsedRecords& parsed, SlkDrillGroupError expected,
                  std::size_t expected_source_row,
                  SlkDrillBlockRole expected_role,
                  std::size_t expected_block_row = 0U) {
  const auto result = otgy::core::analyze_slk_drill_groups(parsed);
  assert(!result);
  assert(result.analysis() == nullptr);
  assert(result.failure() != nullptr);
  assert(result.failure()->error == expected);
  assert(result.failure()->source_row_index == expected_source_row);
  assert(result.failure()->role == expected_role);
  assert(result.failure()->block_row_index == expected_block_row);
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] SlkParsedRecords parse_file(const std::string& path) {
  const auto bytes = read_file(path);
  auto result = otgy::core::parse_slk_records(bytes.data(), bytes.size());
  assert(result);
  return std::move(result).take_records();
}

[[nodiscard]] std::size_t next_ordinary_begin(const SlkParsedRecords& parsed,
                                              std::size_t sequence_end) {
  auto index = sequence_end + 1U;
  while (index < parsed.rows().size() &&
         parsed.rows()[index].type == SlkRowType::D) {
    ++index;
  }
  assert(index < parsed.rows().size());
  return index;
}

void test_matching_groups_roles_mapping_and_ownership() {
  auto analysis = [] {
    const auto parsed = records({
        row(3U, 0U, SlkRowType::C, 2U),
        row(4U, 2U, SlkRowType::N, 0U),
        row(5U, 5U, SlkRowType::N, 0U),
        row(6U, 16U, SlkRowType::C, 2U),
        row(7U, 20U, SlkRowType::N, 0U),
        row(8U, 23U, SlkRowType::N, 0U),
        row(9U, 40U, SlkRowType::C, 3U),
        row(10U, 41U, SlkRowType::N, 0U),
        row(11U, 44U, SlkRowType::N, 0U),
        row(12U, 46U, SlkRowType::S, 0U),
        row(13U, 52U, SlkRowType::C, 3U),
        row(14U, 53U, SlkRowType::N, 0U),
        row(15U, 56U, SlkRowType::N, 0U),
        row(16U, 58U, SlkRowType::S, 0U),
        row(17U, 70U, SlkRowType::C, 0U),
        row(18U, 72U, SlkRowType::D, 3U),
        row(19U, 100U, SlkRowType::R, 0U),
    });
    auto result = otgy::core::analyze_slk_drill_groups(parsed);
    assert(result);
    return std::move(result).take_analysis();
  }();

  assert(analysis.begin_row_index() == 0U);
  assert(analysis.next_row_index() == 14U);
  assert(analysis.end() == SlkOrdinarySequenceEnd::Showtime);
  assert(analysis.groups().size() == 2U);

  const auto& first = analysis.groups()[0];
  assert(first.id.value == 3U);
  assert(first.npc.role == SlkDrillBlockRole::Npc);
  assert(first.player.role == SlkDrillBlockRole::Player);
  assert(first.npc.rows.size() == 3U);
  assert(first.player.rows.size() == 3U);
  assert(first.npc.rows[2].source_row_index == 2U);
  assert(first.player.rows[2].source_row_index == 5U);
  assert(first.npc.rows[2].block_row_index == 2U);
  assert(first.player.rows[2].block_row_index == 2U);
  for (const auto& mapped : first.npc.rows) {
    assert(mapped.drill_group_id.value == first.id.value);
    assert(mapped.role == SlkDrillBlockRole::Npc);
  }
  for (const auto& mapped : first.player.rows) {
    assert(mapped.drill_group_id.value == first.id.value);
    assert(mapped.role == SlkDrillBlockRole::Player);
  }

  const auto& second = analysis.groups()[1];
  assert(second.id.value == 9U);
  assert(second.npc.rows.size() == 4U);
  assert(second.player.rows.size() == 4U);
  assert(second.npc.rows.back().row.type == SlkRowType::S);
  assert(second.player.rows.back().row.type == SlkRowType::S);

  const auto copy = analysis;
  const auto moved = std::move(analysis);
  assert(copy.groups().size() == 2U);
  assert(moved.groups().size() == 2U);
  assert(copy.groups()[0].npc.rows.data() != moved.groups()[0].npc.rows.data());
}

void test_boundaries_and_repository_files() {
  const auto boundary_records = records({
      row(3U, 0U, SlkRowType::C, 1U),
      row(4U, 1U, SlkRowType::N, 0U),
      row(5U, 4U, SlkRowType::C, 1U),
      row(6U, 5U, SlkRowType::N, 0U),
      row(7U, 16U, SlkRowType::R, 0U),
  });
  const auto boundary_result =
      otgy::core::analyze_slk_drill_groups(boundary_records);
  const auto& boundary = analysis_of(boundary_result);
  assert(boundary.groups().size() == 1U);
  assert(boundary.next_row_index() == 4U);
  assert(boundary.end() == SlkOrdinarySequenceEnd::FileBoundary);

  const auto empty_showtime = records({
      row(3U, 0U, SlkRowType::C, 0U),
      row(4U, 1U, SlkRowType::R, 0U),
  });
  const auto empty_showtime_result =
      otgy::core::analyze_slk_drill_groups(empty_showtime);
  const auto& empty_showtime_analysis = analysis_of(empty_showtime_result);
  assert(empty_showtime_analysis.groups().empty());
  assert(empty_showtime_analysis.next_row_index() == 0U);
  assert(empty_showtime_analysis.end() == SlkOrdinarySequenceEnd::Showtime);

  const auto empty_boundary = records({
      row(3U, 0U, SlkRowType::R, 0U),
  });
  const auto empty_boundary_result =
      otgy::core::analyze_slk_drill_groups(empty_boundary);
  const auto& empty_boundary_analysis = analysis_of(empty_boundary_result);
  assert(empty_boundary_analysis.groups().empty());
  assert(empty_boundary_analysis.end() == SlkOrdinarySequenceEnd::FileBoundary);

  const auto canonical =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto canonical_result = otgy::core::analyze_slk_drill_groups(canonical);
  const auto& canonical_analysis = analysis_of(canonical_result);
  assert(canonical_analysis.groups().size() == 14U);
  assert(canonical_analysis.next_row_index() == 152U);
  assert(canonical.rows()[152U].sheet_row == 155U);
  assert(canonical_analysis.end() == SlkOrdinarySequenceEnd::Showtime);
  const auto canonical_second_begin =
      next_ordinary_begin(canonical, canonical_analysis.next_row_index());
  assert(canonical_second_begin == 159U);
  assert(canonical.rows()[canonical_second_begin].sheet_row == 162U);
  const auto canonical_second_result =
      otgy::core::analyze_slk_drill_groups(canonical, canonical_second_begin);
  const auto& canonical_second = analysis_of(canonical_second_result);
  assert(canonical_second.groups().size() == 20U);
  assert(canonical_second.next_row_index() == 415U);
  assert(canonical.rows()[415U].sheet_row == 418U);
  assert(canonical_second.end() == SlkOrdinarySequenceEnd::Showtime);
  const auto canonical_boundary_index =
      next_ordinary_begin(canonical, canonical_second.next_row_index());
  const auto canonical_boundary_result =
      otgy::core::analyze_slk_drill_groups(canonical, canonical_boundary_index);
  const auto& canonical_boundary = analysis_of(canonical_boundary_result);
  assert(canonical_boundary.groups().empty());
  assert(canonical_boundary.end() == SlkOrdinarySequenceEnd::FileBoundary);

  const auto compatibility =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_result =
      otgy::core::analyze_slk_drill_groups(compatibility);
  const auto& compatibility_analysis = analysis_of(compatibility_result);
  assert(compatibility_analysis.groups().size() == 16U);
  assert(compatibility_analysis.next_row_index() == 180U);
  assert(compatibility.rows()[180U].sheet_row == 183U);
  assert(compatibility_analysis.end() == SlkOrdinarySequenceEnd::Showtime);
  const auto compatibility_second_begin = next_ordinary_begin(
      compatibility, compatibility_analysis.next_row_index());
  assert(compatibility_second_begin == 189U);
  assert(compatibility.rows()[compatibility_second_begin].sheet_row == 192U);
  const auto compatibility_second_result = otgy::core::analyze_slk_drill_groups(
      compatibility, compatibility_second_begin);
  const auto& compatibility_second = analysis_of(compatibility_second_result);
  assert(compatibility_second.groups().size() == 18U);
  assert(compatibility_second.next_row_index() == 393U);
  assert(compatibility.rows()[393U].sheet_row == 396U);
  assert(compatibility_second.end() == SlkOrdinarySequenceEnd::Showtime);
  const auto compatibility_boundary_index =
      next_ordinary_begin(compatibility, compatibility_second.next_row_index());
  const auto compatibility_boundary_result =
      otgy::core::analyze_slk_drill_groups(compatibility,
                                           compatibility_boundary_index);
  const auto& compatibility_boundary =
      analysis_of(compatibility_boundary_result);
  assert(compatibility_boundary.groups().empty());
  assert(compatibility_boundary.end() == SlkOrdinarySequenceEnd::FileBoundary);
}

void test_structure_and_pair_mismatch_errors() {
  assert_error(
      records({row(3U, 0U, SlkRowType::N, 0U), row(4U, 1U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::ExpectedBlockStart, 0U, SlkDrillBlockRole::Npc);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 1U), row(4U, 1U, SlkRowType::C, 1U),
               row(5U, 2U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::MissingNRows, 1U, SlkDrillBlockRole::Npc, 1U);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 1U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 2U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::MissingPlayerBlock, 2U, SlkDrillBlockRole::Player);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 2U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 4U, SlkRowType::C, 2U), row(6U, 5U, SlkRowType::N, 0U),
               row(7U, 6U, SlkRowType::N, 0U), row(8U, 8U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::BlockLengthMismatch, 4U, SlkDrillBlockRole::Player,
      2U);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 1U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 2U, SlkRowType::S, 0U), row(6U, 4U, SlkRowType::C, 1U),
               row(7U, 5U, SlkRowType::N, 0U), row(8U, 6U, SlkRowType::N, 0U),
               row(9U, 8U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::BlockTypeMismatch, 5U, SlkDrillBlockRole::Player, 2U);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 2U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 4U, SlkRowType::C, 3U), row(6U, 5U, SlkRowType::N, 0U),
               row(7U, 8U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::BlockValueMismatch, 2U, SlkDrillBlockRole::Player);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 2U), row(4U, 2U, SlkRowType::N, 0U),
               row(5U, 5U, SlkRowType::N, 0U), row(6U, 8U, SlkRowType::C, 2U),
               row(7U, 11U, SlkRowType::N, 0U), row(8U, 15U, SlkRowType::N, 0U),
               row(9U, 20U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::BlockRelativePositionMismatch, 5U,
      SlkDrillBlockRole::Player, 2U);
}

void test_stage_cursor_and_termination_errors() {
  const auto later_stage = records({
      row(3U, 0U, SlkRowType::C, 0U),
      row(4U, 1U, SlkRowType::D, 1U),
      row(5U, 16U, SlkRowType::C, 1U),
      row(6U, 17U, SlkRowType::N, 0U),
      row(7U, 20U, SlkRowType::C, 1U),
      row(8U, 21U, SlkRowType::N, 0U),
      row(9U, 32U, SlkRowType::R, 0U),
  });
  const auto later_result =
      otgy::core::analyze_slk_drill_groups(later_stage, 2U);
  const auto& later = analysis_of(later_result);
  assert(later.begin_row_index() == 2U);
  assert(later.next_row_index() == 6U);
  assert(later.groups().size() == 1U);
  assert(later.groups()[0].id.value == 5U);

  const auto invalid_begin =
      otgy::core::analyze_slk_drill_groups(later_stage, 7U);
  assert(!invalid_begin);
  assert(invalid_begin.failure()->error == SlkDrillGroupError::InvalidBeginRow);

  assert_error(
      records({row(3U, 0U, SlkRowType::C, 1U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 2U, SlkRowType::D, 1U), row(6U, 3U, SlkRowType::R, 0U)}),
      SlkDrillGroupError::ExpectedBlockStart, 2U, SlkDrillBlockRole::Player);

  const auto end_of_records = otgy::core::analyze_slk_drill_groups(
      records({row(3U, 0U, SlkRowType::C, 1U), row(4U, 1U, SlkRowType::N, 0U),
               row(5U, 4U, SlkRowType::C, 1U), row(6U, 5U, SlkRowType::N, 0U)}),
      0U);
  assert(end_of_records);
  assert(end_of_records.analysis()->end() ==
         SlkOrdinarySequenceEnd::EndOfRecords);
  assert(end_of_records.analysis()->groups().size() == 1U);
}

void test_error_messages() {
  assert(otgy::core::slk_drill_group_error_message(SlkDrillGroupError::None)
             .empty());
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(SlkDrillGroupError::AllocationFailure);
       ++raw) {
    assert(!otgy::core::slk_drill_group_error_message(
                static_cast<SlkDrillGroupError>(raw))
                .empty());
  }
  assert(!otgy::core::slk_drill_group_error_message(
              static_cast<SlkDrillGroupError>(255))
              .empty());
}

}  // namespace

int main() {
  test_matching_groups_roles_mapping_and_ownership();
  test_boundaries_and_repository_files();
  test_structure_and_pair_mismatch_errors();
  test_stage_cursor_and_termination_errors();
  test_error_messages();
  return 0;
}
