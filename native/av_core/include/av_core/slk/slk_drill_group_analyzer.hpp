// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/slk/slk_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class SlkDrillBlockRole : std::uint8_t {
  Npc = 0,
  Player = 1,
};

struct SlkDrillGroupId final {
  std::uint64_t value;
};

struct SlkDrillGroupRow final {
  SlkParsedRow row;
  std::size_t source_row_index;
  std::size_t block_row_index;
  SlkDrillGroupId drill_group_id;
  SlkDrillBlockRole role;
};

struct SlkDrillBlock final {
  SlkDrillBlockRole role;
  std::vector<SlkDrillGroupRow> rows;
};

struct SlkDrillGroup final {
  SlkDrillGroupId id;
  SlkDrillBlock npc;
  SlkDrillBlock player;
};

enum class SlkOrdinarySequenceEnd : std::uint8_t {
  Showtime = 0,
  FileBoundary = 1,
  /// The records end without a trailing `r` boundary row.
  EndOfRecords = 2,
};

class SlkDrillGroupAnalysis final {
 public:
  SlkDrillGroupAnalysis(std::vector<SlkDrillGroup> groups,
                        std::size_t begin_row_index, std::size_t next_row_index,
                        SlkOrdinarySequenceEnd end) noexcept;

  [[nodiscard]] const std::vector<SlkDrillGroup>& groups() const noexcept {
    return groups_;
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
  std::vector<SlkDrillGroup> groups_;
  std::size_t begin_row_index_;
  std::size_t next_row_index_;
  SlkOrdinarySequenceEnd end_;
};

enum class SlkDrillGroupError : std::uint8_t {
  None = 0,
  InvalidBeginRow = 1,
  ExpectedBlockStart = 2,
  MissingNRows = 3,
  MissingPlayerBlock = 4,
  BlockLengthMismatch = 5,
  BlockTypeMismatch = 6,
  BlockValueMismatch = 7,
  BlockRelativePositionMismatch = 8,
  MissingSequenceEnd = 9,
  AllocationFailure = 10,
};

[[nodiscard]] constexpr std::string_view slk_drill_group_error_message(
    SlkDrillGroupError error) noexcept {
  switch (error) {
    case SlkDrillGroupError::None:
      return {};
    case SlkDrillGroupError::InvalidBeginRow:
      return "SLK ordinary sequence begin row is out of range";
    case SlkDrillGroupError::ExpectedBlockStart:
      return "SLK drill block must start with nonzero c";
    case SlkDrillGroupError::MissingNRows:
      return "SLK drill block must contain at least one n row";
    case SlkDrillGroupError::MissingPlayerBlock:
      return "SLK NPC drill block has no adjacent player block";
    case SlkDrillGroupError::BlockLengthMismatch:
      return "SLK NPC and player drill blocks have different lengths";
    case SlkDrillGroupError::BlockTypeMismatch:
      return "SLK NPC and player drill block row types differ";
    case SlkDrillGroupError::BlockValueMismatch:
      return "SLK NPC and player drill block values differ";
    case SlkDrillGroupError::BlockRelativePositionMismatch:
      return "SLK NPC and player drill block relative positions differ";
    case SlkDrillGroupError::MissingSequenceEnd:
      return "SLK ordinary sequence has no Showtime or file boundary";
    case SlkDrillGroupError::AllocationFailure:
      return "unable to allocate SLK drill groups";
  }
  return "unknown SLK drill group error";
}

struct SlkDrillGroupFailure final {
  SlkDrillGroupError error;
  std::size_t source_row_index;
  std::uint64_t sheet_row;
  std::size_t block_row_index;
  SlkDrillBlockRole role;
};

class SlkDrillGroupAnalysisResult final {
 public:
  explicit SlkDrillGroupAnalysisResult(SlkDrillGroupAnalysis analysis) noexcept;
  explicit SlkDrillGroupAnalysisResult(SlkDrillGroupFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SlkDrillGroupAnalysis* analysis() const noexcept;
  [[nodiscard]] const SlkDrillGroupFailure* failure() const noexcept;

  [[nodiscard]] SlkDrillGroupAnalysis take_analysis() &&;

 private:
  std::variant<SlkDrillGroupAnalysis, SlkDrillGroupFailure> storage_;
};

/// Analyzes one ordinary block sequence and stops before c0 or r.
[[nodiscard]] SlkDrillGroupAnalysisResult analyze_slk_drill_groups(
    const SlkParsedRecords& records, std::size_t begin_row_index = 0U) noexcept;

}  // namespace otgy::core
