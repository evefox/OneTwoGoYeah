// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_import.hpp"

#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] constexpr SlkParseFailure no_parse_failure() noexcept {
  return {SlkParseError::None, 0U, 0U, 0U};
}

[[nodiscard]] constexpr SlkShowtimeFailure no_semantic_failure() noexcept {
  return {
      SlkShowtimeError::None,      0U, 0U, 0U, 0U, 0U, SlkDrillGroupError::None,
      SlkDanceMoveDrillError::None};
}

}  // namespace

FinishedSlkDocument::FinishedSlkDocument(SlkShowtimeAnalysis semantics) noexcept
    : semantics_(std::move(semantics)) {}

SlkImportResult::SlkImportResult(FinishedSlkDocument document) noexcept
    : storage_(std::move(document)) {}

SlkImportResult::SlkImportResult(SlkImportFailure failure) noexcept
    : storage_(failure) {}

bool SlkImportResult::has_value() const noexcept {
  return std::holds_alternative<FinishedSlkDocument>(storage_);
}

const FinishedSlkDocument* SlkImportResult::document() const noexcept {
  return std::get_if<FinishedSlkDocument>(&storage_);
}

const SlkImportFailure* SlkImportResult::failure() const noexcept {
  return std::get_if<SlkImportFailure>(&storage_);
}

FinishedSlkDocument SlkImportResult::take_document() && {
  return std::move(std::get<FinishedSlkDocument>(storage_));
}

SlkImportResult import_finished_slk(const std::uint8_t* bytes,
                                    std::size_t byte_count) noexcept {
  auto parse_result = parse_slk_records(bytes, byte_count);
  if (!parse_result) {
    return SlkImportResult{{SlkImportError::RecordParseFailed,
                            *parse_result.failure(), no_semantic_failure()}};
  }

  auto records = std::move(parse_result).take_records();
  auto semantic_result = analyze_slk_showtimes(records);
  if (!semantic_result) {
    return SlkImportResult{{SlkImportError::SemanticAnalysisFailed,
                            no_parse_failure(), *semantic_result.failure()}};
  }
  return SlkImportResult{
      FinishedSlkDocument{std::move(semantic_result).take_analysis()}};
}

}  // namespace otgy::core
