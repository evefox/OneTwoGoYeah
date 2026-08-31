// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

struct SlkPosition final {
  std::uint64_t measure;
  std::uint64_t beat;
  std::uint64_t sixteenth;
  std::uint64_t total_sixteenth;
};

enum class SlkPositionError : std::uint8_t {
  None = 0,
  BeatOutOfRange = 1,
  SixteenthOutOfRange = 2,
  ArithmeticOverflow = 3,
  InconsistentTotal = 4,
};

[[nodiscard]] constexpr std::string_view slk_position_error_message(
    SlkPositionError error) noexcept {
  switch (error) {
    case SlkPositionError::None:
      return {};
    case SlkPositionError::BeatOutOfRange:
      return "SLK beat must be between 0 and 3";
    case SlkPositionError::SixteenthOutOfRange:
      return "SLK sixteenth must be between 0 and 3";
    case SlkPositionError::ArithmeticOverflow:
      return "SLK total sixteenth position exceeds uint64 range";
    case SlkPositionError::InconsistentTotal:
      return "SLK position columns do not describe the same row";
  }
  return "unknown SLK position error";
}

class SlkPositionResult final {
 public:
  explicit SlkPositionResult(SlkPosition position) noexcept;
  explicit SlkPositionResult(SlkPositionError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const SlkPosition* position() const noexcept;
  [[nodiscard]] SlkPositionError error() const noexcept;

 private:
  std::variant<SlkPosition, SlkPositionError> storage_;
};

/// Builds all four export columns from zero-based measure/beat/sixteenth.
[[nodiscard]] SlkPositionResult compose_slk_position(
    std::uint64_t measure, std::uint64_t beat,
    std::uint64_t sixteenth) noexcept;

/// Decomposes an absolute zero-based 1/16 row into all four columns.
[[nodiscard]] SlkPositionResult decompose_slk_position(
    std::uint64_t total_sixteenth) noexcept;

/// Strictly validates that the first three imported columns equal the fourth.
[[nodiscard]] SlkPositionResult validate_slk_position(
    std::uint64_t measure, std::uint64_t beat, std::uint64_t sixteenth,
    std::uint64_t total_sixteenth) noexcept;

}  // namespace otgy::core
