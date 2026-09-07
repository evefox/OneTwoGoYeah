// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_showtime_analyzer.hpp"

#include <new>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] SlkShowtimeFailure make_failure(
    SlkShowtimeError error, const std::vector<SlkParsedRow>& rows,
    std::size_t source_row_index, std::size_t showtime_index,
    std::size_t stage_group_index = 0U, std::size_t show_d_index = 0U,
    SlkDrillGroupError drill_group_error = SlkDrillGroupError::None,
    SlkDanceMoveDrillError dance_move_drill_error =
        SlkDanceMoveDrillError::None) noexcept {
  const auto sheet_row =
      source_row_index < rows.size() ? rows[source_row_index].sheet_row : 0U;
  return {error,
          source_row_index,
          sheet_row,
          showtime_index,
          stage_group_index,
          show_d_index,
          drill_group_error,
          dance_move_drill_error};
}

[[nodiscard]] SlkShowtimeFailure group_failure(
    const std::vector<SlkParsedRow>& rows, const SlkDrillGroupFailure& failure,
    std::size_t showtime_index) noexcept {
  return make_failure(SlkShowtimeError::DrillGroupAnalysisFailed, rows,
                      failure.source_row_index, showtime_index, 0U, 0U,
                      failure.error);
}

[[nodiscard]] SlkShowtimeFailure dance_move_failure(
    const std::vector<SlkParsedRow>& rows, const SlkDrillGroupAnalysis& groups,
    const SlkDanceMoveDrillFailure& failure,
    std::size_t showtime_index) noexcept {
  auto source_row_index = groups.next_row_index();
  if (failure.source_group_index < groups.groups().size() &&
      !groups.groups()[failure.source_group_index].npc.rows.empty()) {
    source_row_index = groups.groups()[failure.source_group_index]
                           .npc.rows.front()
                           .source_row_index;
  }
  return make_failure(SlkShowtimeError::DanceMoveDrillAnalysisFailed, rows,
                      source_row_index, showtime_index,
                      failure.drill_group_index, 0U, SlkDrillGroupError::None,
                      failure.error);
}

}  // namespace

SlkShowtimeAnalysis::SlkShowtimeAnalysis(
    std::vector<SlkShowtime> showtimes,
    std::optional<SlkParsedRow> boundary_row,
    std::optional<std::size_t> boundary_source_row_index) noexcept
    : showtimes_(std::move(showtimes)),
      boundary_row_(boundary_row),
      boundary_source_row_index_(boundary_source_row_index) {}

SlkShowtimeAnalysisResult::SlkShowtimeAnalysisResult(
    SlkShowtimeAnalysis analysis) noexcept
    : storage_(std::move(analysis)) {}

SlkShowtimeAnalysisResult::SlkShowtimeAnalysisResult(
    SlkShowtimeFailure failure) noexcept
    : storage_(failure) {}

bool SlkShowtimeAnalysisResult::has_value() const noexcept {
  return std::holds_alternative<SlkShowtimeAnalysis>(storage_);
}

const SlkShowtimeAnalysis* SlkShowtimeAnalysisResult::analysis()
    const noexcept {
  return std::get_if<SlkShowtimeAnalysis>(&storage_);
}

const SlkShowtimeFailure* SlkShowtimeAnalysisResult::failure() const noexcept {
  return std::get_if<SlkShowtimeFailure>(&storage_);
}

SlkShowtimeAnalysis SlkShowtimeAnalysisResult::take_analysis() && {
  return std::move(std::get<SlkShowtimeAnalysis>(storage_));
}

SlkShowtimeAnalysisResult analyze_slk_showtimes(
    const SlkParsedRecords& records) noexcept {
  const auto& rows = records.rows();
  std::size_t cursor = 0U;
  try {
    std::vector<SlkShowtime> showtimes;
    while (cursor < rows.size()) {
      auto group_result = analyze_slk_drill_groups(records, cursor);
      if (!group_result) {
        return SlkShowtimeAnalysisResult{
            group_failure(rows, *group_result.failure(), showtimes.size())};
      }
      auto groups = std::move(group_result).take_analysis();

      if (groups.end() == SlkOrdinarySequenceEnd::FileBoundary) {
        if (!groups.groups().empty()) {
          return SlkShowtimeAnalysisResult{
              make_failure(SlkShowtimeError::MissingShowtime, rows,
                           groups.next_row_index(), showtimes.size())};
        }
        const auto boundary_index = groups.next_row_index();
        if (showtimes.empty()) {
          return SlkShowtimeAnalysisResult{make_failure(
              SlkShowtimeError::EmptyChart, rows, boundary_index, 0U)};
        }
        if (boundary_index < rows.size() &&
            (rows[boundary_index].type != SlkRowType::R ||
             rows[boundary_index].value != 0U ||
             boundary_index + 1U != rows.size())) {
          return SlkShowtimeAnalysisResult{
              make_failure(SlkShowtimeError::InvalidFileBoundary, rows,
                           boundary_index, showtimes.size())};
        }
        const std::optional<SlkParsedRow> boundary =
            boundary_index < rows.size()
                ? std::optional<SlkParsedRow>{rows[boundary_index]}
                : std::nullopt;
        const std::optional<std::size_t> boundary_source_row_index =
            boundary_index < rows.size()
                ? std::optional<std::size_t>{boundary_index}
                : std::nullopt;
        return SlkShowtimeAnalysisResult{SlkShowtimeAnalysis{
            std::move(showtimes), boundary, boundary_source_row_index}};
      }

      if (groups.groups().empty()) {
        return SlkShowtimeAnalysisResult{
            make_failure(SlkShowtimeError::EmptyOrdinaryStage, rows,
                         groups.next_row_index(), showtimes.size())};
      }
      auto dance_result = analyze_slk_dance_move_drills(groups);
      if (!dance_result) {
        return SlkShowtimeAnalysisResult{dance_move_failure(
            rows, groups, *dance_result.failure(), showtimes.size())};
      }
      auto stage = std::move(dance_result).take_analysis();

      const auto c_index = groups.next_row_index();
      if (c_index >= rows.size() || rows[c_index].type != SlkRowType::C ||
          rows[c_index].value != 0U) {
        return SlkShowtimeAnalysisResult{
            make_failure(SlkShowtimeError::MissingShowtime, rows, c_index,
                         showtimes.size())};
      }

      SlkShowtime showtime{{rows[c_index].sheet_row},
                           rows[c_index],
                           c_index,
                           std::move(stage),
                           {}};
      auto d_index = c_index + 1U;
      const auto& drills = showtime.stage.drills();
      std::size_t d_count = 0U;
      while (d_index < rows.size() && rows[d_index].type == SlkRowType::D) {
        if (d_count >= drills.size()) {
          return SlkShowtimeAnalysisResult{
              make_failure(SlkShowtimeError::ShowtimeDCountMismatch, rows,
                           d_index, showtimes.size(), 0U, d_count)};
        }
        if (rows[d_index].value != drills[d_count].move_n_count) {
          return SlkShowtimeAnalysisResult{
              make_failure(SlkShowtimeError::ShowtimeDValueMismatch, rows,
                           d_index, showtimes.size(), 0U, d_count)};
        }
        showtime.show_d_rows.push_back({{rows[d_index].sheet_row},
                                        rows[d_index],
                                        d_index,
                                        d_count,
                                        drills[d_count].id,
                                        rows[d_index].value});
        ++d_index;
        ++d_count;
      }
      if (d_count == 0U) {
        return SlkShowtimeAnalysisResult{
            make_failure(SlkShowtimeError::ShowtimeDCountMismatch, rows,
                         c_index + 1U, showtimes.size(), 0U, 0U)};
      }

      showtimes.push_back(std::move(showtime));
      cursor = d_index;
      if (cursor >= rows.size()) {
        return SlkShowtimeAnalysisResult{SlkShowtimeAnalysis{
            std::move(showtimes), std::nullopt, std::nullopt}};
      }
    }
  } catch (const std::bad_alloc&) {
    return SlkShowtimeAnalysisResult{
        make_failure(SlkShowtimeError::AllocationFailure, rows, cursor, 0U)};
  }

  return SlkShowtimeAnalysisResult{
      make_failure(SlkShowtimeError::MissingFileBoundary, rows, cursor, 0U)};
}

}  // namespace otgy::core
