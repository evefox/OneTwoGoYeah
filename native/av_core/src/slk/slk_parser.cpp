// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_parser.hpp"

#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace otgy::core {
namespace {

constexpr std::array<std::string_view, 6U> kTypeSchema{{
    "C;Y2;X1;K\"int\"",
    "C;X2;K\"int\"",
    "C;X3;K\"int\"",
    "C;X4;K\"int\"",
    "C;X5;K\"enum(n,s,c,d,r)\"",
    "C;X6;K\"int\"",
}};

struct ParserState final {
  std::vector<SlkParsedRow> rows;
  std::array<std::string_view, 6U> cells{};
  std::uint64_t current_line = 0U;
  std::uint64_t current_sheet_row = 0U;
  std::uint64_t next_sheet_row = 3U;
  std::size_t schema_index = 0U;
  std::uint8_t expected_column = 1U;
  bool data_started = false;
  bool boundary_seen = false;
  bool container_end_seen = false;
};

[[nodiscard]] SlkParseFailure failure_details(
    SlkParseError error, const ParserState& state,
    std::uint64_t sheet_row = 0U, std::uint8_t column = 0U) noexcept {
  return {error, state.current_line, sheet_row, column};
}

struct StepResult final {
  bool ok;
  SlkParseFailure failure;
};

[[nodiscard]] StepResult step_success() noexcept {
  return {true, {SlkParseError::None, 0U, 0U, 0U}};
}

[[nodiscard]] StepResult step_failure(
    SlkParseError error, const ParserState& state,
    std::uint64_t sheet_row = 0U, std::uint8_t column = 0U) noexcept {
  return {false, failure_details(error, state, sheet_row, column)};
}

enum class DecimalError : std::uint8_t {
  None,
  Invalid,
  Overflow,
};

[[nodiscard]] DecimalError parse_decimal(
    std::string_view text, std::uint64_t& value) noexcept {
  if (text.empty()) {
    return DecimalError::Invalid;
  }
  constexpr auto kMaximum = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t parsed = 0U;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return DecimalError::Invalid;
    }
    const auto digit = static_cast<std::uint64_t>(character - '0');
    if (parsed > (kMaximum - digit) / 10U) {
      return DecimalError::Overflow;
    }
    parsed = parsed * 10U + digit;
  }
  value = parsed;
  return DecimalError::None;
}

[[nodiscard]] SlkParseError decimal_parse_error(
    DecimalError error) noexcept {
  return error == DecimalError::Overflow ? SlkParseError::IntegerOverflow
                                         : SlkParseError::InvalidInteger;
}

[[nodiscard]] bool starts_with(std::string_view text,
                               std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] bool supported_preamble_record(
    std::string_view line) noexcept {
  return starts_with(line, "P;") || starts_with(line, "F;") ||
         starts_with(line, "B;") || starts_with(line, "O;") ||
         starts_with(line, "NN;") || starts_with(line, "C;");
}

[[nodiscard]] bool valid_id_record(std::string_view line) noexcept {
  if (!starts_with(line, "ID;")) {
    return false;
  }

  bool has_name_flag = false;
  std::size_t token_begin = 0U;
  std::size_t token_count = 0U;
  while (token_begin <= line.size()) {
    const auto separator = line.find(';', token_begin);
    const auto token_end = separator == std::string_view::npos
                               ? line.size()
                               : separator;
    const auto token = line.substr(token_begin, token_end - token_begin);
    if (token.empty()) {
      return false;
    }
    if (token_count == 0U && token != "ID") {
      return false;
    }
    if (token == "N") {
      has_name_flag = true;
    }
    ++token_count;
    if (separator == std::string_view::npos) {
      break;
    }
    token_begin = separator + 1U;
  }

  const auto last_separator = line.rfind(';');
  return token_count >= 3U && has_name_flag &&
         last_separator != std::string_view::npos &&
         line.substr(last_separator + 1U) == "E";
}

[[nodiscard]] bool parse_first_cell(
    std::string_view line, std::uint64_t& sheet_row,
    std::string_view& value) noexcept {
  constexpr std::string_view kPrefix = "C;Y";
  constexpr std::string_view kSeparator = ";X1;K";
  if (!starts_with(line, kPrefix)) {
    return false;
  }
  const auto separator = line.find(kSeparator, kPrefix.size());
  if (separator == std::string_view::npos) {
    return false;
  }
  if (parse_decimal(
          line.substr(kPrefix.size(), separator - kPrefix.size()),
          sheet_row) != DecimalError::None) {
    return false;
  }
  value = line.substr(separator + kSeparator.size());
  return true;
}

[[nodiscard]] bool parse_following_cell(
    std::string_view line, std::uint8_t expected_column,
    std::string_view& value) noexcept {
  const char column_character =
      static_cast<char>('0' + expected_column);
  const std::array<char, 7U> prefix{{
      'C', ';', 'X', column_character, ';', 'K', '\0'}};
  const std::string_view prefix_view{prefix.data(), prefix.size() - 1U};
  if (!starts_with(line, prefix_view)) {
    return false;
  }
  value = line.substr(prefix_view.size());
  return true;
}

[[nodiscard]] bool parse_row_type(std::string_view text,
                                  SlkRowType& type) noexcept {
  if (text.size() != 3U || text.front() != '"' || text.back() != '"') {
    return false;
  }
  switch (text[1]) {
    case 'n':
      type = SlkRowType::N;
      return true;
    case 's':
      type = SlkRowType::S;
      return true;
    case 'c':
      type = SlkRowType::C;
      return true;
    case 'd':
      type = SlkRowType::D;
      return true;
    case 'r':
      type = SlkRowType::R;
      return true;
  }
  return false;
}

[[nodiscard]] bool supported_type_value(SlkRowType type,
                                        std::uint64_t value) noexcept {
  switch (type) {
    case SlkRowType::N:
    case SlkRowType::S:
    case SlkRowType::R:
      return value == 0U;
    case SlkRowType::C:
      return true;
    case SlkRowType::D:
      return value != 0U;
  }
  return false;
}

[[nodiscard]] StepResult append_completed_row(
    ParserState& state) {
  std::array<std::uint64_t, 4U> integers{};
  for (std::size_t index = 0U; index < integers.size(); ++index) {
    const auto error = parse_decimal(state.cells[index], integers[index]);
    if (error != DecimalError::None) {
      return step_failure(decimal_parse_error(error), state,
                          state.current_sheet_row,
                          static_cast<std::uint8_t>(index + 1U));
    }
  }

  const auto position = validate_slk_position(
      integers[0], integers[1], integers[2], integers[3]);
  if (!position) {
    return step_failure(SlkParseError::InvalidPosition, state,
                        state.current_sheet_row, 4U);
  }

  SlkRowType type = SlkRowType::N;
  if (!parse_row_type(state.cells[4], type)) {
    return step_failure(SlkParseError::UnsupportedRowType, state,
                        state.current_sheet_row, 5U);
  }
  std::uint64_t value = 0U;
  const auto value_error = parse_decimal(state.cells[5], value);
  if (value_error != DecimalError::None) {
    return step_failure(decimal_parse_error(value_error), state,
                        state.current_sheet_row, 6U);
  }
  if (!supported_type_value(type, value)) {
    return step_failure(SlkParseError::UnsupportedTypeValue, state,
                        state.current_sheet_row, 6U);
  }

  if (!state.rows.empty() &&
      position.position()->total_sixteenth <=
          state.rows.back().position.total_sixteenth) {
    return step_failure(SlkParseError::NonIncreasingPosition, state,
                        state.current_sheet_row, 4U);
  }
  if (state.boundary_seen) {
    return step_failure(SlkParseError::BoundaryNotLast, state,
                        state.current_sheet_row, 5U);
  }
  if (type == SlkRowType::R) {
    state.boundary_seen = true;
  }

  state.rows.push_back({state.current_sheet_row, *position.position(),
                        type, value});
  state.expected_column = 1U;
  ++state.next_sheet_row;
  return step_success();
}

[[nodiscard]] StepResult process_line(
    ParserState& state, std::string_view line, bool is_last_line) {
  if (state.current_line == 1U) {
    if (!valid_id_record(line)) {
      return step_failure(SlkParseError::InvalidHeader, state);
    }
    return step_success();
  }

  if (line == "E") {
    if (!is_last_line || state.expected_column != 1U) {
      return step_failure(
          is_last_line ? SlkParseError::InvalidCellOrder
                       : SlkParseError::MissingContainerEnd,
          state, state.current_sheet_row, state.expected_column);
    }
    state.container_end_seen = true;
    return step_success();
  }
  if (is_last_line) {
    return step_failure(SlkParseError::MissingContainerEnd, state);
  }

  if (!state.data_started) {
    if (state.schema_index > 0U &&
        state.schema_index < kTypeSchema.size()) {
      if (line != kTypeSchema[state.schema_index]) {
        return step_failure(SlkParseError::InvalidTypeSchema, state);
      }
      ++state.schema_index;
      return step_success();
    }
    if (line == kTypeSchema[0]) {
      state.schema_index = 1U;
      return step_success();
    }
    if (state.schema_index == kTypeSchema.size() &&
        starts_with(line, "C;Y")) {
      state.data_started = true;
    } else {
      return supported_preamble_record(line)
                 ? step_success()
                 : step_failure(SlkParseError::InvalidRecord, state);
    }
  }

  if (state.expected_column == 1U) {
    if (!starts_with(line, "C;")) {
      return starts_with(line, "F;")
                 ? step_success()
                 : step_failure(SlkParseError::InvalidRecord, state,
                                state.next_sheet_row, 1U);
    }
    std::uint64_t sheet_row = 0U;
    std::string_view value;
    if (!parse_first_cell(line, sheet_row, value)) {
      return step_failure(SlkParseError::InvalidRecord, state,
                          state.next_sheet_row, 1U);
    }
    if (sheet_row != state.next_sheet_row) {
      return step_failure(SlkParseError::InvalidSheetRow, state,
                          sheet_row, 1U);
    }
    state.current_sheet_row = sheet_row;
    state.cells[0] = value;
    state.expected_column = 2U;
    return step_success();
  }

  std::string_view value;
  if (!parse_following_cell(line, state.expected_column, value)) {
    return step_failure(SlkParseError::InvalidCellOrder, state,
                        state.current_sheet_row, state.expected_column);
  }
  state.cells[static_cast<std::size_t>(state.expected_column - 1U)] = value;
  if (state.expected_column < 6U) {
    ++state.expected_column;
    return step_success();
  }
  return append_completed_row(state);
}

}  // namespace

SlkParsedRecords::SlkParsedRecords(std::vector<SlkParsedRow> rows) noexcept
    : rows_(std::move(rows)) {}

SlkParseResult::SlkParseResult(SlkParsedRecords records) noexcept
    : storage_(std::move(records)) {}

SlkParseResult::SlkParseResult(SlkParseFailure failure) noexcept
    : storage_(failure) {}

bool SlkParseResult::has_value() const noexcept {
  return std::holds_alternative<SlkParsedRecords>(storage_);
}

const SlkParsedRecords* SlkParseResult::records() const noexcept {
  return std::get_if<SlkParsedRecords>(&storage_);
}

const SlkParseFailure* SlkParseResult::failure() const noexcept {
  return std::get_if<SlkParseFailure>(&storage_);
}

SlkParsedRecords SlkParseResult::take_records() && {
  return std::move(std::get<SlkParsedRecords>(storage_));
}

SlkParseResult parse_slk_records(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept {
  ParserState state;
  if (byte_count == 0U) {
    return SlkParseResult{
        failure_details(SlkParseError::EmptyInput, state)};
  }
  if (bytes == nullptr) {
    return SlkParseResult{
        failure_details(SlkParseError::NullInput, state)};
  }

  try {
    const auto* characters = reinterpret_cast<const char*>(bytes);
    std::size_t line_begin = 0U;
    for (std::size_t index = 0U; index < byte_count; ++index) {
      if (characters[index] == '\n') {
        return SlkParseResult{failure_details(
            SlkParseError::InvalidLineEnding, state)};
      }
      if (characters[index] != '\r') {
        continue;
      }
      if (index + 1U >= byte_count || characters[index + 1U] != '\n') {
        return SlkParseResult{failure_details(
            SlkParseError::InvalidLineEnding, state)};
      }
      ++state.current_line;
      const bool is_last_line = index + 2U == byte_count;
      const std::string_view line{
          characters + line_begin, index - line_begin};
      const auto result = process_line(state, line, is_last_line);
      if (!result.ok) {
        return SlkParseResult{result.failure};
      }
      line_begin = index + 2U;
      ++index;
    }

    if (line_begin != byte_count) {
      return SlkParseResult{failure_details(
          SlkParseError::InvalidLineEnding, state)};
    }
    if (!state.container_end_seen) {
      return SlkParseResult{failure_details(
          SlkParseError::MissingContainerEnd, state)};
    }
    if (state.schema_index != kTypeSchema.size()) {
      return SlkParseResult{failure_details(
          SlkParseError::InvalidTypeSchema, state)};
    }
    if (state.rows.empty()) {
      return SlkParseResult{
          failure_details(SlkParseError::MissingData, state)};
    }
    return SlkParseResult{SlkParsedRecords{std::move(state.rows)}};
  } catch (const std::length_error&) {
    return SlkParseResult{
        failure_details(SlkParseError::InputTooLarge, state)};
  } catch (const std::bad_alloc&) {
    return SlkParseResult{
        failure_details(SlkParseError::AllocationFailure, state)};
  }
}

}  // namespace otgy::core
