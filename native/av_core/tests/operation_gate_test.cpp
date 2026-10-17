// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/coordination/operation_gate.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using otgy::core::ApplicationOperationClass;
using otgy::core::ApplicationOperationMode;
using otgy::core::ApplicationOperationResult;

constexpr std::array<ApplicationOperationMode, 4U> kModes{{
    ApplicationOperationMode::Idle,
    ApplicationOperationMode::Playing,
    ApplicationOperationMode::ExportingAudio,
    ApplicationOperationMode::ChartInteraction,
}};

constexpr std::array<ApplicationOperationClass, 7U> kOperations{{
    ApplicationOperationClass::General,
    ApplicationOperationClass::PausePlayback,
    ApplicationOperationClass::ToggleWaveformVisibility,
    ApplicationOperationClass::CancelAudioExport,
    ApplicationOperationClass::SelectionCommand,
    ApplicationOperationClass::NavigationOrView,
    ApplicationOperationClass::HistoryNavigation,
}};

/// The complete truth table, indexed as [mode][operation]. Every cell names the
/// exact result, so a renumbered class or a drifting whitelist fails here.
constexpr std::array<std::array<ApplicationOperationResult, kOperations.size()>,
                     kModes.size()>
    kExpected{{
        // Idle allows every class.
        {{ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed}},
        // Playing only pauses itself or toggles waveform visibility.
        {{ApplicationOperationResult::PlaybackActive,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::PlaybackActive,
          ApplicationOperationResult::PlaybackActive,
          ApplicationOperationResult::PlaybackActive,
          ApplicationOperationResult::PlaybackActive}},
        // ExportingAudio only cancels the export.
        {{ApplicationOperationResult::AudioExportActive,
          ApplicationOperationResult::AudioExportActive,
          ApplicationOperationResult::AudioExportActive,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::AudioExportActive,
          ApplicationOperationResult::AudioExportActive,
          ApplicationOperationResult::AudioExportActive}},
        // A selection or paste-pending session keeps its own commands,
        // navigation and view transforms, history navigation and the waveform
        // visibility toggle. Project/audio mutation and export stay rejected,
        // and PausePlayback remains a defensive escape hatch.
        {{ApplicationOperationResult::ChartInteractionActive,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::ChartInteractionActive,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed,
          ApplicationOperationResult::Allowed}},
    }};

void test_full_truth_table() {
  for (std::size_t mode = 0U; mode < kModes.size(); ++mode) {
    for (std::size_t operation = 0U; operation < kOperations.size();
         ++operation) {
      assert(otgy::core::coordinate_application_operation(kModes[mode],
                                                           kOperations[operation]) ==
             kExpected[mode][operation]);
    }
  }
}

void test_chart_interaction_whitelist() {
  // The session modes reject exactly the blacklist and nothing else.
  for (const auto operation : kOperations) {
    const bool whitelisted =
        operation == ApplicationOperationClass::SelectionCommand ||
        operation == ApplicationOperationClass::NavigationOrView ||
        operation == ApplicationOperationClass::HistoryNavigation ||
        operation == ApplicationOperationClass::ToggleWaveformVisibility ||
        operation == ApplicationOperationClass::PausePlayback;
    const auto expected = whitelisted
                              ? ApplicationOperationResult::Allowed
                              : ApplicationOperationResult::ChartInteractionActive;
    assert(otgy::core::coordinate_application_operation(
               ApplicationOperationMode::ChartInteraction, operation) ==
           expected);
  }
}

void test_invalid_values_and_messages() {
  assert(otgy::core::coordinate_application_operation(
             static_cast<ApplicationOperationMode>(255),
             ApplicationOperationClass::General) ==
         ApplicationOperationResult::InvalidMode);
  // An unknown class keeps reporting InvalidOperation in every mode, including
  // the new one, and regardless of the mode validity.
  for (const auto mode : kModes) {
    assert(otgy::core::coordinate_application_operation(
               mode, static_cast<ApplicationOperationClass>(255)) ==
           ApplicationOperationResult::InvalidOperation);
  }
  assert(otgy::core::coordinate_application_operation(
             static_cast<ApplicationOperationMode>(255),
             static_cast<ApplicationOperationClass>(255)) ==
         ApplicationOperationResult::InvalidOperation);

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  ApplicationOperationResult::ChartInteractionActive);
       ++raw) {
    const auto message = otgy::core::application_operation_result_message(
        static_cast<ApplicationOperationResult>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::application_operation_result_message(
              static_cast<ApplicationOperationResult>(255))
              .empty());
}

}  // namespace

int main() {
  test_full_truth_table();
  test_chart_interaction_whitelist();
  test_invalid_values_and_messages();
  return 0;
}
