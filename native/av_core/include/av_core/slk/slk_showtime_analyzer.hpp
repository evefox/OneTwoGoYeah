// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/slk/slk_dance_move_drill_analyzer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

struct SlkShowtimeId final {
  std::uint64_t value;
};

struct SlkShowDId final {
  std::uint64_t value;
};

struct SlkShowD final {
  SlkShowDId id;
  SlkParsedRow row;
  std::size_t source_row_index;
  std::size_t show_d_index;
  SlkDanceMoveDrillId dance_move_drill_id;
  std::uint64_t show_n_count;
};

struct SlkShowtime final {
  SlkShowtimeId id;
  SlkParsedRow c_row;
  std::size_t source_row_index;
  SlkDanceMoveDrillAnalysis stage;
  std::vector<SlkShowD> show_d_rows;
};

class SlkShowtimeAnalysis final {
 public:
  SlkShowtimeAnalysis(std::vector<SlkShowtime> showtimes,
                      std::optional<SlkParsedRow> boundary_row,
                      std::optional<std::size_t> boundary_source_row_index)
      noexcept;

  [[nodiscard]] const std::vector<SlkShowtime>& showtimes() const noexcept {
    return showtimes_;
  }
  [[nodiscard]] const std::optional<SlkParsedRow>& boundary_row()
      const noexcept {
    return boundary_row_;
  }
  [[nodiscard]] const std::optional<std::size_t>& boundary_source_row_index()
      const noexcept {
    return boundary_source_row_index_;
  }

 private:
  std::vector<SlkShowtime> showtimes_;
  std::optional<SlkParsedRow> boundary_row_;
  std::optional<std::size_t> boundary_source_row_index_;
};

enum class SlkShowtimeError : std::uint8_t {
  None = 0,
  EmptyChart = 1,
  EmptyOrdinaryStage = 2,
  DrillGroupAnalysisFailed = 3,
  DanceMoveDrillAnalysisFailed = 4,
  MissingShowtime = 5,
  ShowtimeDCountMismatch = 6,
  ShowtimeDValueMismatch = 7,
  MissingFileBoundary = 8,
  InvalidFileBoundary = 9,
  AllocationFailure = 10,
};

[[nodiscard]] constexpr std::string_view slk_showtime_error_message(
    SlkShowtimeError error) noexcept {
  switch (error) {
    case SlkShowtimeError::None:
      return {};
    case SlkShowtimeError::EmptyChart:
      return "SLK finished chart contains no Showtime stages";
    case SlkShowtimeError::EmptyOrdinaryStage:
      return "SLK Showtime has no preceding dance move drills";
    case SlkShowtimeError::DrillGroupAnalysisFailed:
      return "SLK drill group analysis failed";
    case SlkShowtimeError::DanceMoveDrillAnalysisFailed:
      return "SLK dance move drill analysis failed";
    case SlkShowtimeError::MissingShowtime:
      return "SLK ordinary stage is not followed by Showtime";
    case SlkShowtimeError::ShowtimeDCountMismatch:
      return "SLK Showtime must contain at least one d row and no more than "
             "its preceding dance move drills";
    case SlkShowtimeError::ShowtimeDValueMismatch:
      return "SLK Showtime d value does not match dance move n count";
    case SlkShowtimeError::MissingFileBoundary:
      return "SLK finished chart is missing its r boundary";
    case SlkShowtimeError::InvalidFileBoundary:
      return "SLK r boundary must be the final semantic row";
    case SlkShowtimeError::AllocationFailure:
      return "unable to allocate SLK Showtime semantics";
  }
  return "unknown SLK Showtime error";
}

struct SlkShowtimeFailure final {
  SlkShowtimeError error;
  std::size_t source_row_index;
  std::uint64_t sheet_row;
  std::size_t showtime_index;
  std::size_t stage_group_index;
  std::size_t show_d_index;
  SlkDrillGroupError drill_group_error;
  SlkDanceMoveDrillError dance_move_drill_error;
};

class SlkShowtimeAnalysisResult final {
 public:
  explicit SlkShowtimeAnalysisResult(SlkShowtimeAnalysis analysis) noexcept;
  explicit SlkShowtimeAnalysisResult(SlkShowtimeFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SlkShowtimeAnalysis* analysis() const noexcept;
  [[nodiscard]] const SlkShowtimeFailure* failure() const noexcept;

  [[nodiscard]] SlkShowtimeAnalysis take_analysis() &&;

 private:
  std::variant<SlkShowtimeAnalysis, SlkShowtimeFailure> storage_;
};

/// Interprets every ordinary stage, Showtime and the final r boundary.
[[nodiscard]] SlkShowtimeAnalysisResult analyze_slk_showtimes(
    const SlkParsedRecords& records) noexcept;

}  // namespace otgy::core
