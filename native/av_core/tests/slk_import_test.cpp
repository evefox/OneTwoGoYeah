// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_import.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::FinishedSlkDocument;
using otgy::core::SlkImportError;
using otgy::core::SlkImportResult;
using otgy::core::SlkParseError;
using otgy::core::SlkShowtimeError;

constexpr const char* kPrefix =
    "ID;PWXL;N;E\r\n"
    "P;PGeneral\r\n"
    "C;Y2;X1;K\"int\"\r\n"
    "C;X2;K\"int\"\r\n"
    "C;X3;K\"int\"\r\n"
    "C;X4;K\"int\"\r\n"
    "C;X5;K\"enum(n,s,c,d,r)\"\r\n"
    "C;X6;K\"int\"\r\n";

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return {text.begin(), text.end()};
}

[[nodiscard]] std::string cell_row(std::uint64_t sheet_row, std::uint64_t total,
                                   char type, std::uint64_t value) {
  const auto position = otgy::core::decompose_slk_position(total);
  assert(position);
  return "C;Y" + std::to_string(sheet_row) + ";X1;K" +
         std::to_string(position.position()->measure) + "\r\n" + "C;X2;K" +
         std::to_string(position.position()->beat) + "\r\n" + "C;X3;K" +
         std::to_string(position.position()->sixteenth) + "\r\n" + "C;X4;K" +
         std::to_string(total) + "\r\n" + "C;X5;K\"" + std::string(1U, type) +
         "\"\r\n" + "C;X6;K" + std::to_string(value) + "\r\n";
}

[[nodiscard]] std::string empty_chart_text() {
  return std::string{kPrefix} + cell_row(3U, 0U, 'r', 0U) + "E\r\n";
}

[[nodiscard]] std::string missing_show_d_text() {
  return std::string{kPrefix} + cell_row(3U, 0U, 'c', 1U) +
         cell_row(4U, 1U, 'n', 0U) + cell_row(5U, 2U, 's', 0U) +
         cell_row(6U, 4U, 'c', 1U) + cell_row(7U, 5U, 'n', 0U) +
         cell_row(8U, 6U, 's', 0U) + cell_row(9U, 8U, 'c', 0U) +
         cell_row(10U, 16U, 'r', 0U) + "E\r\n";
}

[[nodiscard]] const FinishedSlkDocument& document_of(
    const SlkImportResult& result) {
  assert(result);
  assert(result.document() != nullptr);
  assert(result.failure() == nullptr);
  return *result.document();
}

void test_repository_files_and_ownership() {
  auto canonical_bytes =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  auto canonical_result = otgy::core::import_finished_slk(
      canonical_bytes.data(), canonical_bytes.size());
  const auto& canonical = document_of(canonical_result);
  assert(canonical.semantics().showtimes().size() == 2U);
  assert(canonical.semantics().showtimes()[0].show_d_rows.size() == 6U);
  assert(canonical.semantics().showtimes()[1].show_d_rows.size() == 11U);
  assert(canonical.semantics().boundary_row().sheet_row == 430U);

  std::fill(canonical_bytes.begin(), canonical_bytes.end(), 0U);
  assert(canonical.semantics().showtimes()[0].id.value == 155U);
  assert(canonical.semantics().boundary_row().sheet_row == 430U);

  const auto copy = *canonical_result.document();
  const auto moved = std::move(canonical_result).take_document();
  assert(copy.semantics().showtimes().size() == 2U);
  assert(moved.semantics().showtimes().size() == 2U);
  assert(copy.semantics().showtimes()[0].show_d_rows.data() !=
         moved.semantics().showtimes()[0].show_d_rows.data());

  const auto compatibility_bytes =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_result = otgy::core::import_finished_slk(
      compatibility_bytes.data(), compatibility_bytes.size());
  const auto& compatibility = document_of(compatibility_result);
  assert(compatibility.semantics().showtimes().size() == 2U);
  assert(compatibility.semantics().showtimes()[0].show_d_rows.size() == 8U);
  assert(compatibility.semantics().showtimes()[1].show_d_rows.size() == 9U);
  assert(compatibility.semantics().boundary_row().sheet_row == 406U);
}

void test_parse_failures() {
  const auto empty = otgy::core::import_finished_slk(nullptr, 0U);
  assert(!empty);
  assert(empty.document() == nullptr);
  assert(empty.failure()->error == SlkImportError::RecordParseFailed);
  assert(empty.failure()->parse_failure.error == SlkParseError::EmptyInput);
  assert(empty.failure()->semantic_failure.error == SlkShowtimeError::None);

  const auto null_input = otgy::core::import_finished_slk(nullptr, 1U);
  assert(!null_input);
  assert(null_input.failure()->error == SlkImportError::RecordParseFailed);
  assert(null_input.failure()->parse_failure.error == SlkParseError::NullInput);
  assert(null_input.failure()->semantic_failure.error ==
         SlkShowtimeError::None);

  auto invalid_header = bytes_of(empty_chart_text());
  invalid_header[0] = 'X';
  const auto invalid = otgy::core::import_finished_slk(invalid_header.data(),
                                                       invalid_header.size());
  assert(!invalid);
  assert(invalid.failure()->parse_failure.error ==
         SlkParseError::InvalidHeader);
}

void test_semantic_failures_are_atomic() {
  const auto empty_chart_bytes = bytes_of(empty_chart_text());
  const auto empty_chart = otgy::core::import_finished_slk(
      empty_chart_bytes.data(), empty_chart_bytes.size());
  assert(!empty_chart);
  assert(empty_chart.document() == nullptr);
  assert(empty_chart.failure()->error ==
         SlkImportError::SemanticAnalysisFailed);
  assert(empty_chart.failure()->parse_failure.error == SlkParseError::None);
  assert(empty_chart.failure()->semantic_failure.error ==
         SlkShowtimeError::EmptyChart);

  const auto missing_d_bytes = bytes_of(missing_show_d_text());
  const auto missing_d = otgy::core::import_finished_slk(
      missing_d_bytes.data(), missing_d_bytes.size());
  assert(!missing_d);
  assert(missing_d.document() == nullptr);
  assert(missing_d.failure()->error == SlkImportError::SemanticAnalysisFailed);
  assert(missing_d.failure()->parse_failure.error == SlkParseError::None);
  assert(missing_d.failure()->semantic_failure.error ==
         SlkShowtimeError::ShowtimeDCountMismatch);
  assert(missing_d.failure()->semantic_failure.source_row_index == 7U);
  assert(missing_d.failure()->semantic_failure.sheet_row == 10U);
}

void test_error_messages() {
  assert(otgy::core::slk_import_error_message(SlkImportError::None).empty());
  assert(
      !otgy::core::slk_import_error_message(SlkImportError::RecordParseFailed)
           .empty());
  assert(!otgy::core::slk_import_error_message(
              SlkImportError::SemanticAnalysisFailed)
              .empty());
  assert(!otgy::core::slk_import_error_message(static_cast<SlkImportError>(255))
              .empty());
}

}  // namespace

int main() {
  test_repository_files_and_ownership();
  test_parse_failures();
  test_semantic_failures_are_atomic();
  test_error_messages();
  return 0;
}
