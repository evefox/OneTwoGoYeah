// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_dance_move_drill_analyzer.hpp"

#include <new>
#include <utility>

namespace otgy::core {
namespace {

struct DecodedCValue final {
  bool valid;
  bool split;
  std::uint64_t segment_n_count;
  std::uint64_t move_n_count;
};

[[nodiscard]] DecodedCValue decode_c_value(std::uint64_t c_value) noexcept {
  if (c_value >= 1U && c_value <= 11U) {
    return {true, false, c_value, c_value};
  }
  const auto segment_n_count = c_value / 100U;
  const auto move_n_count = c_value % 100U;
  if (segment_n_count == 0U || move_n_count < 1U || move_n_count > 11U) {
    return {false, false, 0U, 0U};
  }
  return {true, true, segment_n_count, move_n_count};
}

[[nodiscard]] bool valid_group(const SlkDrillGroup& group) noexcept {
  return !group.npc.rows.empty() && !group.player.rows.empty() &&
         group.npc.rows.front().row.type == SlkRowType::C &&
         group.player.rows.front().row.type == SlkRowType::C &&
         group.npc.rows.front().row.value ==
             group.player.rows.front().row.value;
}

[[nodiscard]] std::uint64_t c_value_of(const SlkDrillGroup& group) noexcept {
  return valid_group(group) ? group.npc.rows.front().row.value : 0U;
}

[[nodiscard]] bool has_end_row(const SlkDrillBlock& block) noexcept {
  return !block.rows.empty() && block.rows.back().row.type == SlkRowType::S;
}

[[nodiscard]] std::uint64_t actual_n_count(
    const SlkDrillBlock& block) noexcept {
  if (block.rows.empty()) {
    return 0U;
  }
  const auto non_c_rows = block.rows.size() - 1U;
  const auto count = non_c_rows - (has_end_row(block) ? 1U : 0U);
  return static_cast<std::uint64_t>(count);
}

[[nodiscard]] SlkDanceMoveDrillFailure make_failure(
    SlkDanceMoveDrillError error, const std::vector<SlkDrillGroup>& groups,
    std::size_t source_group_index,
    std::size_t drill_group_index = 0U) noexcept {
  if (source_group_index >= groups.size()) {
    return {error, source_group_index, drill_group_index, {0U}, 0U, 0U};
  }
  const auto& group = groups[source_group_index];
  const auto sheet_row =
      group.npc.rows.empty() ? 0U : group.npc.rows.front().row.sheet_row;
  return {error,    source_group_index, drill_group_index,
          group.id, sheet_row,          c_value_of(group)};
}

void append_move_rows(SlkDanceMove& move, const SlkDrillBlock& block,
                      std::size_t source_group_index,
                      SlkDanceMoveDrillId drill_id) {
  for (std::size_t index = 1U; index < block.rows.size(); ++index) {
    move.rows.push_back({block.rows[index], source_group_index,
                         move.rows.size(), drill_id, move.role});
  }
}

}  // namespace

SlkDanceMoveDrillAnalysis::SlkDanceMoveDrillAnalysis(
    std::vector<SlkDanceMoveDrill> drills, std::size_t begin_row_index,
    std::size_t next_row_index, SlkOrdinarySequenceEnd end) noexcept
    : drills_(std::move(drills)),
      begin_row_index_(begin_row_index),
      next_row_index_(next_row_index),
      end_(end) {}

SlkDanceMoveDrillAnalysisResult::SlkDanceMoveDrillAnalysisResult(
    SlkDanceMoveDrillAnalysis analysis) noexcept
    : storage_(std::move(analysis)) {}

SlkDanceMoveDrillAnalysisResult::SlkDanceMoveDrillAnalysisResult(
    SlkDanceMoveDrillFailure failure) noexcept
    : storage_(failure) {}

bool SlkDanceMoveDrillAnalysisResult::has_value() const noexcept {
  return std::holds_alternative<SlkDanceMoveDrillAnalysis>(storage_);
}

const SlkDanceMoveDrillAnalysis* SlkDanceMoveDrillAnalysisResult::analysis()
    const noexcept {
  return std::get_if<SlkDanceMoveDrillAnalysis>(&storage_);
}

const SlkDanceMoveDrillFailure* SlkDanceMoveDrillAnalysisResult::failure()
    const noexcept {
  return std::get_if<SlkDanceMoveDrillFailure>(&storage_);
}

SlkDanceMoveDrillAnalysis SlkDanceMoveDrillAnalysisResult::take_analysis() && {
  return std::move(std::get<SlkDanceMoveDrillAnalysis>(storage_));
}

SlkDanceMoveDrillAnalysisResult analyze_slk_dance_move_drills(
    const SlkDrillGroupAnalysis& group_analysis) noexcept {
  const auto& groups = group_analysis.groups();
  if (groups.empty()) {
    return SlkDanceMoveDrillAnalysisResult{
        make_failure(SlkDanceMoveDrillError::EmptySequence, groups, 0U)};
  }

  std::size_t cursor = 0U;
  try {
    std::vector<SlkDanceMoveDrill> drills;
    while (cursor < groups.size()) {
      const auto drill_begin = cursor;
      if (!valid_group(groups[cursor])) {
        return SlkDanceMoveDrillAnalysisResult{make_failure(
            SlkDanceMoveDrillError::InvalidDrillGroup, groups, cursor)};
      }
      const auto first_c_value = c_value_of(groups[cursor]);
      const auto first_decoded = decode_c_value(first_c_value);
      if (!first_decoded.valid) {
        return SlkDanceMoveDrillAnalysisResult{make_failure(
            SlkDanceMoveDrillError::InvalidCValue, groups, cursor)};
      }

      const SlkDanceMoveDrillId drill_id{groups[cursor].id.value};
      SlkDanceMoveDrill drill{
          drill_id,
          first_decoded.move_n_count,
          {},
          {SlkDrillBlockRole::Npc, {}},
          {SlkDrillBlockRole::Player, {}},
      };
      std::uint64_t total_n_count = 0U;

      while (cursor < groups.size()) {
        const auto drill_group_index = cursor - drill_begin;
        const auto& group = groups[cursor];
        if (!valid_group(group)) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::InvalidDrillGroup, groups,
                           cursor, drill_group_index)};
        }
        const auto c_value = c_value_of(group);
        const auto decoded = decode_c_value(c_value);
        if (!decoded.valid) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::InvalidCValue, groups,
                           cursor, drill_group_index)};
        }
        if (decoded.split != first_decoded.split) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::InconsistentEncoding, groups,
                           cursor, drill_group_index)};
        }
        if (decoded.move_n_count != first_decoded.move_n_count) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::InconsistentMoveNCount,
                           groups, cursor, drill_group_index)};
        }
        if (actual_n_count(group.npc) != decoded.segment_n_count ||
            actual_n_count(group.player) != decoded.segment_n_count) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::SegmentNCountMismatch,
                           groups, cursor, drill_group_index)};
        }
        if (decoded.segment_n_count >
            first_decoded.move_n_count - total_n_count) {
          return SlkDanceMoveDrillAnalysisResult{make_failure(
              SlkDanceMoveDrillError::SegmentTotalExceedsMoveNCount, groups,
              cursor, drill_group_index)};
        }

        total_n_count += decoded.segment_n_count;
        drill.groups.push_back({group.id, cursor, drill_group_index, c_value,
                                decoded.segment_n_count});
        append_move_rows(drill.npc, group.npc, cursor, drill_id);
        append_move_rows(drill.player, group.player, cursor, drill_id);

        const auto has_end =
            has_end_row(group.npc) && has_end_row(group.player);
        if (total_n_count < first_decoded.move_n_count) {
          if (has_end) {
            return SlkDanceMoveDrillAnalysisResult{
                make_failure(SlkDanceMoveDrillError::PrematureMoveEnd, groups,
                             cursor, drill_group_index)};
          }
          ++cursor;
          if (cursor == groups.size()) {
            return SlkDanceMoveDrillAnalysisResult{
                make_failure(SlkDanceMoveDrillError::IncompleteMoveNCount,
                             groups, cursor, drill_group_index + 1U)};
          }
          continue;
        }

        if (!has_end) {
          return SlkDanceMoveDrillAnalysisResult{
              make_failure(SlkDanceMoveDrillError::MissingMoveEnd, groups,
                           cursor, drill_group_index)};
        }
        if (first_decoded.split && drill.groups.size() < 2U) {
          return SlkDanceMoveDrillAnalysisResult{make_failure(
              SlkDanceMoveDrillError::SplitDrillRequiresMultipleGroups, groups,
              cursor, drill_group_index)};
        }

        drills.push_back(std::move(drill));
        ++cursor;
        break;
      }
    }

    return SlkDanceMoveDrillAnalysisResult{SlkDanceMoveDrillAnalysis{
        std::move(drills), group_analysis.begin_row_index(),
        group_analysis.next_row_index(), group_analysis.end()}};
  } catch (const std::bad_alloc&) {
    return SlkDanceMoveDrillAnalysisResult{make_failure(
        SlkDanceMoveDrillError::AllocationFailure, groups, cursor)};
  }
}

}  // namespace otgy::core
