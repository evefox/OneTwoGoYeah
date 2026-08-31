// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"
#include "av_core/slk/slk_import.hpp"

#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class SlkChartDraftProjectionError : std::uint8_t {
  None = 0,
  InvalidFinishedDocument = 1,
  AllocationFailure = 2,
};

[[nodiscard]] constexpr std::string_view
slk_chart_draft_projection_error_message(
    SlkChartDraftProjectionError error) noexcept {
  switch (error) {
    case SlkChartDraftProjectionError::None:
      return {};
    case SlkChartDraftProjectionError::InvalidFinishedDocument:
      return "finished SLK document cannot be projected to ChartDraft";
    case SlkChartDraftProjectionError::AllocationFailure:
      return "unable to allocate imported ChartDraft";
  }
  return "unknown SLK ChartDraft projection error";
}

struct SlkChartDraftProjectionFailure final {
  SlkChartDraftProjectionError error;
  std::uint64_t source_sheet_row;
};

class SlkChartDraftProjectionResult final {
 public:
  explicit SlkChartDraftProjectionResult(ChartDraft draft) noexcept;
  explicit SlkChartDraftProjectionResult(
      SlkChartDraftProjectionFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const ChartDraft* draft() const noexcept;
  [[nodiscard]] const SlkChartDraftProjectionFailure* failure() const noexcept;

  [[nodiscard]] ChartDraft take_draft() &&;

 private:
  std::variant<ChartDraft, SlkChartDraftProjectionFailure> storage_;
};

/// Projects validated finished semantics without rescanning SLK rows.
[[nodiscard]] SlkChartDraftProjectionResult project_finished_slk_to_chart_draft(
    const FinishedSlkDocument& document) noexcept;

}  // namespace otgy::core
