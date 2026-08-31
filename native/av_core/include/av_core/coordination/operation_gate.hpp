// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace otgy::core {

/// The mutually exclusive global modes that restrict application operations.
enum class ApplicationOperationMode : std::uint8_t {
  Idle = 0,
  Playing = 1,
  ExportingAudio = 2,
};

/// Whitelist classes used by every UI and command entry point.
///
/// General is the safe default for all operations not named here. It includes
/// project/audio mutation, import/export, navigation and view transforms.
enum class ApplicationOperationClass : std::uint8_t {
  General = 0,
  PausePlayback = 1,
  ToggleWaveformVisibility = 2,
  CancelAudioExport = 3,
};

enum class ApplicationOperationResult : std::uint8_t {
  Allowed = 0,
  PlaybackActive = 1,
  AudioExportActive = 2,
  InvalidMode = 3,
  InvalidOperation = 4,
};

[[nodiscard]] constexpr std::string_view application_operation_result_message(
    ApplicationOperationResult result) noexcept {
  switch (result) {
    case ApplicationOperationResult::Allowed:
      return {};
    case ApplicationOperationResult::PlaybackActive:
      return "playback is active";
    case ApplicationOperationResult::AudioExportActive:
      return "audio export is active";
    case ApplicationOperationResult::InvalidMode:
      return "application operation mode is invalid";
    case ApplicationOperationResult::InvalidOperation:
      return "application operation class is invalid";
  }
  return "unknown application operation result";
}

/// Applies the complete Playing/ExportingAudio whitelist without mutating
/// project, playback, export or view state.
[[nodiscard]] ApplicationOperationResult coordinate_application_operation(
    ApplicationOperationMode mode,
    ApplicationOperationClass operation) noexcept;

}  // namespace otgy::core
