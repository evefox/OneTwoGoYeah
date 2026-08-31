// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/coordination/operation_gate.hpp"

namespace otgy::core {
namespace {

[[nodiscard]] bool valid_operation(
    ApplicationOperationClass operation) noexcept {
  return operation == ApplicationOperationClass::General ||
         operation == ApplicationOperationClass::PausePlayback ||
         operation == ApplicationOperationClass::ToggleWaveformVisibility ||
         operation == ApplicationOperationClass::CancelAudioExport;
}

}  // namespace

ApplicationOperationResult coordinate_application_operation(
    ApplicationOperationMode mode,
    ApplicationOperationClass operation) noexcept {
  if (!valid_operation(operation)) {
    return ApplicationOperationResult::InvalidOperation;
  }

  switch (mode) {
    case ApplicationOperationMode::Idle:
      return ApplicationOperationResult::Allowed;
    case ApplicationOperationMode::Playing:
      return operation == ApplicationOperationClass::PausePlayback ||
                     operation ==
                         ApplicationOperationClass::ToggleWaveformVisibility
                 ? ApplicationOperationResult::Allowed
                 : ApplicationOperationResult::PlaybackActive;
    case ApplicationOperationMode::ExportingAudio:
      return operation == ApplicationOperationClass::CancelAudioExport
                 ? ApplicationOperationResult::Allowed
                 : ApplicationOperationResult::AudioExportActive;
  }
  return ApplicationOperationResult::InvalidMode;
}

}  // namespace otgy::core
