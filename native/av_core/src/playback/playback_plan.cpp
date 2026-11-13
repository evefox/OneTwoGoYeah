// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace otgy::core {
namespace {

class StableRandom final {
 public:
  explicit StableRandom(std::uint64_t seed) noexcept : state_(seed) {}

  [[nodiscard]] std::uint64_t next() noexcept {
    state_ += UINT64_C(0x9e3779b97f4a7c15);
    auto value = state_;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
  }

 private:
  std::uint64_t state_;
};

struct SoundPair final {
  PlaybackSoundId npc;
  PlaybackSoundId player;
};

constexpr std::array<SoundPair, 4U> kFourKeySounds{{
    {PlaybackSoundId::M1_2, PlaybackSoundId::F1_2},
    {PlaybackSoundId::M1_4, PlaybackSoundId::F1_4},
    {PlaybackSoundId::M1_6, PlaybackSoundId::F1_6},
    {PlaybackSoundId::M1_8, PlaybackSoundId::F1_8},
}};

constexpr std::array<SoundPair, 8U> kEightKeySounds{{
    {PlaybackSoundId::M1_1, PlaybackSoundId::F1_1},
    {PlaybackSoundId::M1_2, PlaybackSoundId::F1_2},
    {PlaybackSoundId::M1_3, PlaybackSoundId::F1_3},
    {PlaybackSoundId::M1_4, PlaybackSoundId::F1_4},
    {PlaybackSoundId::M1_6, PlaybackSoundId::F1_6},
    {PlaybackSoundId::M1_7, PlaybackSoundId::F1_7},
    {PlaybackSoundId::M1_8, PlaybackSoundId::F1_8},
    {PlaybackSoundId::M1_9, PlaybackSoundId::F1_9},
}};

[[nodiscard]] SoundPair choose_note(StableRandom& random,
                                    PlaybackDanceMode mode) noexcept {
  if (mode == PlaybackDanceMode::FourKey) {
    return kFourKeySounds[static_cast<std::size_t>(random.next() & 3U)];
  }
  return kEightKeySounds[static_cast<std::size_t>(random.next() & 7U)];
}

[[nodiscard]] bool valid_dance_mode(PlaybackDanceMode mode) noexcept {
  return mode == PlaybackDanceMode::FourKey ||
         mode == PlaybackDanceMode::EightKey;
}

using EventLookup =
    std::unordered_map<std::uint64_t, const ChartRoleEvent*>;

}  // namespace

PlaybackPlanError build_playback_plan(
    const ChartDraft& chart, const PlaybackSoundConfig& config,
    PlaybackPlan& out) noexcept {
  out = PlaybackPlan{};
  if (!valid_dance_mode(config.dance_mode)) {
    return PlaybackPlanError::InvalidConfiguration;
  }

  try {
    EventLookup events;
    events.reserve(chart.role_events().size());
    for (const auto& event : chart.role_events()) {
      events.emplace(event.id.value, &event);
    }

    // Reserve the worst case. The exact vectors may stay smaller when
    // unpaired player events are skipped.
    out.audio_triggers.reserve(
        chart.role_events().size() + chart.showtime_start_events().size() +
        chart.showtime_move_events().size());
    out.player_notes.reserve(chart.role_events().size());

    std::size_t order = 0U;
    const auto append_audio = [&out, &order](const ChartRoleEvent& event,
                                             PlaybackSoundId sound,
                                             bool player) {
      out.audio_triggers.push_back({event.row, sound, player, order++});
    };
    const auto append_note = [&out, &order](const ChartRoleEvent& event,
                                            PlaybackSoundId sound) {
      out.player_notes.push_back({event.row, sound, event.id, order++});
    };

    StableRandom random{config.random_seed};
    for (const auto& group : chart.drill_groups()) {
      std::vector<SoundPair> note_choices;
      std::vector<bool> end_choices;
      for (const auto id : group.npc_event_ids) {
        const auto found = events.find(id.value);
        if (found == events.end() || found->second->role != ChartRole::Npc) {
          continue;
        }
        const auto& event = *found->second;
        if (event.type == ChartRoleEventType::BlockStart) {
          if (config.transition_sounds_enabled) {
            append_audio(event, PlaybackSoundId::Turn1, false);
          }
        } else if (event.type == ChartRoleEventType::Note) {
          const auto choice = choose_note(random, config.dance_mode);
          note_choices.push_back(choice);
          append_audio(event, choice.npc, false);
        } else if (event.type == ChartRoleEventType::MoveEnd) {
          const bool yeah = (random.next() & 1U) != 0U;
          end_choices.push_back(yeah);
          append_audio(event, yeah ? PlaybackSoundId::M1Yeah
                                   : PlaybackSoundId::M1Go,
                       false);
        }
      }

      std::size_t note_index = 0U;
      std::size_t end_index = 0U;
      for (const auto id : group.player_event_ids) {
        const auto found = events.find(id.value);
        if (found == events.end() ||
            found->second->role != ChartRole::Player) {
          continue;
        }
        const auto& event = *found->second;
        if (event.type == ChartRoleEventType::BlockStart) {
          if (config.transition_sounds_enabled) {
            append_audio(event, PlaybackSoundId::Turn1, true);
          }
        } else if (event.type == ChartRoleEventType::Note) {
          if (note_index < note_choices.size()) {
            append_note(event, note_choices[note_index].player);
          }
          ++note_index;
        } else if (event.type == ChartRoleEventType::MoveEnd) {
          if (end_index < end_choices.size()) {
            append_note(event, end_choices[end_index]
                                   ? PlaybackSoundId::F1Yeah
                                   : PlaybackSoundId::F1Go);
          }
          ++end_index;
        }
      }
    }

    if (config.transition_sounds_enabled) {
      for (const auto& event : chart.showtime_start_events()) {
        out.audio_triggers.push_back(
            {event.row, PlaybackSoundId::Turn2, false, order++});
      }
    }
    for (const auto& event : chart.showtime_move_events()) {
      out.audio_triggers.push_back(
          {event.row, PlaybackSoundId::SpaceBar, false, order++});
    }
  } catch (const std::bad_alloc&) {
    out = PlaybackPlan{};
    return PlaybackPlanError::AllocationFailure;
  } catch (const std::length_error&) {
    out = PlaybackPlan{};
    return PlaybackPlanError::SizeOverflow;
  }
  return PlaybackPlanError::None;
}

}  // namespace otgy::core
