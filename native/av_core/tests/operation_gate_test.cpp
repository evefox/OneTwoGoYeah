// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/coordination/operation_gate.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using otgy::core::ApplicationOperationClass;
using otgy::core::ApplicationOperationMode;
using otgy::core::ApplicationOperationResult;

constexpr std::array<ApplicationOperationClass, 4U> kOperations{{
    ApplicationOperationClass::General,
    ApplicationOperationClass::PausePlayback,
    ApplicationOperationClass::ToggleWaveformVisibility,
    ApplicationOperationClass::CancelAudioExport,
}};

void test_idle_allows_dispatch() {
  for (const auto operation : kOperations) {
    assert(otgy::core::coordinate_application_operation(
               ApplicationOperationMode::Idle, operation) ==
           ApplicationOperationResult::Allowed);
  }
}

void test_playing_whitelist() {
  for (const auto operation : kOperations) {
    const auto expected =
        operation == ApplicationOperationClass::PausePlayback ||
                operation ==
                    ApplicationOperationClass::ToggleWaveformVisibility
            ? ApplicationOperationResult::Allowed
            : ApplicationOperationResult::PlaybackActive;
    assert(otgy::core::coordinate_application_operation(
               ApplicationOperationMode::Playing, operation) == expected);
  }
}

void test_exporting_whitelist() {
  for (const auto operation : kOperations) {
    const auto expected =
        operation == ApplicationOperationClass::CancelAudioExport
            ? ApplicationOperationResult::Allowed
            : ApplicationOperationResult::AudioExportActive;
    assert(otgy::core::coordinate_application_operation(
               ApplicationOperationMode::ExportingAudio, operation) ==
           expected);
  }
}

void test_invalid_values_and_messages() {
  assert(otgy::core::coordinate_application_operation(
             static_cast<ApplicationOperationMode>(255),
             ApplicationOperationClass::General) ==
         ApplicationOperationResult::InvalidMode);
  for (const auto mode : {ApplicationOperationMode::Idle,
                          ApplicationOperationMode::Playing,
                          ApplicationOperationMode::ExportingAudio}) {
    assert(otgy::core::coordinate_application_operation(
               mode, static_cast<ApplicationOperationClass>(255)) ==
           ApplicationOperationResult::InvalidOperation);
  }

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  ApplicationOperationResult::InvalidOperation);
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
  test_idle_allows_dispatch();
  test_playing_whitelist();
  test_exporting_whitelist();
  test_invalid_values_and_messages();
  return 0;
}
