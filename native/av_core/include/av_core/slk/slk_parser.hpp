// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/slk/slk_position.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class SlkRowType : std::uint8_t {
  N = 0,
  S = 1,
  C = 2,
  D = 3,
  R = 4,
};

struct SlkParsedRow final {
  std::uint64_t sheet_row;
  SlkPosition position;
  SlkRowType type;
  std::uint64_t value;
};

class SlkParsedRecords final {
 public:
  explicit SlkParsedRecords(std::vector<SlkParsedRow> rows) noexcept;

  SlkParsedRecords(const SlkParsedRecords&) = default;
  SlkParsedRecords& operator=(const SlkParsedRecords&) = default;
  SlkParsedRecords(SlkParsedRecords&&) noexcept = default;
  SlkParsedRecords& operator=(SlkParsedRecords&&) noexcept = default;
  ~SlkParsedRecords() = default;

  [[nodiscard]] const std::vector<SlkParsedRow>& rows() const noexcept {
    return rows_;
  }

 private:
  std::vector<SlkParsedRow> rows_;
};

enum class SlkParseError : std::uint8_t {
  None = 0,
  EmptyInput = 1,
  NullInput = 2,
  InvalidLineEnding = 3,
  InvalidHeader = 4,
  InvalidTypeSchema = 5,
  InvalidRecord = 6,
  InvalidCellOrder = 7,
  InvalidSheetRow = 8,
  InvalidInteger = 9,
  IntegerOverflow = 10,
  InvalidPosition = 11,
  UnsupportedRowType = 12,
  UnsupportedTypeValue = 13,
  NonIncreasingPosition = 14,
  MissingData = 15,
  MissingBoundary = 16,
  BoundaryNotLast = 17,
  MissingContainerEnd = 18,
  InputTooLarge = 19,
  AllocationFailure = 20,
};

[[nodiscard]] constexpr std::string_view slk_parse_error_message(
    SlkParseError error) noexcept {
  switch (error) {
    case SlkParseError::None:
      return {};
    case SlkParseError::EmptyInput:
      return "SLK input is empty";
    case SlkParseError::NullInput:
      return "SLK input bytes must not be null";
    case SlkParseError::InvalidLineEnding:
      return "SLK records must use CRLF line endings";
    case SlkParseError::InvalidHeader:
      return "SLK header is not supported";
    case SlkParseError::InvalidTypeSchema:
      return "SLK six-column type schema is invalid";
    case SlkParseError::InvalidRecord:
      return "SLK record syntax is invalid";
    case SlkParseError::InvalidCellOrder:
      return "SLK data cells are not in six-column order";
    case SlkParseError::InvalidSheetRow:
      return "SLK data sheet rows must be continuous from row 3";
    case SlkParseError::InvalidInteger:
      return "SLK integer cell must contain unsigned decimal ASCII";
    case SlkParseError::IntegerOverflow:
      return "SLK integer cell exceeds uint64 range";
    case SlkParseError::InvalidPosition:
      return "SLK position columns are invalid or inconsistent";
    case SlkParseError::UnsupportedRowType:
      return "SLK row type is not supported";
    case SlkParseError::UnsupportedTypeValue:
      return "SLK row type and value combination is not supported";
    case SlkParseError::NonIncreasingPosition:
      return "SLK data positions must be strictly increasing";
    case SlkParseError::MissingData:
      return "SLK contains no data rows";
    case SlkParseError::MissingBoundary:
      return "SLK is missing its final r boundary row";
    case SlkParseError::BoundaryNotLast:
      return "SLK r boundary must be the final data row";
    case SlkParseError::MissingContainerEnd:
      return "SLK container must end with E followed by CRLF";
    case SlkParseError::InputTooLarge:
      return "SLK input dimensions exceed parser limits";
    case SlkParseError::AllocationFailure:
      return "unable to allocate parsed SLK rows";
  }
  return "unknown SLK parse error";
}

struct SlkParseFailure final {
  SlkParseError error;
  std::uint64_t line_number;
  std::uint64_t sheet_row;
  std::uint8_t column;
};

class SlkParseResult final {
 public:
  explicit SlkParseResult(SlkParsedRecords records) noexcept;
  explicit SlkParseResult(SlkParseFailure failure) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SlkParsedRecords* records() const noexcept;
  [[nodiscard]] const SlkParseFailure* failure() const noexcept;

  [[nodiscard]] SlkParsedRecords take_records() &&;

 private:
  std::variant<SlkParsedRecords, SlkParseFailure> storage_;
};

/// Parses supported Excel SYLK bytes without decoding non-ASCII template text.
[[nodiscard]] SlkParseResult parse_slk_records(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept;

}  // namespace otgy::core
