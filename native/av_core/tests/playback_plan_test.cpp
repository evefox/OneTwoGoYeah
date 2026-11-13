// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_plan.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using otgy::core::ChartDanceMoveDrill;
using otgy::core::ChartDanceMoveDrillId;
using otgy::core::ChartDraft;
using otgy::core::ChartDrillGroup;
using otgy::core::ChartDrillGroupId;
using otgy::core::ChartEventId;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::ChartShowtime;
using otgy::core::ChartShowtimeId;
using otgy::core::ChartShowtimeMoveEvent;
using otgy::core::ChartShowtimeStartEvent;
using otgy::core::PlaybackDanceMode;
using otgy::core::PlaybackPlan;
using otgy::core::PlaybackPlanError;
using otgy::core::PlaybackSoundConfig;
using otgy::core::PlaybackSoundId;
using otgy::core::PlaybackSoundLanguage;

ChartRoleEvent role_event(std::uint64_t id, std::uint64_t row,
                          ChartRole role, ChartRoleEventType type,
                          std::uint64_t group_id = 100U) {
  return ChartRoleEvent{ChartEventId{id}, row, role, type,
                        ChartDrillGroupId{group_id},
                        ChartDanceMoveDrillId{200U}};
}

ChartDrillGroup group(std::vector<ChartEventId> npc,
                      std::vector<ChartEventId> player) {
  return ChartDrillGroup{ChartDrillGroupId{100U},
                         ChartDanceMoveDrillId{200U},
                         std::move(npc), std::move(player)};
}

PlaybackSoundConfig sound_config(PlaybackDanceMode mode,
                                 bool transition_sounds = true) {
  return PlaybackSoundConfig{PlaybackSoundLanguage::Chinese, mode,
                             transition_sounds, 42U, {160.0, 0.0}};
}

PlaybackSoundId player_sound_of(PlaybackSoundId npc) {
  switch (npc) {
    case PlaybackSoundId::M1_1:
      return PlaybackSoundId::F1_1;
    case PlaybackSoundId::M1_2:
      return PlaybackSoundId::F1_2;
    case PlaybackSoundId::M1_3:
      return PlaybackSoundId::F1_3;
    case PlaybackSoundId::M1_4:
      return PlaybackSoundId::F1_4;
    case PlaybackSoundId::M1_6:
      return PlaybackSoundId::F1_6;
    case PlaybackSoundId::M1_7:
      return PlaybackSoundId::F1_7;
    case PlaybackSoundId::M1_8:
      return PlaybackSoundId::F1_8;
    case PlaybackSoundId::M1_9:
      return PlaybackSoundId::F1_9;
    case PlaybackSoundId::M1Go:
      return PlaybackSoundId::F1Go;
    case PlaybackSoundId::M1Yeah:
      return PlaybackSoundId::F1Yeah;
    default:
      assert(false);
      return npc;
  }
}

std::vector<ChartRoleEvent> paired_role_events() {
  return {
      role_event(1U, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart),
      role_event(2U, 4U, ChartRole::Npc, ChartRoleEventType::Note),
      role_event(3U, 8U, ChartRole::Npc, ChartRoleEventType::Note),
      role_event(4U, 12U, ChartRole::Npc, ChartRoleEventType::MoveEnd),
      role_event(5U, 16U, ChartRole::Player, ChartRoleEventType::BlockStart),
      role_event(6U, 20U, ChartRole::Player, ChartRoleEventType::Note),
      role_event(7U, 24U, ChartRole::Player, ChartRoleEventType::Note),
      role_event(8U, 28U, ChartRole::Player, ChartRoleEventType::MoveEnd),
  };
}

ChartDraft paired_chart() {
  return ChartDraft(paired_role_events(), {}, {},
                    {group({ChartEventId{1U}, ChartEventId{2U},
                            ChartEventId{3U}, ChartEventId{4U}},
                           {ChartEventId{5U}, ChartEventId{6U},
                            ChartEventId{7U}, ChartEventId{8U}})},
                    {}, {});
}

void test_role_plan_and_pairing() {
  const auto chart = paired_chart();
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart, sound_config(PlaybackDanceMode::EightKey), plan) ==
         PlaybackPlanError::None);

  assert(plan.audio_triggers.size() == 5U);
  assert(plan.player_notes.size() == 3U);

  assert(plan.audio_triggers[0].row == 0U);
  assert(plan.audio_triggers[0].sound == PlaybackSoundId::Turn1);
  assert(!plan.audio_triggers[0].player);
  assert(plan.audio_triggers[4].row == 16U);
  assert(plan.audio_triggers[4].sound == PlaybackSoundId::Turn1);
  assert(plan.audio_triggers[4].player);

  assert(plan.player_notes[0].row == 20U);
  assert(plan.player_notes[0].event_id.value == 6U);
  assert(plan.player_notes[0].expected_sound ==
         player_sound_of(plan.audio_triggers[1].sound));
  assert(plan.player_notes[1].row == 24U);
  assert(plan.player_notes[1].event_id.value == 7U);
  assert(plan.player_notes[1].expected_sound ==
         player_sound_of(plan.audio_triggers[2].sound));
  assert(plan.player_notes[2].row == 28U);
  assert(plan.player_notes[2].event_id.value == 8U);
  assert(plan.player_notes[2].expected_sound ==
         player_sound_of(plan.audio_triggers[3].sound));

  std::vector<std::size_t> orders;
  orders.reserve(plan.audio_triggers.size() + plan.player_notes.size());
  for (const auto& trigger : plan.audio_triggers) {
    orders.push_back(trigger.order);
  }
  for (const auto& note : plan.player_notes) {
    orders.push_back(note.order);
  }
  std::sort(orders.begin(), orders.end());
  for (std::size_t index = 0U; index < orders.size(); ++index) {
    assert(orders[index] == index);
  }
}

void test_transition_switch() {
  const auto chart = paired_chart();
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart,
             sound_config(PlaybackDanceMode::EightKey, false), plan) ==
         PlaybackPlanError::None);
  assert(plan.audio_triggers.size() == 3U);
  for (const auto& trigger : plan.audio_triggers) {
    assert(trigger.sound != PlaybackSoundId::Turn1);
  }
  assert(plan.player_notes.size() == 3U);
}

void test_unpaired_player_notes_skipped() {
  ChartDraft chart(
      {role_event(1U, 4U, ChartRole::Npc, ChartRoleEventType::Note),
       role_event(2U, 20U, ChartRole::Player, ChartRoleEventType::Note),
       role_event(3U, 24U, ChartRole::Player, ChartRoleEventType::Note)},
      {}, {},
      {group({ChartEventId{1U}},
             {ChartEventId{2U}, ChartEventId{3U}})},
      {}, {});
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart,
             sound_config(PlaybackDanceMode::EightKey, false), plan) ==
         PlaybackPlanError::None);
  assert(plan.audio_triggers.size() == 1U);
  assert(plan.player_notes.size() == 1U);
  assert(plan.player_notes[0].event_id.value == 2U);
  assert(plan.player_notes[0].expected_sound ==
         player_sound_of(plan.audio_triggers[0].sound));
}

void test_four_key_sounds() {
  const auto chart = paired_chart();
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart, sound_config(PlaybackDanceMode::FourKey), plan) ==
         PlaybackPlanError::None);
  for (const auto& trigger : plan.audio_triggers) {
    if (trigger.sound == PlaybackSoundId::M1_1 ||
        trigger.sound == PlaybackSoundId::M1_3 ||
        trigger.sound == PlaybackSoundId::M1_7 ||
        trigger.sound == PlaybackSoundId::M1_9) {
      assert(false);
    }
  }
  for (const auto& note : plan.player_notes) {
    if (note.expected_sound == PlaybackSoundId::F1_1 ||
        note.expected_sound == PlaybackSoundId::F1_3 ||
        note.expected_sound == PlaybackSoundId::F1_7 ||
        note.expected_sound == PlaybackSoundId::F1_9) {
      assert(false);
    }
  }
}

void test_showtime_triggers() {
  ChartDraft chart(
      {}, {ChartShowtimeStartEvent{ChartEventId{9U}, 32U,
                                   ChartShowtimeId{300U}}},
      {ChartShowtimeMoveEvent{ChartEventId{10U}, 36U,
                              ChartShowtimeId{300U},
                              ChartDanceMoveDrillId{200U}, 4U}},
      {}, {}, {});
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart, sound_config(PlaybackDanceMode::EightKey), plan) ==
         PlaybackPlanError::None);
  assert(plan.audio_triggers.size() == 2U);
  assert(plan.audio_triggers[0].row == 32U);
  assert(plan.audio_triggers[0].sound == PlaybackSoundId::Turn2);
  assert(!plan.audio_triggers[0].player);
  assert(plan.audio_triggers[1].row == 36U);
  assert(plan.audio_triggers[1].sound == PlaybackSoundId::SpaceBar);
  assert(plan.player_notes.empty());
}

void test_invalid_dance_mode() {
  const auto chart = paired_chart();
  auto config = sound_config(PlaybackDanceMode::EightKey);
  config.dance_mode = static_cast<PlaybackDanceMode>(9U);
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(chart, config, plan) ==
         PlaybackPlanError::InvalidConfiguration);
  assert(plan.audio_triggers.empty());
  assert(plan.player_notes.empty());
}

}  // namespace

int main() {
  test_role_plan_and_pairing();
  test_transition_switch();
  test_unpaired_player_notes_skipped();
  test_four_key_sounds();
  test_showtime_triggers();
  test_invalid_dance_mode();
  return 0;
}
