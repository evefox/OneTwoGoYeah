// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/coordination/operation_gate.hpp"

namespace otgy::core {
namespace {

[[nodiscard]] bool valid_mode(ApplicationOperationMode mode) noexcept {
  switch (mode) {
    case ApplicationOperationMode::Idle:
    case ApplicationOperationMode::Playing:
    case ApplicationOperationMode::ExportingAudio:
    case ApplicationOperationMode::ChartInteraction:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_operation(
    ApplicationOperationClass operation) noexcept {
  switch (operation) {
    case ApplicationOperationClass::General:
    case ApplicationOperationClass::PausePlayback:
    case ApplicationOperationClass::ToggleWaveformVisibility:
    case ApplicationOperationClass::CancelAudioExport:
    case ApplicationOperationClass::SelectionCommand:
    case ApplicationOperationClass::NavigationOrView:
    case ApplicationOperationClass::HistoryNavigation:
      return true;
  }
  return false;
}

/// The whitelist of each mode, written once so the modes cannot drift apart.
/// Every class not named for a mode is rejected with that mode's result.
[[nodiscard]] bool allowed_in(ApplicationOperationMode mode,
                              ApplicationOperationClass operation) noexcept {
  switch (mode) {
    case ApplicationOperationMode::Idle:
      return true;
    case ApplicationOperationMode::Playing:
      return operation == ApplicationOperationClass::PausePlayback ||
             operation == ApplicationOperationClass::ToggleWaveformVisibility;
    case ApplicationOperationMode::ExportingAudio:
      return operation == ApplicationOperationClass::CancelAudioExport;
    case ApplicationOperationMode::ChartInteraction:
      // A selection or paste-pending session is read-only: only the session's
      // own commands, navigation and view transforms, history navigation and
      // the waveform visibility toggle stay available. PausePlayback is kept
      // as a defensive escape hatch; a session never coexists with playback.
      return operation == ApplicationOperationClass::SelectionCommand ||
             operation == ApplicationOperationClass::NavigationOrView ||
             operation == ApplicationOperationClass::HistoryNavigation ||
             operation ==
                 ApplicationOperationClass::ToggleWaveformVisibility ||
             operation == ApplicationOperationClass::PausePlayback;
  }
  return false;
}

[[nodiscard]] ApplicationOperationResult rejection_for(
    ApplicationOperationMode mode) noexcept {
  switch (mode) {
    case ApplicationOperationMode::Playing:
      return ApplicationOperationResult::PlaybackActive;
    case ApplicationOperationMode::ExportingAudio:
      return ApplicationOperationResult::AudioExportActive;
    case ApplicationOperationMode::ChartInteraction:
      return ApplicationOperationResult::ChartInteractionActive;
    case ApplicationOperationMode::Idle:
      break;
  }
  return ApplicationOperationResult::InvalidMode;
}

}  // namespace

ApplicationOperationResult coordinate_application_operation(
    ApplicationOperationMode mode,
    ApplicationOperationClass operation) noexcept {
  // The operation class is validated first so an unknown class keeps reporting
  // InvalidOperation regardless of the mode, as it always has.
  if (!valid_operation(operation)) {
    return ApplicationOperationResult::InvalidOperation;
  }
  if (!valid_mode(mode)) {
    return ApplicationOperationResult::InvalidMode;
  }
  return allowed_in(mode, operation) ? ApplicationOperationResult::Allowed
                                     : rejection_for(mode);
}

}  // namespace otgy::core
