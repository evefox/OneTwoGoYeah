// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace otgy::core {

enum class AudioExportNameError : std::uint8_t {
  None = 0,
  InvalidLocalTime = 1,
  InvalidCollisionIndex = 2,
  AllocationFailure = 3,
};

[[nodiscard]] constexpr std::string_view audio_export_name_error_message(
    AudioExportNameError error) noexcept {
  switch (error) {
    case AudioExportNameError::None:
      return {};
    case AudioExportNameError::InvalidLocalTime:
      return "audio export local time is invalid";
    case AudioExportNameError::InvalidCollisionIndex:
      return "audio export collision index must be at least one";
    case AudioExportNameError::AllocationFailure:
      return "unable to allocate audio export name";
  }
  return "unknown audio export name error";
}

struct AudioExportLocalTime final {
  std::uint16_t year;
  std::uint8_t month;
  std::uint8_t day;
  std::uint8_t hour;
  std::uint8_t minute;
  std::uint8_t second;
};

class AudioExportNameResult final {
 public:
  explicit AudioExportNameResult(std::string name) noexcept;
  explicit AudioExportNameResult(AudioExportNameError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::string* name() const noexcept;
  [[nodiscard]] AudioExportNameError error() const noexcept;
  [[nodiscard]] std::string take_name() &&;

 private:
  std::variant<std::string, AudioExportNameError> storage_;
};

/// Applies the shared cross-platform base-name rules to UTF-8 input.
/// Invalid UTF-8 bytes are replaced with underscores; valid Unicode is kept.
[[nodiscard]] AudioExportNameResult normalize_audio_export_base_name(
    std::string_view input) noexcept;

/// Removes the final extension from a source file name, then normalizes it.
[[nodiscard]] AudioExportNameResult default_audio_export_base_name(
    std::string_view source_file_name) noexcept;

/// Builds `<base>_<yyyyMMdd_HHmmss>.ogg` or its `_2`, `_3`, ... variant.
/// `collision_index == 1` means that no collision suffix is appended.
[[nodiscard]] AudioExportNameResult make_audio_export_file_name(
    std::string_view base_name, AudioExportLocalTime local_time,
    std::uint32_t collision_index = 1U) noexcept;

}  // namespace otgy::core
