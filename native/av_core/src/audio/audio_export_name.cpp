// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export_name.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

struct Utf8CodePoint final {
  std::uint32_t value;
  std::size_t byte_count;
  bool valid;
};

[[nodiscard]] bool is_continuation(unsigned char byte) noexcept {
  return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] Utf8CodePoint decode_utf8(std::string_view input,
                                        std::size_t offset) noexcept {
  const auto first = static_cast<unsigned char>(input[offset]);
  if (first < 0x80U) {
    return {first, 1U, true};
  }
  if (first >= 0xC2U && first <= 0xDFU && offset + 1U < input.size()) {
    const auto second = static_cast<unsigned char>(input[offset + 1U]);
    if (is_continuation(second)) {
      return {static_cast<std::uint32_t>(first & 0x1FU) << 6U |
                  static_cast<std::uint32_t>(second & 0x3FU),
              2U, true};
    }
  }
  if (first >= 0xE0U && first <= 0xEFU && offset + 2U < input.size()) {
    const auto second = static_cast<unsigned char>(input[offset + 1U]);
    const auto third = static_cast<unsigned char>(input[offset + 2U]);
    const bool second_valid =
        is_continuation(second) &&
        (first != 0xE0U || second >= 0xA0U) &&
        (first != 0xEDU || second <= 0x9FU);
    if (second_valid && is_continuation(third)) {
      return {static_cast<std::uint32_t>(first & 0x0FU) << 12U |
                  static_cast<std::uint32_t>(second & 0x3FU) << 6U |
                  static_cast<std::uint32_t>(third & 0x3FU),
              3U, true};
    }
  }
  if (first >= 0xF0U && first <= 0xF4U && offset + 3U < input.size()) {
    const auto second = static_cast<unsigned char>(input[offset + 1U]);
    const auto third = static_cast<unsigned char>(input[offset + 2U]);
    const auto fourth = static_cast<unsigned char>(input[offset + 3U]);
    const bool second_valid =
        is_continuation(second) &&
        (first != 0xF0U || second >= 0x90U) &&
        (first != 0xF4U || second <= 0x8FU);
    if (second_valid && is_continuation(third) &&
        is_continuation(fourth)) {
      return {static_cast<std::uint32_t>(first & 0x07U) << 18U |
                  static_cast<std::uint32_t>(second & 0x3FU) << 12U |
                  static_cast<std::uint32_t>(third & 0x3FU) << 6U |
                  static_cast<std::uint32_t>(fourth & 0x3FU),
              4U, true};
    }
  }
  return {0U, 1U, false};
}

[[nodiscard]] bool is_unicode_whitespace(std::uint32_t value) noexcept {
  return value == 0x0009U || value == 0x000AU || value == 0x000BU ||
         value == 0x000CU || value == 0x000DU || value == 0x0020U ||
         value == 0x0085U || value == 0x00A0U || value == 0x1680U ||
         (value >= 0x2000U && value <= 0x200AU) || value == 0x2028U ||
         value == 0x2029U || value == 0x202FU || value == 0x205FU ||
         value == 0x3000U;
}

[[nodiscard]] bool is_control(std::uint32_t value) noexcept {
  return value <= 0x001FU || (value >= 0x007FU && value <= 0x009FU);
}

[[nodiscard]] bool is_forbidden_ascii(std::uint32_t value) noexcept {
  switch (value) {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] constexpr char ascii_upper(char value) noexcept {
  return value >= 'a' && value <= 'z'
             ? static_cast<char>(value - 'a' + 'A')
             : value;
}

[[nodiscard]] bool ascii_case_equal(std::string_view left,
                                    std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (ascii_upper(left[index]) != ascii_upper(right[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_reserved_name(std::string_view name) noexcept {
  if (ascii_case_equal(name, "CON") || ascii_case_equal(name, "PRN") ||
      ascii_case_equal(name, "AUX") || ascii_case_equal(name, "NUL")) {
    return true;
  }
  if (name.size() != 4U || (ascii_upper(name[3U]) < '1' ||
                            ascii_upper(name[3U]) > '9')) {
    return false;
  }
  const auto prefix = name.substr(0U, 3U);
  return ascii_case_equal(prefix, "COM") || ascii_case_equal(prefix, "LPT");
}

[[nodiscard]] bool is_leap_year(std::uint16_t year) noexcept {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

[[nodiscard]] bool valid_local_time(AudioExportLocalTime value) noexcept {
  static constexpr std::array<std::uint8_t, 12> kDaysPerMonth{
      31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
  if (value.month < 1U || value.month > 12U || value.hour > 23U ||
      value.minute > 59U || value.second > 59U) {
    return false;
  }
  auto maximum_day = kDaysPerMonth[value.month - 1U];
  if (value.month == 2U && is_leap_year(value.year)) {
    maximum_day = 29U;
  }
  return value.day >= 1U && value.day <= maximum_day;
}

void append_two_digits(std::string& output, std::uint8_t value) {
  output.push_back(static_cast<char>('0' + value / 10U));
  output.push_back(static_cast<char>('0' + value % 10U));
}

void append_four_digits(std::string& output, std::uint16_t value) {
  output.push_back(static_cast<char>('0' + value / 1000U));
  output.push_back(static_cast<char>('0' + value / 100U % 10U));
  output.push_back(static_cast<char>('0' + value / 10U % 10U));
  output.push_back(static_cast<char>('0' + value % 10U));
}

}  // namespace

AudioExportNameResult::AudioExportNameResult(std::string name) noexcept
    : storage_(std::move(name)) {}

AudioExportNameResult::AudioExportNameResult(
    AudioExportNameError error) noexcept
    : storage_(error) {}

AudioExportNameResult::operator bool() const noexcept {
  return std::holds_alternative<std::string>(storage_);
}

const std::string* AudioExportNameResult::name() const noexcept {
  return std::get_if<std::string>(&storage_);
}

AudioExportNameError AudioExportNameResult::error() const noexcept {
  const auto* error = std::get_if<AudioExportNameError>(&storage_);
  return error == nullptr ? AudioExportNameError::None : *error;
}

std::string AudioExportNameResult::take_name() && {
  return std::move(std::get<std::string>(storage_));
}

AudioExportNameResult normalize_audio_export_base_name(
    std::string_view input) noexcept {
  try {
    std::string normalized;
    normalized.reserve(input.size());
    bool started = false;
    for (std::size_t offset = 0U; offset < input.size();) {
      const auto code_point = decode_utf8(input, offset);
      if (!code_point.valid) {
        normalized.push_back('_');
        started = true;
        offset += code_point.byte_count;
        continue;
      }
      if (!started && is_unicode_whitespace(code_point.value)) {
        offset += code_point.byte_count;
        continue;
      }
      started = true;
      if (is_control(code_point.value) ||
          is_forbidden_ascii(code_point.value)) {
        normalized.push_back('_');
      } else {
        normalized.append(input.substr(offset, code_point.byte_count));
      }
      offset += code_point.byte_count;
    }

    std::size_t keep_end = 0U;
    for (std::size_t offset = 0U; offset < normalized.size();) {
      const auto code_point = decode_utf8(normalized, offset);
      const auto next = offset + code_point.byte_count;
      if (!is_unicode_whitespace(code_point.value) && code_point.value != '.') {
        keep_end = next;
      }
      offset = next;
    }
    normalized.resize(keep_end);
    if (normalized.empty()) {
      normalized = "audio";
    } else if (is_reserved_name(normalized)) {
      normalized.insert(normalized.begin(), '_');
    }
    return AudioExportNameResult{std::move(normalized)};
  } catch (const std::bad_alloc&) {
    return AudioExportNameResult{AudioExportNameError::AllocationFailure};
  }
}

AudioExportNameResult default_audio_export_base_name(
    std::string_view source_file_name) noexcept {
  const auto separator = source_file_name.find_last_of("/\\");
  auto file_name = separator == std::string_view::npos
                       ? source_file_name
                       : source_file_name.substr(separator + 1U);
  const auto extension = file_name.find_last_of('.');
  if (extension != std::string_view::npos) {
    file_name = file_name.substr(0U, extension);
  }
  return normalize_audio_export_base_name(file_name);
}

AudioExportNameResult make_audio_export_file_name(
    std::string_view base_name, AudioExportLocalTime local_time,
    std::uint32_t collision_index) noexcept {
  if (!valid_local_time(local_time)) {
    return AudioExportNameResult{AudioExportNameError::InvalidLocalTime};
  }
  if (collision_index == 0U) {
    return AudioExportNameResult{
        AudioExportNameError::InvalidCollisionIndex};
  }
  auto normalized = normalize_audio_export_base_name(base_name);
  if (!normalized) {
    return normalized;
  }
  try {
    std::string output = std::move(normalized).take_name();
    output.reserve(output.size() + 32U);
    output.push_back('_');
    append_four_digits(output, local_time.year);
    append_two_digits(output, local_time.month);
    append_two_digits(output, local_time.day);
    output.push_back('_');
    append_two_digits(output, local_time.hour);
    append_two_digits(output, local_time.minute);
    append_two_digits(output, local_time.second);
    if (collision_index > 1U) {
      output.push_back('_');
      std::array<char, 10> digits{};
      const auto converted = std::to_chars(
          digits.data(), digits.data() + digits.size(), collision_index);
      output.append(digits.data(), converted.ptr);
    }
    output.append(".ogg");
    return AudioExportNameResult{std::move(output)};
  } catch (const std::bad_alloc&) {
    return AudioExportNameResult{AudioExportNameError::AllocationFailure};
  }
}

}  // namespace otgy::core
