// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_showtime_analyzer.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::SlkDanceMoveDrillError;
using otgy::core::SlkDrillGroupError;
using otgy::core::SlkParsedRecords;
using otgy::core::SlkParsedRow;
using otgy::core::SlkRowType;
using otgy::core::SlkShowtimeAnalysis;
using otgy::core::SlkShowtimeAnalysisResult;
using otgy::core::SlkShowtimeError;

[[nodiscard]] SlkParsedRow row(std::uint64_t sheet_row,
                               std::uint64_t total_sixteenth, SlkRowType type,
                               std::uint64_t value) {
  const auto position = otgy::core::decompose_slk_position(total_sixteenth);
  assert(position);
  return {sheet_row, *position.position(), type, value};
}

void append_group(std::vector<SlkParsedRow>& rows, std::uint64_t& sheet_row,
                  std::uint64_t& total_sixteenth, std::uint64_t c_value,
                  std::uint64_t n_count, bool has_end) {
  const auto append_block = [&](std::uint64_t block_start) {
    rows.push_back(row(sheet_row++, block_start, SlkRowType::C, c_value));
    for (std::uint64_t index = 0U; index < n_count; ++index) {
      rows.push_back(
          row(sheet_row++, block_start + index + 1U, SlkRowType::N, 0U));
    }
    if (has_end) {
      rows.push_back(
          row(sheet_row++, block_start + n_count + 1U, SlkRowType::S, 0U));
    }
  };

  append_block(total_sixteenth);
  const auto block_span = n_count + (has_end ? 2U : 1U);
  const auto player_start = total_sixteenth + block_span + 2U;
  append_block(player_start);
  total_sixteenth = player_start + block_span + 2U;
}

[[nodiscard]] std::vector<SlkParsedRow> valid_rows() {
  std::vector<SlkParsedRow> rows;
  std::uint64_t sheet_row = 3U;
  std::uint64_t position = 0U;
  append_group(rows, sheet_row, position, 1U, 1U, true);
  append_group(rows, sheet_row, position, 2U, 2U, true);
  rows.push_back(row(sheet_row++, position, SlkRowType::C, 0U));
  position += 2U;
  rows.push_back(row(sheet_row++, position, SlkRowType::D, 1U));
  position += 16U;
  rows.push_back(row(sheet_row++, position, SlkRowType::D, 2U));
  position += 16U;
  append_group(rows, sheet_row, position, 7U, 7U, true);
  rows.push_back(row(sheet_row++, position, SlkRowType::C, 0U));
  position += 2U;
  rows.push_back(row(sheet_row++, position, SlkRowType::D, 7U));
  position += 32U;
  rows.push_back(row(sheet_row, position, SlkRowType::R, 0U));
  return rows;
}

[[nodiscard]] SlkParsedRecords records(std::vector<SlkParsedRow> rows) {
  return SlkParsedRecords{std::move(rows)};
}

[[nodiscard]] const SlkShowtimeAnalysis& analysis_of(
    const SlkShowtimeAnalysisResult& result) {
  assert(result);
  assert(result.analysis() != nullptr);
  assert(result.failure() == nullptr);
  return *result.analysis();
}

otgy::core::SlkShowtimeFailure failure_of(const SlkParsedRecords& input,
                                          SlkShowtimeError expected) {
  const auto result = otgy::core::analyze_slk_showtimes(input);
  assert(!result);
  assert(result.analysis() == nullptr);
  assert(result.failure() != nullptr);
  assert(result.failure()->error == expected);
  return *result.failure();
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

[[nodiscard]] std::size_t find_row(const std::vector<SlkParsedRow>& rows,
                                   SlkRowType type, std::size_t ordinal) {
  std::size_t seen = 0U;
  for (std::size_t index = 0U; index < rows.size(); ++index) {
    if (rows[index].type == type) {
      if (seen == ordinal) {
        return index;
      }
      ++seen;
    }
  }
  assert(false);
  return 0U;
}

void test_full_association_mapping_and_ownership() {
  auto result = otgy::core::analyze_slk_showtimes(records(valid_rows()));
  auto analysis = std::move(result).take_analysis();
  assert(analysis.showtimes().size() == 2U);

  const auto& first = analysis.showtimes()[0];
  assert(first.id.value == first.c_row.sheet_row);
  assert(first.c_row.type == SlkRowType::C);
  assert(first.c_row.value == 0U);
  assert(first.stage.drills().size() == 2U);
  assert(first.show_d_rows.size() == 2U);
  assert(first.show_d_rows[0].show_d_index == 0U);
  assert(first.show_d_rows[0].show_n_count == 1U);
  assert(first.show_d_rows[1].show_n_count == 2U);
  assert(first.show_d_rows[0].dance_move_drill_id.value ==
         first.stage.drills()[0].id.value);
  assert(first.show_d_rows[1].dance_move_drill_id.value ==
         first.stage.drills()[1].id.value);
  for (const auto& show_d : first.show_d_rows) {
    assert(show_d.id.value == show_d.row.sheet_row);
    assert(show_d.row.type == SlkRowType::D);
  }

  const auto& second = analysis.showtimes()[1];
  assert(second.stage.drills().size() == 1U);
  assert(second.show_d_rows.size() == 1U);
  assert(second.show_d_rows[0].show_n_count == 7U);
  assert(analysis.boundary_source_row_index().value() + 1U ==
         valid_rows().size());
  assert(analysis.boundary_row()->type == SlkRowType::R);
  assert(analysis.boundary_row()->value == 0U);

  const auto copy = analysis;
  const auto moved = std::move(analysis);
  assert(copy.showtimes().size() == 2U);
  assert(moved.showtimes().size() == 2U);
  assert(copy.showtimes()[0].show_d_rows.data() !=
         moved.showtimes()[0].show_d_rows.data());
}

void test_repository_files() {
  const auto canonical =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto canonical_result = otgy::core::analyze_slk_showtimes(canonical);
  const auto& canonical_analysis = analysis_of(canonical_result);
  assert(canonical_analysis.showtimes().size() == 2U);
  assert(canonical_analysis.showtimes()[0].id.value == 155U);
  assert(canonical_analysis.showtimes()[0].show_d_rows.size() == 6U);
  assert(canonical_analysis.showtimes()[1].id.value == 418U);
  assert(canonical_analysis.showtimes()[1].show_d_rows.size() == 11U);
  assert(canonical_analysis.boundary_row()->sheet_row == 430U);
  assert(canonical_analysis.boundary_source_row_index().value() == 427U);

  const auto compatibility =
      parse_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_result =
      otgy::core::analyze_slk_showtimes(compatibility);
  const auto& compatibility_analysis = analysis_of(compatibility_result);
  assert(compatibility_analysis.showtimes().size() == 2U);
  assert(compatibility_analysis.showtimes()[0].id.value == 183U);
  assert(compatibility_analysis.showtimes()[0].show_d_rows.size() == 8U);
  assert(compatibility_analysis.showtimes()[1].id.value == 396U);
  assert(compatibility_analysis.showtimes()[1].show_d_rows.size() == 9U);
  assert(compatibility_analysis.boundary_row()->sheet_row == 406U);
  assert(compatibility_analysis.boundary_source_row_index().value() == 403U);
}

void test_stage_and_nested_analysis_errors() {
  const auto empty_chart = records({row(3U, 0U, SlkRowType::R, 0U)});
  const auto& empty_failure =
      failure_of(empty_chart, SlkShowtimeError::EmptyChart);
  assert(empty_failure.source_row_index == 0U);

  const auto empty_stage = records({
      row(3U, 0U, SlkRowType::C, 0U),
      row(4U, 1U, SlkRowType::R, 0U),
  });
  failure_of(empty_stage, SlkShowtimeError::EmptyOrdinaryStage);

  std::vector<SlkParsedRow> trailing_stage;
  std::uint64_t sheet = 3U;
  std::uint64_t position = 0U;
  append_group(trailing_stage, sheet, position, 1U, 1U, true);
  const auto trailing_boundary_sheet = sheet;
  trailing_stage.push_back(row(sheet, position, SlkRowType::R, 0U));
  const auto trailing_result =
      analyze_slk_showtimes(records(std::move(trailing_stage)));
  assert(trailing_result);
  assert(trailing_result.analysis()->showtimes().empty());
  assert(trailing_result.analysis()->trailing_drills().size() == 1U);
  assert(trailing_result.analysis()->trailing_drills().front().move_n_count ==
         1U);
  assert(trailing_result.analysis()->boundary_row()->sheet_row ==
         trailing_boundary_sheet);

  std::vector<SlkParsedRow> trailing_without_boundary;
  sheet = 3U;
  position = 0U;
  append_group(trailing_without_boundary, sheet, position, 1U, 1U, true);
  const auto no_boundary_result =
      analyze_slk_showtimes(records(std::move(trailing_without_boundary)));
  assert(no_boundary_result);
  assert(!no_boundary_result.analysis()->boundary_row().has_value());
  assert(no_boundary_result.analysis()->trailing_drills().size() == 1U);

  auto mixed_rows = valid_rows();
  const auto mixed_boundary = mixed_rows.back();
  mixed_rows.pop_back();
  sheet = mixed_boundary.sheet_row;
  position = mixed_boundary.position.total_sixteenth;
  append_group(mixed_rows, sheet, position, 1U, 1U, true);
  mixed_rows.push_back(row(sheet, position, SlkRowType::R, 0U));
  const auto mixed_result =
      analyze_slk_showtimes(records(std::move(mixed_rows)));
  assert(mixed_result);
  assert(mixed_result.analysis()->showtimes().size() == 2U);
  assert(mixed_result.analysis()->trailing_drills().size() == 1U);

  std::vector<SlkParsedRow> invalid_dance;
  sheet = 3U;
  position = 0U;
  append_group(invalid_dance, sheet, position, 12U, 1U, true);
  invalid_dance.push_back(row(sheet++, position++, SlkRowType::C, 0U));
  invalid_dance.push_back(row(sheet++, position++, SlkRowType::D, 12U));
  invalid_dance.push_back(row(sheet, position, SlkRowType::R, 0U));
  const auto& dance_failure =
      failure_of(records(std::move(invalid_dance)),
                 SlkShowtimeError::DanceMoveDrillAnalysisFailed);
  assert(dance_failure.dance_move_drill_error ==
         SlkDanceMoveDrillError::InvalidCValue);
  assert(dance_failure.stage_group_index == 0U);

  std::vector<SlkParsedRow> invalid_group;
  sheet = 3U;
  position = 0U;
  invalid_group.push_back(row(sheet++, position++, SlkRowType::C, 1U));
  invalid_group.push_back(row(sheet++, position++, SlkRowType::N, 0U));
  invalid_group.push_back(row(sheet++, position++, SlkRowType::S, 0U));
  invalid_group.push_back(row(sheet++, position++, SlkRowType::C, 0U));
  invalid_group.push_back(row(sheet, position, SlkRowType::R, 0U));
  const auto& group_failure =
      failure_of(records(std::move(invalid_group)),
                 SlkShowtimeError::DrillGroupAnalysisFailed);
  assert(group_failure.drill_group_error ==
         SlkDrillGroupError::MissingPlayerBlock);
}

void test_showtime_count_value_and_boundary_errors() {
  auto fewer = valid_rows();
  const auto second_d = find_row(fewer, SlkRowType::D, 1U);
  fewer.erase(fewer.begin() + static_cast<std::ptrdiff_t>(second_d));
  const auto fewer_result = analyze_slk_showtimes(records(std::move(fewer)));
  assert(fewer_result);
  assert(fewer_result.analysis()->showtimes()[0].stage.drills().size() ==
         2U);
  assert(fewer_result.analysis()->showtimes()[0].show_d_rows.size() == 1U);
  assert(fewer_result.analysis()->showtimes()[0].show_d_rows[0]
             .dance_move_drill_id.value ==
         fewer_result.analysis()->showtimes()[0].stage.drills()[0].id.value);

  auto none = valid_rows();
  const auto second_d_none = find_row(none, SlkRowType::D, 1U);
  const auto first_d_none = find_row(none, SlkRowType::D, 0U);
  none.erase(none.begin() + static_cast<std::ptrdiff_t>(second_d_none));
  none.erase(none.begin() + static_cast<std::ptrdiff_t>(first_d_none));
  const auto& none_failure = failure_of(
      records(std::move(none)), SlkShowtimeError::ShowtimeDCountMismatch);
  assert(none_failure.showtime_index == 0U);
  assert(none_failure.show_d_index == 0U);

  auto more = valid_rows();
  const auto second_d_more = find_row(more, SlkRowType::D, 1U);
  const auto extra = row(9'999U, 99U, SlkRowType::D, 2U);
  more.insert(more.begin() + static_cast<std::ptrdiff_t>(second_d_more + 1U),
              extra);
  const auto& more_failure = failure_of(
      records(std::move(more)), SlkShowtimeError::ShowtimeDCountMismatch);
  assert(more_failure.show_d_index == 2U);

  auto wrong_value = valid_rows();
  const auto first_d = find_row(wrong_value, SlkRowType::D, 0U);
  wrong_value[first_d].value = 2U;
  const auto& value_failure =
      failure_of(records(std::move(wrong_value)),
                 SlkShowtimeError::ShowtimeDValueMismatch);
  assert(value_failure.source_row_index == first_d);
  assert(value_failure.show_d_index == 0U);

  auto missing_boundary = valid_rows();
  missing_boundary.pop_back();
  const auto without_boundary = analyze_slk_showtimes(
      records(std::move(missing_boundary)));
  assert(without_boundary);
  assert(!without_boundary.analysis()->boundary_row().has_value());
  assert(!without_boundary.analysis()->boundary_source_row_index().has_value());

  auto invalid_boundary = valid_rows();
  invalid_boundary.push_back(row(9'999U, 9'999U, SlkRowType::N, 0U));
  failure_of(records(std::move(invalid_boundary)),
             SlkShowtimeError::InvalidFileBoundary);

  failure_of(records({}), SlkShowtimeError::MissingFileBoundary);
}

void test_error_messages() {
  assert(
      otgy::core::slk_showtime_error_message(SlkShowtimeError::None).empty());
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(SlkShowtimeError::AllocationFailure);
       ++raw) {
    assert(!otgy::core::slk_showtime_error_message(
                static_cast<SlkShowtimeError>(raw))
                .empty());
  }
  assert(!otgy::core::slk_showtime_error_message(
              static_cast<SlkShowtimeError>(255))
              .empty());
}

}  // namespace

int main() {
  test_full_association_mapping_and_ownership();
  test_repository_files();
  test_stage_and_nested_analysis_errors();
  test_showtime_count_value_and_boundary_errors();
  test_error_messages();
  return 0;
}
