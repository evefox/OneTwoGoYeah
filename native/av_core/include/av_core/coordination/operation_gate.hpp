// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace otgy::core {

/// The mutually exclusive global modes that restrict application operations.
///
/// ChartInteraction covers both the chart selection session and the pending
/// paste session: their allowed sets are identical, and which commands are
/// reachable is already decided by the editor's own session preconditions.
enum class ApplicationOperationMode : std::uint8_t {
  Idle = 0,
  Playing = 1,
  ExportingAudio = 2,
  ChartInteraction = 3,
};

/// Whitelist classes used by every UI and command entry point.
///
/// General is the safe default for all operations not named here. It includes
/// project/audio mutation, import/export and playback control.
///
/// NavigationOrView covers timeline movement, Zoom, Scale, page navigation,
/// the song-time jump and the waveform settings panel. ToggleWaveformVisibility
/// is deliberately separate: it stays allowed during playback, while the
/// waveform settings panel does not.
enum class ApplicationOperationClass : std::uint8_t {
  General = 0,
  PausePlayback = 1,
  ToggleWaveformVisibility = 2,
  CancelAudioExport = 3,
  SelectionCommand = 4,
  NavigationOrView = 5,
  HistoryNavigation = 6,
};

enum class ApplicationOperationResult : std::uint8_t {
  Allowed = 0,
  PlaybackActive = 1,
  AudioExportActive = 2,
  InvalidMode = 3,
  InvalidOperation = 4,
  ChartInteractionActive = 5,
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
    case ApplicationOperationResult::ChartInteractionActive:
      return "chart interaction is active";
  }
  return "unknown application operation result";
}

/// Applies the complete mode whitelist without mutating project, playback,
/// export or view state.
[[nodiscard]] ApplicationOperationResult coordinate_application_operation(
    ApplicationOperationMode mode,
    ApplicationOperationClass operation) noexcept;

}  // namespace otgy::core
