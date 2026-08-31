// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_drill_group_analyzer.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

struct BlockSpan final {
  std::size_t begin;
  std::size_t end;
};

struct BlockParseResult final {
  bool ok;
  BlockSpan span;
  SlkDrillGroupFailure failure;
};

[[nodiscard]] SlkDrillGroupFailure make_failure(
    SlkDrillGroupError error, const std::vector<SlkParsedRow>& rows,
    std::size_t source_row_index, SlkDrillBlockRole role,
    std::size_t block_row_index = 0U) noexcept {
  const auto sheet_row =
      source_row_index < rows.size() ? rows[source_row_index].sheet_row : 0U;
  return {error, source_row_index, sheet_row, block_row_index, role};
}

[[nodiscard]] bool is_sequence_end(const SlkParsedRow& row) noexcept {
  return row.type == SlkRowType::R ||
         (row.type == SlkRowType::C && row.value == 0U);
}

[[nodiscard]] BlockParseResult parse_block(
    const std::vector<SlkParsedRow>& rows, std::size_t begin,
    SlkDrillBlockRole role) noexcept {
  if (begin >= rows.size()) {
    return {false,
            {begin, begin},
            make_failure(SlkDrillGroupError::MissingSequenceEnd, rows, begin,
                         role)};
  }
  if (rows[begin].type != SlkRowType::C || rows[begin].value == 0U) {
    return {false,
            {begin, begin},
            make_failure(SlkDrillGroupError::ExpectedBlockStart, rows, begin,
                         role)};
  }

  auto cursor = begin + 1U;
  const auto first_n = cursor;
  while (cursor < rows.size() && rows[cursor].type == SlkRowType::N) {
    ++cursor;
  }
  if (cursor == first_n) {
    return {
        false,
        {begin, cursor},
        make_failure(SlkDrillGroupError::MissingNRows, rows, cursor, role, 1U)};
  }
  if (cursor < rows.size() && rows[cursor].type == SlkRowType::S) {
    ++cursor;
  }
  return {true,
          {begin, cursor},
          make_failure(SlkDrillGroupError::None, rows, begin, role)};
}

[[nodiscard]] SlkDrillGroupFailure compare_blocks(
    const std::vector<SlkParsedRow>& rows, BlockSpan npc,
    BlockSpan player) noexcept {
  const auto npc_size = npc.end - npc.begin;
  const auto player_size = player.end - player.begin;
  if (npc_size != player_size) {
    const auto mismatch_index = std::min(npc_size, player_size);
    const auto source_index = mismatch_index < player_size
                                  ? player.begin + mismatch_index
                                  : player.begin;
    return make_failure(SlkDrillGroupError::BlockLengthMismatch, rows,
                        source_index, SlkDrillBlockRole::Player,
                        mismatch_index);
  }

  const auto npc_content_start = rows[npc.begin + 1U].position.total_sixteenth;
  const auto player_content_start =
      rows[player.begin + 1U].position.total_sixteenth;
  for (std::size_t index = 0U; index < npc_size; ++index) {
    const auto& npc_row = rows[npc.begin + index];
    const auto& player_row = rows[player.begin + index];
    const auto player_source_index = player.begin + index;
    if (npc_row.type != player_row.type) {
      return make_failure(SlkDrillGroupError::BlockTypeMismatch, rows,
                          player_source_index, SlkDrillBlockRole::Player,
                          index);
    }
    if (npc_row.value != player_row.value) {
      return make_failure(SlkDrillGroupError::BlockValueMismatch, rows,
                          player_source_index, SlkDrillBlockRole::Player,
                          index);
    }
    if (index > 0U &&
        (npc_row.position.total_sixteenth < npc_content_start ||
         player_row.position.total_sixteenth < player_content_start ||
         npc_row.position.total_sixteenth - npc_content_start !=
             player_row.position.total_sixteenth - player_content_start)) {
      return make_failure(SlkDrillGroupError::BlockRelativePositionMismatch,
                          rows, player_source_index, SlkDrillBlockRole::Player,
                          index);
    }
  }
  return make_failure(SlkDrillGroupError::None, rows, player.begin,
                      SlkDrillBlockRole::Player);
}

[[nodiscard]] SlkDrillBlock make_block(const std::vector<SlkParsedRow>& rows,
                                       BlockSpan span, SlkDrillGroupId group_id,
                                       SlkDrillBlockRole role) {
  SlkDrillBlock block{role, {}};
  block.rows.reserve(span.end - span.begin);
  for (auto source_index = span.begin; source_index < span.end;
       ++source_index) {
    block.rows.push_back({rows[source_index], source_index,
                          source_index - span.begin, group_id, role});
  }
  return block;
}

}  // namespace

SlkDrillGroupAnalysis::SlkDrillGroupAnalysis(
    std::vector<SlkDrillGroup> groups, std::size_t begin_row_index,
    std::size_t next_row_index, SlkOrdinarySequenceEnd end) noexcept
    : groups_(std::move(groups)),
      begin_row_index_(begin_row_index),
      next_row_index_(next_row_index),
      end_(end) {}

SlkDrillGroupAnalysisResult::SlkDrillGroupAnalysisResult(
    SlkDrillGroupAnalysis analysis) noexcept
    : storage_(std::move(analysis)) {}

SlkDrillGroupAnalysisResult::SlkDrillGroupAnalysisResult(
    SlkDrillGroupFailure failure) noexcept
    : storage_(failure) {}

bool SlkDrillGroupAnalysisResult::has_value() const noexcept {
  return std::holds_alternative<SlkDrillGroupAnalysis>(storage_);
}

const SlkDrillGroupAnalysis* SlkDrillGroupAnalysisResult::analysis()
    const noexcept {
  return std::get_if<SlkDrillGroupAnalysis>(&storage_);
}

const SlkDrillGroupFailure* SlkDrillGroupAnalysisResult::failure()
    const noexcept {
  return std::get_if<SlkDrillGroupFailure>(&storage_);
}

SlkDrillGroupAnalysis SlkDrillGroupAnalysisResult::take_analysis() && {
  return std::move(std::get<SlkDrillGroupAnalysis>(storage_));
}

SlkDrillGroupAnalysisResult analyze_slk_drill_groups(
    const SlkParsedRecords& records, std::size_t begin_row_index) noexcept {
  const auto& rows = records.rows();
  if (begin_row_index >= rows.size()) {
    return SlkDrillGroupAnalysisResult{
        make_failure(SlkDrillGroupError::InvalidBeginRow, rows, begin_row_index,
                     SlkDrillBlockRole::Npc)};
  }

  auto cursor = begin_row_index;
  try {
    std::vector<SlkDrillGroup> groups;
    while (cursor < rows.size()) {
      if (is_sequence_end(rows[cursor])) {
        const auto end = rows[cursor].type == SlkRowType::R
                             ? SlkOrdinarySequenceEnd::FileBoundary
                             : SlkOrdinarySequenceEnd::Showtime;
        return SlkDrillGroupAnalysisResult{SlkDrillGroupAnalysis{
            std::move(groups), begin_row_index, cursor, end}};
      }

      const auto npc = parse_block(rows, cursor, SlkDrillBlockRole::Npc);
      if (!npc.ok) {
        return SlkDrillGroupAnalysisResult{npc.failure};
      }
      cursor = npc.span.end;
      if (cursor >= rows.size() || is_sequence_end(rows[cursor])) {
        return SlkDrillGroupAnalysisResult{
            make_failure(SlkDrillGroupError::MissingPlayerBlock, rows, cursor,
                         SlkDrillBlockRole::Player)};
      }

      const auto player = parse_block(rows, cursor, SlkDrillBlockRole::Player);
      if (!player.ok) {
        return SlkDrillGroupAnalysisResult{player.failure};
      }
      const auto mismatch = compare_blocks(rows, npc.span, player.span);
      if (mismatch.error != SlkDrillGroupError::None) {
        return SlkDrillGroupAnalysisResult{mismatch};
      }

      const SlkDrillGroupId id{rows[npc.span.begin].sheet_row};
      groups.push_back(
          {id, make_block(rows, npc.span, id, SlkDrillBlockRole::Npc),
           make_block(rows, player.span, id, SlkDrillBlockRole::Player)});
      cursor = player.span.end;
    }
  } catch (const std::bad_alloc&) {
    return SlkDrillGroupAnalysisResult{
        make_failure(SlkDrillGroupError::AllocationFailure, rows, cursor,
                     SlkDrillBlockRole::Npc)};
  }

  return SlkDrillGroupAnalysisResult{
      make_failure(SlkDrillGroupError::MissingSequenceEnd, rows, cursor,
                   SlkDrillBlockRole::Npc)};
}

}  // namespace otgy::core
