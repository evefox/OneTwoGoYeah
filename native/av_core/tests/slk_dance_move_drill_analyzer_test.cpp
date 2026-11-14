// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_dance_move_drill_analyzer.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::SlkDanceMoveDrillAnalysis;
using otgy::core::SlkDanceMoveDrillAnalysisResult;
using otgy::core::SlkDanceMoveDrillError;
using otgy::core::SlkDrillBlock;
using otgy::core::SlkDrillBlockRole;
using otgy::core::SlkDrillGroup;
using otgy::core::SlkDrillGroupAnalysis;
using otgy::core::SlkDrillGroupId;
using otgy::core::SlkDrillGroupRow;
using otgy::core::SlkOrdinarySequenceEnd;
using otgy::core::SlkParsedRecords;
using otgy::core::SlkParsedRow;
using otgy::core::SlkRowType;

[[nodiscard]] SlkParsedRow parsed_row(std::uint64_t sheet_row,
                                      std::uint64_t total_sixteenth,
                                      SlkRowType type, std::uint64_t value) {
  const auto position = otgy::core::decompose_slk_position(total_sixteenth);
  assert(position);
  return {sheet_row, *position.position(), type, value};
}

[[nodiscard]] SlkDrillBlock block(SlkDrillBlockRole role,
                                  SlkDrillGroupId group_id,
                                  std::uint64_t c_value, std::uint64_t n_count,
                                  bool has_end, std::size_t source_row_begin) {
  SlkDrillBlock result{role, {}};
  const auto base =
      group_id.value * 32U + (role == SlkDrillBlockRole::Player ? 16U : 0U);
  const auto sheet_base =
      group_id.value + (role == SlkDrillBlockRole::Player ? 100U : 0U);
  result.rows.push_back({parsed_row(sheet_base, base, SlkRowType::C, c_value),
                         source_row_begin, 0U, group_id, role});
  for (std::uint64_t index = 0U; index < n_count; ++index) {
    result.rows.push_back(
        {parsed_row(sheet_base + index + 1U, base + index + 1U, SlkRowType::N,
                    0U),
         source_row_begin + static_cast<std::size_t>(index) + 1U,
         static_cast<std::size_t>(index) + 1U, group_id, role});
  }
  if (has_end) {
    result.rows.push_back(
        {parsed_row(sheet_base + n_count + 1U, base + n_count + 1U,
                    SlkRowType::S, 0U),
         source_row_begin + static_cast<std::size_t>(n_count) + 1U,
         static_cast<std::size_t>(n_count) + 1U, group_id, role});
  }
  return result;
}

[[nodiscard]] SlkDrillGroup group(std::uint64_t id, std::uint64_t c_value,
                                  std::uint64_t n_count, bool has_end) {
  const SlkDrillGroupId group_id{id};
  return {group_id,
          block(SlkDrillBlockRole::Npc, group_id, c_value, n_count, has_end,
                id * 10U),
          block(SlkDrillBlockRole::Player, group_id, c_value, n_count, has_end,
                id * 10U + 100U)};
}

[[nodiscard]] SlkDrillGroupAnalysis group_analysis(
    std::vector<SlkDrillGroup> groups, std::size_t begin_row_index = 7U,
    std::size_t next_row_index = 99U,
    SlkOrdinarySequenceEnd end = SlkOrdinarySequenceEnd::Showtime) {
  return SlkDrillGroupAnalysis{std::move(groups), begin_row_index,
                               next_row_index, end};
}

[[nodiscard]] const SlkDanceMoveDrillAnalysis& analysis_of(
    const SlkDanceMoveDrillAnalysisResult& result) {
  assert(result);
  assert(result.analysis() != nullptr);
  assert(result.failure() == nullptr);
  return *result.analysis();
}

void assert_error(std::vector<SlkDrillGroup> groups,
                  SlkDanceMoveDrillError expected,
                  std::size_t expected_source_group,
                  std::size_t expected_drill_group = 0U) {
  const auto input = group_analysis(std::move(groups));
  const auto result = otgy::core::analyze_slk_dance_move_drills(input);
  assert(!result);
  assert(result.analysis() == nullptr);
  assert(result.failure() != nullptr);
  assert(result.failure()->error == expected);
  assert(result.failure()->source_group_index == expected_source_group);
  assert(result.failure()->drill_group_index == expected_drill_group);
  if (expected_source_group < input.groups().size()) {
    const auto& source = input.groups()[expected_source_group];
    assert(result.failure()->drill_group_id.value == source.id.value);
    if (source.npc.rows.empty()) {
      assert(result.failure()->sheet_row == 0U);
      assert(result.failure()->c_value == 0U);
    } else {
      assert(result.failure()->sheet_row ==
             source.npc.rows.front().row.sheet_row);
      assert(result.failure()->c_value == source.npc.rows.front().row.value);
    }
  } else {
    assert(result.failure()->drill_group_id.value == 0U);
    assert(result.failure()->sheet_row == 0U);
    assert(result.failure()->c_value == 0U);
  }
}

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
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

[[nodiscard]] SlkDrillGroupAnalysis analyze_group_sequence(
    const SlkParsedRecords& parsed, std::size_t begin) {
  auto result = otgy::core::analyze_slk_drill_groups(parsed, begin);
  assert(result);
  return std::move(result).take_analysis();
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

void assert_move_counts(const SlkDanceMoveDrillAnalysis& analysis,
                        const std::vector<std::uint64_t>& expected) {
  assert(analysis.drills().size() == expected.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    assert(analysis.drills()[index].move_n_count == expected[index]);
    assert(analysis.drills()[index].npc.rows.size() == expected[index] + 1U);
    assert(analysis.drills()[index].player.rows.size() == expected[index] + 1U);
  }
}

void test_unsplit_and_split_drills_mapping_and_ownership() {
  auto result = otgy::core::analyze_slk_dance_move_drills(group_analysis({
      group(3U, 6U, 6U, true),
      group(20U, 308U, 3U, false),
      group(30U, 208U, 2U, false),
      group(40U, 308U, 3U, true),
  }));
  auto analysis = std::move(result).take_analysis();
  assert(analysis.begin_row_index() == 7U);
  assert(analysis.next_row_index() == 99U);
  assert(analysis.end() == SlkOrdinarySequenceEnd::Showtime);
  assert(analysis.drills().size() == 2U);

  const auto& unsplit = analysis.drills()[0];
  assert(unsplit.id.value == 3U);
  assert(unsplit.move_n_count == 6U);
  assert(unsplit.groups.size() == 1U);
  assert(unsplit.groups[0].segment_n_count == 6U);
  assert(unsplit.npc.rows.size() == 7U);
  assert(unsplit.player.rows.size() == 7U);

  const auto& split = analysis.drills()[1];
  assert(split.id.value == 20U);
  assert(split.move_n_count == 8U);
  assert(split.groups.size() == 3U);
  assert(split.groups[0].segment_n_count == 3U);
  assert(split.groups[1].segment_n_count == 2U);
  assert(split.groups[2].segment_n_count == 3U);
  assert(split.groups[0].source_group_index == 1U);
  assert(split.groups[2].drill_group_index == 2U);
  assert(split.groups[0].npc_c_row.row.type == SlkRowType::C);
  assert(split.groups[0].npc_c_row.role == SlkDrillBlockRole::Npc);
  assert(split.groups[0].player_c_row.row.type == SlkRowType::C);
  assert(split.groups[0].player_c_row.role == SlkDrillBlockRole::Player);
  assert(split.npc.rows.size() == 9U);
  assert(split.player.rows.size() == 9U);
  assert(split.npc.rows.back().row.row.type == SlkRowType::S);
  assert(split.player.rows.back().row.row.type == SlkRowType::S);
  for (std::size_t index = 0U; index < split.npc.rows.size(); ++index) {
    assert(split.npc.rows[index].dance_move_row_index == index);
    assert(split.npc.rows[index].dance_move_drill_id.value == 20U);
    assert(split.npc.rows[index].role == SlkDrillBlockRole::Npc);
    assert(split.player.rows[index].dance_move_row_index == index);
    assert(split.player.rows[index].dance_move_drill_id.value == 20U);
    assert(split.player.rows[index].role == SlkDrillBlockRole::Player);
  }

  const auto copy = analysis;
  const auto moved = std::move(analysis);
  assert(copy.drills().size() == 2U);
  assert(moved.drills().size() == 2U);
  assert(copy.drills()[1].npc.rows.data() != moved.drills()[1].npc.rows.data());
}

void test_all_unsplit_counts() {
  std::vector<SlkDrillGroup> groups;
  for (std::uint64_t count = 1U; count <= 11U; ++count) {
    groups.push_back(group(count * 20U, count, count, true));
  }
  const auto input = group_analysis(std::move(groups));
  const auto result = otgy::core::analyze_slk_dance_move_drills(input);
  const auto& analysis = analysis_of(result);
  assert(analysis.drills().size() == 11U);
  for (std::uint64_t count = 1U; count <= 11U; ++count) {
    const auto& drill = analysis.drills()[count - 1U];
    assert(drill.move_n_count == count);
    assert(drill.groups.size() == 1U);
    assert(drill.npc.rows.size() == count + 1U);
  }
}

void test_two_digit_segment_count() {
  const auto input = group_analysis({
      group(3U, 1'011U, 10U, false),
      group(20U, 111U, 1U, true),
  });
  const auto result = otgy::core::analyze_slk_dance_move_drills(input);
  const auto& analysis = analysis_of(result);
  assert(analysis.drills().size() == 1U);
  assert(analysis.drills()[0].move_n_count == 11U);
  assert(analysis.drills()[0].groups.size() == 2U);
  assert(analysis.drills()[0].groups[0].segment_n_count == 10U);
  assert(analysis.drills()[0].groups[1].segment_n_count == 1U);
}

void test_c_value_count_and_encoding_errors() {
  assert_error({}, SlkDanceMoveDrillError::EmptySequence, 0U);
  assert_error({group(3U, 12U, 1U, true)},
               SlkDanceMoveDrillError::InvalidCValue, 0U);
  assert_error({group(3U, 100U, 1U, true)},
               SlkDanceMoveDrillError::InvalidCValue, 0U);
  assert_error({group(3U, 509U, 4U, false)},
               SlkDanceMoveDrillError::SegmentNCountMismatch, 0U);
  assert_error({group(3U, 509U, 5U, false), group(20U, 4U, 4U, true)},
               SlkDanceMoveDrillError::InconsistentEncoding, 1U, 1U);
  assert_error({group(3U, 509U, 5U, false), group(20U, 408U, 4U, true)},
               SlkDanceMoveDrillError::InconsistentMoveNCount, 1U, 1U);
  assert_error({group(3U, 609U, 6U, false), group(20U, 409U, 4U, true)},
               SlkDanceMoveDrillError::SegmentTotalExceedsMoveNCount, 1U, 1U);
}

void test_move_end_errors() {
  assert_error({group(3U, 509U, 5U, true), group(20U, 409U, 4U, true)},
               SlkDanceMoveDrillError::PrematureMoveEnd, 0U);
  assert_error({group(3U, 509U, 5U, false), group(20U, 409U, 4U, false)},
               SlkDanceMoveDrillError::MissingMoveEnd, 1U, 1U);
  assert_error({group(3U, 509U, 5U, false)},
               SlkDanceMoveDrillError::IncompleteMoveNCount, 1U, 1U);
  assert_error({group(3U, 909U, 9U, true)},
               SlkDanceMoveDrillError::SplitDrillRequiresMultipleGroups, 0U);

  SlkDrillGroup invalid{
      {3U}, {SlkDrillBlockRole::Npc, {}}, {SlkDrillBlockRole::Player, {}}};
  assert_error({std::move(invalid)}, SlkDanceMoveDrillError::InvalidDrillGroup,
               0U);
}

void test_repository_sequences() {
  const auto canonical =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto canonical_first_groups = analyze_group_sequence(canonical, 0U);
  const auto canonical_first_result =
      otgy::core::analyze_slk_dance_move_drills(canonical_first_groups);
  const auto& canonical_first = analysis_of(canonical_first_result);
  assert_move_counts(canonical_first, {11U, 10U, 8U, 9U, 9U, 9U});

  const auto canonical_second_begin =
      next_ordinary_begin(canonical, canonical_first_groups.next_row_index());
  const auto canonical_second_groups =
      analyze_group_sequence(canonical, canonical_second_begin);
  const auto canonical_second_result =
      otgy::core::analyze_slk_dance_move_drills(canonical_second_groups);
  assert_move_counts(analysis_of(canonical_second_result),
                     {10U, 9U, 10U, 11U, 9U, 9U, 6U, 6U, 9U, 9U, 9U});

  const auto compatibility =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_first_groups =
      analyze_group_sequence(compatibility, 0U);
  const auto compatibility_first_result =
      otgy::core::analyze_slk_dance_move_drills(compatibility_first_groups);
  assert_move_counts(analysis_of(compatibility_first_result),
                     {9U, 9U, 9U, 9U, 7U, 7U, 8U, 8U});

  const auto compatibility_second_begin = next_ordinary_begin(
      compatibility, compatibility_first_groups.next_row_index());
  const auto compatibility_second_groups =
      analyze_group_sequence(compatibility, compatibility_second_begin);
  const auto compatibility_second_result =
      otgy::core::analyze_slk_dance_move_drills(compatibility_second_groups);
  assert_move_counts(analysis_of(compatibility_second_result),
                     {7U, 7U, 8U, 8U, 9U, 9U, 9U, 9U, 9U});
}

void test_error_messages() {
  assert(otgy::core::slk_dance_move_drill_error_message(
             SlkDanceMoveDrillError::None)
             .empty());
  for (unsigned int raw = 1U;
       raw <=
       static_cast<unsigned int>(SlkDanceMoveDrillError::AllocationFailure);
       ++raw) {
    assert(!otgy::core::slk_dance_move_drill_error_message(
                static_cast<SlkDanceMoveDrillError>(raw))
                .empty());
  }
  assert(!otgy::core::slk_dance_move_drill_error_message(
              static_cast<SlkDanceMoveDrillError>(255))
              .empty());
}

}  // namespace

int main() {
  test_unsplit_and_split_drills_mapping_and_ownership();
  test_all_unsplit_counts();
  test_two_digit_segment_count();
  test_c_value_count_and_encoding_errors();
  test_move_end_errors();
  test_repository_sequences();
  test_error_messages();
  return 0;
}
