// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

class FinishedChart final {
 public:
  explicit FinishedChart(ChartDraft draft) noexcept;

  [[nodiscard]] const ChartDraft& draft() const noexcept {
    return draft_;
  }

 private:
  ChartDraft draft_;
};

enum class ChartDiagnosticCode : std::uint8_t {
  EmptyChart = 0,
  DuplicateEventId = 1,
  DuplicateEventRow = 2,
  DuplicateDrillGroupId = 3,
  DuplicateDanceMoveDrillId = 4,
  DuplicateShowtimeId = 5,
  MissingReference = 6,
  InvalidEventRelation = 7,
  InvalidDrillBlock = 8,
  DrillBlockMismatch = 9,
  InvalidDanceMoveDrill = 10,
  InvalidShowtime = 11,
  OrphanEvent = 12,
  OrphanDrillGroup = 13,
  OrphanDanceMoveDrill = 14,
};

[[nodiscard]] constexpr std::string_view chart_diagnostic_message(
    ChartDiagnosticCode code) noexcept {
  switch (code) {
    case ChartDiagnosticCode::EmptyChart:
      return "finished chart must contain at least one Showtime";
    case ChartDiagnosticCode::DuplicateEventId:
      return "chart event identifiers must be unique";
    case ChartDiagnosticCode::DuplicateEventRow:
      return "only one chart event may occupy a time row";
    case ChartDiagnosticCode::DuplicateDrillGroupId:
      return "drill group identifiers must be unique";
    case ChartDiagnosticCode::DuplicateDanceMoveDrillId:
      return "dance move drill identifiers must be unique";
    case ChartDiagnosticCode::DuplicateShowtimeId:
      return "Showtime identifiers must be unique";
    case ChartDiagnosticCode::MissingReference:
      return "chart relation references a missing object";
    case ChartDiagnosticCode::InvalidEventRelation:
      return "chart event relation fields are inconsistent";
    case ChartDiagnosticCode::InvalidDrillBlock:
      return "finished drill block structure is invalid";
    case ChartDiagnosticCode::DrillBlockMismatch:
      return "NPC and player drill blocks do not match";
    case ChartDiagnosticCode::InvalidDanceMoveDrill:
      return "finished dance move drill is invalid";
    case ChartDiagnosticCode::InvalidShowtime:
      return "finished Showtime relation is invalid";
    case ChartDiagnosticCode::OrphanEvent:
      return "chart event is not owned by exactly one relation";
    case ChartDiagnosticCode::OrphanDrillGroup:
      return "drill group is not owned by exactly one dance move drill";
    case ChartDiagnosticCode::OrphanDanceMoveDrill:
      return "dance move drill is not owned by exactly one Showtime";
  }
  return "unknown chart diagnostic";
}

struct ChartDiagnostic final {
  ChartDiagnosticCode code;
  std::uint64_t object_id;
  std::uint64_t related_id;
  std::uint64_t row;
};

enum class ChartValidationError : std::uint8_t {
  None = 0,
  AllocationFailure = 1,
};

class ChartValidationResult final {
 public:
  explicit ChartValidationResult(FinishedChart chart) noexcept;
  explicit ChartValidationResult(
      std::vector<ChartDiagnostic> diagnostics) noexcept;
  explicit ChartValidationResult(ChartValidationError error) noexcept;

  [[nodiscard]] bool is_finished() const noexcept;
  [[nodiscard]] const FinishedChart* finished_chart() const noexcept;
  [[nodiscard]] const std::vector<ChartDiagnostic>* diagnostics()
      const noexcept;
  [[nodiscard]] ChartValidationError error() const noexcept;

  [[nodiscard]] FinishedChart take_finished_chart() &&;

 private:
  std::variant<FinishedChart, std::vector<ChartDiagnostic>,
               ChartValidationError>
      storage_;
};

/// Validates without modifying or repairing the supplied draft.
[[nodiscard]] ChartValidationResult validate_finished_chart(
    const ChartDraft& draft) noexcept;

}  // namespace otgy::core
