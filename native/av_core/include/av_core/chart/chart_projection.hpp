// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class ChartColumn : std::uint8_t {
  Npc = 0,
  Player = 1,
  Showtime = 2,
};

enum class ChartMarkerType : std::uint8_t {
  NpcBlockStart = 0,
  NpcNote = 1,
  NpcMoveEnd = 2,
  PlayerBlockStart = 3,
  PlayerNote = 4,
  PlayerMoveEnd = 5,
  ShowtimeStart = 6,
  ShowtimeMove = 7,
};

struct ChartMarker final {
  ChartEventId id;
  std::uint64_t row;
  ChartColumn column;
  ChartMarkerType type;
  std::uint64_t show_n_count;
};

enum class ChartProjectionError : std::uint8_t {
  None = 0,
  InvalidDraft = 1,
  InvalidShowNCount = 2,
  RangeOverflow = 3,
  AllocationFailure = 4,
};

[[nodiscard]] constexpr std::string_view chart_projection_error_message(
    ChartProjectionError error) noexcept {
  switch (error) {
    case ChartProjectionError::None:
      return {};
    case ChartProjectionError::InvalidDraft:
      return "chart draft contains an invalid event value";
    case ChartProjectionError::InvalidShowNCount:
      return "Showtime move count must be non-zero";
    case ChartProjectionError::RangeOverflow:
      return "Showtime display range exceeds the chart row limit";
    case ChartProjectionError::AllocationFailure:
      return "unable to allocate chart projection";
  }
  return "unknown chart projection error";
}

class ChartProjectionResult final {
 public:
  explicit ChartProjectionResult(std::vector<ChartMarker> markers) noexcept;
  explicit ChartProjectionResult(ChartProjectionError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::vector<ChartMarker>* markers() const noexcept;
  [[nodiscard]] ChartProjectionError error() const noexcept;
  [[nodiscard]] std::vector<ChartMarker> take_markers() &&;

 private:
  std::variant<std::vector<ChartMarker>, ChartProjectionError> storage_;
};

/// Returns every draft event in deterministic row/id order. Container order is
/// never treated as chart time order.
[[nodiscard]] ChartProjectionResult project_chart_markers(
    const ChartDraft& draft) noexcept;

[[nodiscard]] const ChartMarker* find_chart_marker(
    const std::vector<ChartMarker>& markers, ChartEventId id) noexcept;
[[nodiscard]] const ChartMarker* previous_chart_marker(
    const std::vector<ChartMarker>& markers, std::uint64_t row) noexcept;
[[nodiscard]] const ChartMarker* previous_chart_marker_in_column(
    const std::vector<ChartMarker>& markers, ChartColumn column,
    std::uint64_t row) noexcept;
[[nodiscard]] const ChartMarker* next_chart_marker(
    const std::vector<ChartMarker>& markers, std::uint64_t row) noexcept;

struct ShowtimeMoveProjection final {
  ChartEventId event_id;
  std::uint64_t row;
  std::uint64_t show_n_count;
  std::uint64_t range_start;
  std::uint64_t range_end;
  bool is_first_in_sequence;
  bool is_last_in_sequence;
};

class ShowtimeMoveProjectionResult final {
 public:
  explicit ShowtimeMoveProjectionResult(
      std::vector<ShowtimeMoveProjection> moves) noexcept;
  explicit ShowtimeMoveProjectionResult(ChartProjectionError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::vector<ShowtimeMoveProjection>* moves()
      const noexcept;
  [[nodiscard]] ChartProjectionError error() const noexcept;
  [[nodiscard]] std::vector<ShowtimeMoveProjection> take_moves() &&;

 private:
  std::variant<std::vector<ShowtimeMoveProjection>, ChartProjectionError>
      storage_;
};

[[nodiscard]] ShowtimeMoveProjectionResult project_showtime_moves(
    const ChartDraft& draft) noexcept;

enum class ChartNoteOrdinalError : std::uint8_t {
  None = 0,
  EventNotFound = 1,
  EventIsNotNote = 2,
};

struct ChartNoteOrdinalResult final {
  ChartNoteOrdinalError error;
  std::uint64_t ordinal;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ChartNoteOrdinalError::None;
  }
};

[[nodiscard]] ChartNoteOrdinalResult chart_note_ordinal(
    const ChartDraft& draft, ChartEventId event_id) noexcept;

}  // namespace otgy::core
