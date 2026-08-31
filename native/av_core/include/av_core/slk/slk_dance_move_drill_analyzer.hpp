// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/slk/slk_drill_group_analyzer.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

struct SlkDanceMoveDrillId final {
  std::uint64_t value;
};

struct SlkDanceMoveRow final {
  SlkDrillGroupRow row;
  std::size_t source_group_index;
  std::size_t dance_move_row_index;
  SlkDanceMoveDrillId dance_move_drill_id;
  SlkDrillBlockRole role;
};

struct SlkDanceMove final {
  SlkDrillBlockRole role;
  std::vector<SlkDanceMoveRow> rows;
};

struct SlkDanceMoveDrillGroupMapping final {
  SlkDrillGroupId drill_group_id;
  SlkDrillGroupRow npc_c_row;
  SlkDrillGroupRow player_c_row;
  std::size_t source_group_index;
  std::size_t drill_group_index;
  std::uint64_t c_value;
  std::uint64_t segment_n_count;
};

struct SlkDanceMoveDrill final {
  SlkDanceMoveDrillId id;
  std::uint64_t move_n_count;
  std::vector<SlkDanceMoveDrillGroupMapping> groups;
  SlkDanceMove npc;
  SlkDanceMove player;
};

class SlkDanceMoveDrillAnalysis final {
 public:
  SlkDanceMoveDrillAnalysis(std::vector<SlkDanceMoveDrill> drills,
                            std::size_t begin_row_index,
                            std::size_t next_row_index,
                            SlkOrdinarySequenceEnd end) noexcept;

  [[nodiscard]] const std::vector<SlkDanceMoveDrill>& drills() const noexcept {
    return drills_;
  }
  [[nodiscard]] std::size_t begin_row_index() const noexcept {
    return begin_row_index_;
  }
  [[nodiscard]] std::size_t next_row_index() const noexcept {
    return next_row_index_;
  }
  [[nodiscard]] SlkOrdinarySequenceEnd end() const noexcept {
    return end_;
  }

 private:
  std::vector<SlkDanceMoveDrill> drills_;
  std::size_t begin_row_index_;
  std::size_t next_row_index_;
  SlkOrdinarySequenceEnd end_;
};

enum class SlkDanceMoveDrillError : std::uint8_t {
  None = 0,
  EmptySequence = 1,
  InvalidDrillGroup = 2,
  InvalidCValue = 3,
  SegmentNCountMismatch = 4,
  InconsistentEncoding = 5,
  InconsistentMoveNCount = 6,
  SegmentTotalExceedsMoveNCount = 7,
  PrematureMoveEnd = 8,
  MissingMoveEnd = 9,
  IncompleteMoveNCount = 10,
  SplitDrillRequiresMultipleGroups = 11,
  AllocationFailure = 12,
};

[[nodiscard]] constexpr std::string_view slk_dance_move_drill_error_message(
    SlkDanceMoveDrillError error) noexcept {
  switch (error) {
    case SlkDanceMoveDrillError::None:
      return {};
    case SlkDanceMoveDrillError::EmptySequence:
      return "SLK ordinary sequence contains no drill groups";
    case SlkDanceMoveDrillError::InvalidDrillGroup:
      return "SLK drill group structure is invalid";
    case SlkDanceMoveDrillError::InvalidCValue:
      return "SLK c value does not encode a supported dance move";
    case SlkDanceMoveDrillError::SegmentNCountMismatch:
      return "SLK c segment count does not match actual n rows";
    case SlkDanceMoveDrillError::InconsistentEncoding:
      return "SLK dance move mixes split and unsplit c values";
    case SlkDanceMoveDrillError::InconsistentMoveNCount:
      return "SLK split dance move c values disagree on total n count";
    case SlkDanceMoveDrillError::SegmentTotalExceedsMoveNCount:
      return "SLK split segment counts exceed total dance move n count";
    case SlkDanceMoveDrillError::PrematureMoveEnd:
      return "SLK dance move ends before all n rows are present";
    case SlkDanceMoveDrillError::MissingMoveEnd:
      return "SLK complete dance move is missing its final s row";
    case SlkDanceMoveDrillError::IncompleteMoveNCount:
      return "SLK split dance move ends before reaching its total n count";
    case SlkDanceMoveDrillError::SplitDrillRequiresMultipleGroups:
      return "SLK split c encoding requires multiple drill groups";
    case SlkDanceMoveDrillError::AllocationFailure:
      return "unable to allocate SLK dance move drills";
  }
  return "unknown SLK dance move drill error";
}

struct SlkDanceMoveDrillFailure final {
  SlkDanceMoveDrillError error;
  std::size_t source_group_index;
  std::size_t drill_group_index;
  SlkDrillGroupId drill_group_id;
  std::uint64_t sheet_row;
  std::uint64_t c_value;
};

class SlkDanceMoveDrillAnalysisResult final {
 public:
  explicit SlkDanceMoveDrillAnalysisResult(
      SlkDanceMoveDrillAnalysis analysis) noexcept;
  explicit SlkDanceMoveDrillAnalysisResult(
      SlkDanceMoveDrillFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SlkDanceMoveDrillAnalysis* analysis() const noexcept;
  [[nodiscard]] const SlkDanceMoveDrillFailure* failure() const noexcept;

  [[nodiscard]] SlkDanceMoveDrillAnalysis take_analysis() &&;

 private:
  std::variant<SlkDanceMoveDrillAnalysis, SlkDanceMoveDrillFailure> storage_;
};

/// Decodes and joins the groups in one validated ordinary sequence.
[[nodiscard]] SlkDanceMoveDrillAnalysisResult analyze_slk_dance_move_drills(
    const SlkDrillGroupAnalysis& groups) noexcept;

}  // namespace otgy::core
