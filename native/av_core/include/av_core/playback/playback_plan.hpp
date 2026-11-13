// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"
#include "av_core/playback/playback_sound.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace otgy::core {

/// One scheduled preview sound, expressed in exact 1/16 rows. `player` marks
/// sounds owned by the player column (currently PLY-C transition sounds), which
/// simulation playback must suppress while the offline preview keeps them.
struct PlaybackTrigger final {
  std::uint64_t row;
  PlaybackSoundId sound;
  bool player;
  std::size_t order;
};

/// One player event that can be judged. `expected_sound` mirrors the paired
/// NPC choice inside the same drill group; unpaired player events are omitted.
struct PlaybackPlayerNote final {
  std::uint64_t row;
  PlaybackSoundId expected_sound;
  ChartEventId event_id;
  std::size_t order;
};

/// Deterministic playback plan derived from a ChartDraft. The same plan feeds
/// both the offline preview renderer and the real-time simulation engine.
struct PlaybackPlan final {
  std::vector<PlaybackTrigger> audio_triggers;
  std::vector<PlaybackPlayerNote> player_notes;
};

enum class PlaybackPlanError : std::uint8_t {
  None = 0,
  InvalidConfiguration = 1,
  AllocationFailure = 2,
  SizeOverflow = 3,
};

[[nodiscard]] constexpr std::string_view playback_plan_error_message(
    PlaybackPlanError error) noexcept {
  switch (error) {
    case PlaybackPlanError::None:
      return {};
    case PlaybackPlanError::InvalidConfiguration:
      return "playback plan configuration is invalid";
    case PlaybackPlanError::AllocationFailure:
      return "unable to allocate playback plan storage";
    case PlaybackPlanError::SizeOverflow:
      return "playback plan dimensions exceed the representable size";
  }
  return "unknown playback plan error";
}

/// Builds the deterministic trigger and judgment plan for a chart. The input
/// chart and configuration are not modified. `out` is cleared on failure.
[[nodiscard]] PlaybackPlanError build_playback_plan(
    const ChartDraft& chart, const PlaybackSoundConfig& config,
    PlaybackPlan& out) noexcept;

}  // namespace otgy::core
