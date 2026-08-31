// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/slk/slk_parser.hpp"
#include "av_core/slk/slk_showtime_analyzer.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

class FinishedSlkDocument final {
 public:
  explicit FinishedSlkDocument(SlkShowtimeAnalysis semantics) noexcept;

  [[nodiscard]] const SlkShowtimeAnalysis& semantics() const noexcept {
    return semantics_;
  }

 private:
  SlkShowtimeAnalysis semantics_;
};

enum class SlkImportError : std::uint8_t {
  None = 0,
  RecordParseFailed = 1,
  SemanticAnalysisFailed = 2,
};

[[nodiscard]] constexpr std::string_view slk_import_error_message(
    SlkImportError error) noexcept {
  switch (error) {
    case SlkImportError::None:
      return {};
    case SlkImportError::RecordParseFailed:
      return "SLK record parsing failed";
    case SlkImportError::SemanticAnalysisFailed:
      return "SLK finished chart semantic analysis failed";
  }
  return "unknown SLK import error";
}

struct SlkImportFailure final {
  SlkImportError error;
  SlkParseFailure parse_failure;
  SlkShowtimeFailure semantic_failure;
};

class SlkImportResult final {
 public:
  explicit SlkImportResult(FinishedSlkDocument document) noexcept;
  explicit SlkImportResult(SlkImportFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const FinishedSlkDocument* document() const noexcept;
  [[nodiscard]] const SlkImportFailure* failure() const noexcept;

  [[nodiscard]] FinishedSlkDocument take_document() &&;

 private:
  std::variant<FinishedSlkDocument, SlkImportFailure> storage_;
};

/// Strictly parses and interprets one complete finished SLK byte buffer.
[[nodiscard]] SlkImportResult import_finished_slk(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept;

}  // namespace otgy::core
