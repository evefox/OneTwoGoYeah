// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_validator.hpp"

#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class SlkExportError : std::uint8_t {
  None = 0,
  InvalidFinishedChart = 1,
  SizeOverflow = 2,
  AllocationFailure = 3,
};

[[nodiscard]] constexpr std::string_view slk_export_error_message(
    SlkExportError error) noexcept {
  switch (error) {
    case SlkExportError::None:
      return {};
    case SlkExportError::InvalidFinishedChart:
      return "finished chart cannot be encoded as canonical SLK";
    case SlkExportError::SizeOverflow:
      return "canonical SLK output dimensions exceed supported limits";
    case SlkExportError::AllocationFailure:
      return "unable to allocate canonical SLK output";
  }
  return "unknown SLK export error";
}

class SlkExportResult final {
 public:
  explicit SlkExportResult(std::vector<std::uint8_t> bytes) noexcept;
  explicit SlkExportResult(SlkExportError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const std::vector<std::uint8_t>* bytes() const noexcept;
  [[nodiscard]] SlkExportError error() const noexcept;

  [[nodiscard]] std::vector<std::uint8_t> take_bytes() &&;

 private:
  std::variant<std::vector<std::uint8_t>, SlkExportError> storage_;
};

/// Encodes a validated snapshot into the z3116_lv5.slk canonical byte shell.
[[nodiscard]] SlkExportResult export_finished_chart_to_slk(
    const FinishedChart& chart) noexcept;

}  // namespace otgy::core
