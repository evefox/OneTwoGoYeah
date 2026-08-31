// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_parser.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::SlkParseError;
using otgy::core::SlkParseResult;
using otgy::core::SlkParsedRecords;
using otgy::core::SlkRowType;

constexpr const char* kPrefix =
    "ID;PWXL;N;E\r\n"
    "P;PGeneral\r\n"
    "C;Y2;X1;K\"int\"\r\n"
    "C;X2;K\"int\"\r\n"
    "C;X3;K\"int\"\r\n"
    "C;X4;K\"int\"\r\n"
    "C;X5;K\"enum(n,s,c,d,r)\"\r\n"
    "C;X6;K\"int\"\r\n";

[[nodiscard]] std::vector<std::uint8_t> read_file(
    const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(
    const std::string& text) {
  return {text.begin(), text.end()};
}

[[nodiscard]] std::string cell_row(std::uint64_t sheet_row,
                                   std::uint64_t measure,
                                   std::uint64_t beat,
                                   std::uint64_t sixteenth,
                                   std::uint64_t total,
                                   char type,
                                   std::uint64_t value) {
  return "C;Y" + std::to_string(sheet_row) + ";X1;K" +
         std::to_string(measure) + "\r\n" +
         "C;X2;K" + std::to_string(beat) + "\r\n" +
         "C;X3;K" + std::to_string(sixteenth) + "\r\n" +
         "C;X4;K" + std::to_string(total) + "\r\n" +
         "C;X5;K\"" + std::string(1U, type) + "\"\r\n" +
         "C;X6;K" + std::to_string(value) + "\r\n";
}

[[nodiscard]] std::string valid_text() {
  return std::string{kPrefix} +
         cell_row(3U, 0U, 0U, 0U, 0U, 'c', 1U) +
         cell_row(4U, 0U, 0U, 1U, 1U, 'n', 0U) +
         "F;P0;FG0C;SM24;X7\r\n" +
         cell_row(5U, 0U, 0U, 2U, 2U, 'r', 0U) + "E\r\n";
}

[[nodiscard]] const SlkParsedRecords& records_of(
    const SlkParseResult& result) {
  assert(result);
  assert(result.records() != nullptr);
  assert(result.failure() == nullptr);
  return *result.records();
}

void assert_error(const std::vector<std::uint8_t>& bytes,
                  SlkParseError expected,
                  std::uint64_t expected_sheet_row = 0U,
                  std::uint8_t expected_column = 0U) {
  const auto result = otgy::core::parse_slk_records(
      bytes.data(), bytes.size());
  assert(!result);
  assert(result.records() == nullptr);
  assert(result.failure() != nullptr);
  assert(result.failure()->error == expected);
  if (expected_sheet_row != 0U) {
    assert(result.failure()->sheet_row == expected_sheet_row);
  }
  if (expected_column != 0U) {
    assert(result.failure()->column == expected_column);
  }
}

void test_small_supported_document_and_ownership() {
  auto bytes = bytes_of(valid_text());
  auto result = otgy::core::parse_slk_records(bytes.data(), bytes.size());
  const auto& rows = records_of(result).rows();
  assert(rows.size() == 3U);
  assert(rows[0].sheet_row == 3U);
  assert(rows[0].position.total_sixteenth == 0U);
  assert(rows[0].type == SlkRowType::C);
  assert(rows[0].value == 1U);
  assert(rows[1].type == SlkRowType::N);
  assert(rows[2].type == SlkRowType::R);

  std::fill(bytes.begin(), bytes.end(), 0U);
  assert(rows[0].position.total_sixteenth == 0U);
  assert(rows[2].value == 0U);

  const auto copy = *result.records();
  const auto moved = std::move(result).take_records();
  assert(copy.rows().size() == 3U);
  assert(moved.rows().size() == 3U);
  assert(copy.rows().data() != moved.rows().data());
}

void test_repository_compatibility_files() {
  const auto canonical = read_file(
      std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto canonical_result = otgy::core::parse_slk_records(
      canonical.data(), canonical.size());
  const auto& canonical_rows = records_of(canonical_result).rows();
  assert(canonical_rows.size() == 428U);
  assert(canonical_rows.front().sheet_row == 3U);
  assert(canonical_rows.front().position.total_sixteenth == 206U);
  assert(canonical_rows.front().type == SlkRowType::C);
  assert(canonical_rows.front().value == 611U);
  assert(canonical_rows.back().sheet_row == 430U);
  assert(canonical_rows.back().type == SlkRowType::R);
  assert(canonical_rows.back().position.total_sixteenth == 1'920U);

  const auto compatibility = read_file(
      std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto compatibility_result = otgy::core::parse_slk_records(
      compatibility.data(), compatibility.size());
  const auto& compatibility_rows =
      records_of(compatibility_result).rows();
  assert(compatibility_rows.size() == 404U);
  assert(compatibility_rows.front().position.total_sixteenth == 172U);
  assert(compatibility_rows.front().value == 509U);
  assert(compatibility_rows.back().sheet_row == 406U);
  assert(compatibility_rows.back().type == SlkRowType::R);
  assert(compatibility_rows.back().position.total_sixteenth == 1'808U);
}

void test_container_and_schema_errors() {
  assert_error({}, SlkParseError::EmptyInput);
  const auto null_result = otgy::core::parse_slk_records(nullptr, 1U);
  assert(!null_result);
  assert(null_result.failure()->error == SlkParseError::NullInput);

  auto bare_lf = valid_text();
  for (std::size_t index = 0U; index + 1U < bare_lf.size(); ++index) {
    if (bare_lf[index] == '\r' && bare_lf[index + 1U] == '\n') {
      bare_lf.erase(index, 1U);
      break;
    }
  }
  assert_error(bytes_of(bare_lf), SlkParseError::InvalidLineEnding);

  auto bad_header = valid_text();
  bad_header[0] = 'X';
  assert_error(bytes_of(bad_header), SlkParseError::InvalidHeader);

  auto unknown_record = valid_text();
  unknown_record.insert(unknown_record.find("P;PGeneral"),
                        "Z;unsupported\r\n");
  assert_error(bytes_of(unknown_record), SlkParseError::InvalidRecord);

  auto bad_schema = valid_text();
  const auto schema = bad_schema.find("C;X3;K\"int\"");
  assert(schema != std::string::npos);
  bad_schema.replace(schema, 13U, "C;X3;K\"str\"");
  assert_error(bytes_of(bad_schema), SlkParseError::InvalidTypeSchema);

  auto no_end = valid_text();
  no_end.erase(no_end.size() - 3U);
  assert_error(bytes_of(no_end), SlkParseError::MissingContainerEnd);
}

void test_cell_integer_position_and_order_errors() {
  auto wrong_column = valid_text();
  const auto x3 = wrong_column.find("C;X3;K0", wrong_column.find("C;Y3"));
  assert(x3 != std::string::npos);
  wrong_column[x3 + 3U] = '4';
  assert_error(bytes_of(wrong_column), SlkParseError::InvalidCellOrder,
               3U, 3U);

  auto sheet_gap = valid_text();
  const auto y4 = sheet_gap.find("C;Y4;");
  assert(y4 != std::string::npos);
  sheet_gap[y4 + 3U] = '5';
  assert_error(bytes_of(sheet_gap), SlkParseError::InvalidSheetRow, 5U, 1U);

  auto invalid_integer = valid_text();
  const auto measure = invalid_integer.find("C;Y3;X1;K0");
  assert(measure != std::string::npos);
  invalid_integer[measure + 9U] = '+';
  assert_error(bytes_of(invalid_integer), SlkParseError::InvalidInteger,
               3U, 1U);

  auto overflow = valid_text();
  const auto total = overflow.find("C;X4;K0", overflow.find("C;Y3"));
  assert(total != std::string::npos);
  overflow.replace(total, 7U, "C;X4;K18446744073709551616");
  assert_error(bytes_of(overflow), SlkParseError::IntegerOverflow,
               3U, 4U);

  auto inconsistent = valid_text();
  const auto row4_total = inconsistent.find(
      "C;X4;K1", inconsistent.find("C;Y4"));
  assert(row4_total != std::string::npos);
  inconsistent[row4_total + 6U] = '2';
  assert_error(bytes_of(inconsistent), SlkParseError::InvalidPosition,
               4U, 4U);

  const std::string non_increasing =
      std::string{kPrefix} +
      cell_row(3U, 0U, 0U, 1U, 1U, 'c', 1U) +
      cell_row(4U, 0U, 0U, 0U, 0U, 'r', 0U) + "E\r\n";
  assert_error(bytes_of(non_increasing),
               SlkParseError::NonIncreasingPosition, 4U, 4U);
}

void test_type_value_and_boundary_errors() {
  auto unsupported_type = valid_text();
  const auto type = unsupported_type.find("C;X5;K\"n\"");
  assert(type != std::string::npos);
  unsupported_type[type + 7U] = 'x';
  assert_error(bytes_of(unsupported_type),
               SlkParseError::UnsupportedRowType, 4U, 5U);

  auto unsupported_value = valid_text();
  const auto n_value = unsupported_value.find(
      "C;X6;K0", unsupported_value.find("C;Y4"));
  assert(n_value != std::string::npos);
  unsupported_value[n_value + 6U] = '1';
  assert_error(bytes_of(unsupported_value),
               SlkParseError::UnsupportedTypeValue, 4U, 6U);

  const std::string missing_boundary =
      std::string{kPrefix} +
      cell_row(3U, 0U, 0U, 0U, 0U, 'c', 1U) +
      cell_row(4U, 0U, 0U, 1U, 1U, 'n', 0U) + "E\r\n";
  assert_error(bytes_of(missing_boundary), SlkParseError::MissingBoundary);

  const std::string boundary_not_last =
      std::string{kPrefix} +
      cell_row(3U, 0U, 0U, 0U, 0U, 'r', 0U) +
      cell_row(4U, 0U, 0U, 1U, 1U, 'n', 0U) + "E\r\n";
  assert_error(bytes_of(boundary_not_last),
               SlkParseError::BoundaryNotLast, 4U, 5U);
}

void test_error_messages() {
  assert(otgy::core::slk_parse_error_message(SlkParseError::None).empty());
  for (unsigned int raw = 1U;
       raw <= static_cast<unsigned int>(SlkParseError::AllocationFailure);
       ++raw) {
    assert(!otgy::core::slk_parse_error_message(
                static_cast<SlkParseError>(raw))
                .empty());
  }
  assert(!otgy::core::slk_parse_error_message(
              static_cast<SlkParseError>(255))
              .empty());
}

}  // namespace

int main() {
  test_small_supported_document_and_ownership();
  test_repository_compatibility_files();
  test_container_and_schema_errors();
  test_cell_integer_position_and_order_errors();
  test_type_value_and_boundary_errors();
  test_error_messages();
  return 0;
}
