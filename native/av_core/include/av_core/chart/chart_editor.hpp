// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"
#include "av_core/chart/chart_projection.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace otgy::core {

enum class ChartEditCode : std::uint8_t {
  Success = 0,
  NoChange = 1,
  PositionOccupied = 2,
  CannotAppendAfterEnd = 3,
  CannotPlace = 4,
  NoNpcSource = 5,
  GenerationRangeOccupied = 6,
  InvalidShowtimePosition = 7,
  NoRemainingDanceMoves = 8,
  ShowtimeRangeOccupied = 9,
  InvalidDraft = 10,
  RangeOverflow = 11,
  IdExhausted = 12,
  AllocationFailure = 13,
  NothingToUndo = 14,
  NothingToRedo = 15,
};

[[nodiscard]] constexpr std::string_view chart_edit_code_message(
    ChartEditCode code) noexcept {
  switch (code) {
    case ChartEditCode::Success:
      return {};
    case ChartEditCode::NoChange:
      return "chart edit made no change";
    case ChartEditCode::PositionOccupied:
      return "chart row is already occupied";
    case ChartEditCode::CannotAppendAfterEnd:
      return "cannot append after a move end marker";
    case ChartEditCode::CannotPlace:
      return "marker cannot be placed in this context";
    case ChartEditCode::NoNpcSource:
      return "no NPC demonstration is available to copy";
    case ChartEditCode::GenerationRangeOccupied:
      return "generated marker range is occupied";
    case ChartEditCode::InvalidShowtimePosition:
      return "Showtime move position is invalid";
    case ChartEditCode::NoRemainingDanceMoves:
      return "no remaining dance moves are available";
    case ChartEditCode::ShowtimeRangeOccupied:
      return "Showtime display range is occupied";
    case ChartEditCode::InvalidDraft:
      return "chart draft cannot be edited because its identity or relation data is invalid";
    case ChartEditCode::RangeOverflow:
      return "chart edit exceeds the row limit";
    case ChartEditCode::IdExhausted:
      return "chart object identifiers are exhausted";
    case ChartEditCode::AllocationFailure:
      return "unable to allocate chart edit state";
    case ChartEditCode::NothingToUndo:
      return "there is no chart edit to undo";
    case ChartEditCode::NothingToRedo:
      return "there is no chart edit to redo";
  }
  return "unknown chart edit result";
}

struct ChartEditResult final {
  ChartEditCode code;
  bool changed;
  ChartEventId primary_event_id;

  [[nodiscard]] explicit operator bool() const noexcept {
    return code == ChartEditCode::Success;
  }
};

/// Owns the current ChartDraft and snapshot history. Each successful context
/// click, multi-event generation or deletion is one atomic history entry.
class ChartEditor final {
 public:
  explicit ChartEditor(ChartDraft draft) noexcept;

  [[nodiscard]] const ChartDraft& draft() const noexcept { return draft_; }
  [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
  [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
  [[nodiscard]] std::size_t undo_count() const noexcept { return undo_.size(); }
  [[nodiscard]] std::size_t redo_count() const noexcept { return redo_.size(); }

  [[nodiscard]] ChartEditResult click(ChartColumn column,
                                      std::uint64_t row) noexcept;
  [[nodiscard]] ChartEditResult clear() noexcept;
  [[nodiscard]] ChartEditResult undo() noexcept;
  [[nodiscard]] ChartEditResult redo() noexcept;

 private:
  [[nodiscard]] ChartEditResult commit(ChartDraft draft,
                                       ChartEditResult result,
                                       std::uint64_t next_id,
                                       bool id_available) noexcept;

  ChartDraft draft_;
  std::vector<ChartDraft> undo_;
  std::vector<ChartDraft> redo_;
  std::uint64_t next_id_;
  bool id_available_;
};

}  // namespace otgy::core
