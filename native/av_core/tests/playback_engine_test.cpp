// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_engine.hpp"

#include "av_core/playback/playback_pcm.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartDanceMoveDrillId;
using otgy::core::ChartDraft;
using otgy::core::ChartDrillGroup;
using otgy::core::ChartDrillGroupId;
using otgy::core::ChartEventId;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::JudgementConfig;
using otgy::core::JudgementGrade;
using otgy::core::JudgementResult;
using otgy::core::JudgementWindowBasis;
using otgy::core::Pcm16;
using otgy::core::PlaybackDanceMode;
using otgy::core::PlaybackEngine;
using otgy::core::PlaybackEngineConfig;
using otgy::core::PlaybackEngineError;
using otgy::core::PlaybackPlan;
using otgy::core::PlaybackSoundBank;
using otgy::core::PlaybackSoundClip;
using otgy::core::PlaybackSoundId;
using otgy::core::PlaybackSoundLanguage;

constexpr std::uint32_t kRate = 8000U;

Pcm16 make_song(std::size_t frames) {
  auto result = Pcm16::allocate(kRate, frames);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  auto* left = pcm.mutable_channel_data(otgy::core::PcmChannel::Left);
  auto* right = pcm.mutable_channel_data(otgy::core::PcmChannel::Right);
  for (std::size_t index = 0U; index < frames; ++index) {
    left[index] = static_cast<std::int16_t>(index % 50U);
    right[index] = static_cast<std::int16_t>(-static_cast<int>(index % 50U));
  }
  return pcm;
}

constexpr std::int16_t kMissSample = 77;

PlaybackSoundClip make_clip(PlaybackSoundId id, std::int16_t value,
                            std::size_t frames = 4U) {
  auto result = Pcm16::allocate(kRate, frames);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  auto* left = pcm.mutable_channel_data(otgy::core::PcmChannel::Left);
  auto* right = pcm.mutable_channel_data(otgy::core::PcmChannel::Right);
  for (std::size_t index = 0U; index < frames; ++index) {
    left[index] = value;
    right[index] = value;
  }
  return PlaybackSoundClip{PlaybackSoundLanguage::Chinese, id,
                           std::move(pcm)};
}

PlaybackSoundBank all_note_bank(std::int16_t value) {
  std::vector<PlaybackSoundClip> clips;
  const PlaybackSoundId m1[] = {
      PlaybackSoundId::M1_1, PlaybackSoundId::M1_2, PlaybackSoundId::M1_3,
      PlaybackSoundId::M1_4, PlaybackSoundId::M1_6, PlaybackSoundId::M1_7,
      PlaybackSoundId::M1_8, PlaybackSoundId::M1_9,
  };
  for (const auto id : m1) {
    clips.push_back(make_clip(id, value));
  }
  const PlaybackSoundId f1[] = {
      PlaybackSoundId::F1_1, PlaybackSoundId::F1_2, PlaybackSoundId::F1_3,
      PlaybackSoundId::F1_4, PlaybackSoundId::F1_6, PlaybackSoundId::F1_7,
      PlaybackSoundId::F1_8, PlaybackSoundId::F1_9,
  };
  for (const auto id : f1) {
    clips.push_back(make_clip(id, value));
  }
  clips.push_back(make_clip(PlaybackSoundId::Miss, kMissSample));
  return PlaybackSoundBank(std::move(clips));
}

ChartRoleEvent role_event(std::uint64_t id, std::uint64_t row,
                          ChartRole role, ChartRoleEventType type) {
  return ChartRoleEvent{ChartEventId{id}, row, role, type,
                        ChartDrillGroupId{10U},
                        ChartDanceMoveDrillId{20U}};
}

ChartDrillGroup group(std::vector<ChartEventId> npc,
                      std::vector<ChartEventId> player) {
  return ChartDrillGroup{ChartDrillGroupId{10U},
                         ChartDanceMoveDrillId{20U},
                         std::move(npc), std::move(player)};
}

ChartDraft npc_chart(std::uint64_t row) {
  return ChartDraft(
      {role_event(1U, row, ChartRole::Npc, ChartRoleEventType::Note)}, {},
      {}, {group({ChartEventId{1U}}, {})}, {}, {});
}

ChartDraft pair_chart(std::uint64_t row) {
  return ChartDraft(
      {role_event(1U, row, ChartRole::Npc, ChartRoleEventType::Note),
       role_event(2U, row, ChartRole::Player, ChartRoleEventType::Note)},
      {}, {}, {group({ChartEventId{1U}}, {ChartEventId{2U}})}, {}, {});
}

JudgementConfig wide_judgement(double bpm) {
  auto config = otgy::core::default_judgement_config({bpm, 0.0});
  config.basis = JudgementWindowBasis::HalfBeat;
  config.perfect_fraction = 0.5;
  config.great_fraction = 0.6;
  config.cool_fraction = 0.8;
  config.bad_fraction = 1.0;
  return config;
}

PlaybackEngineConfig engine_config(double bpm, double start_seconds,
                                   double end_seconds,
                                   JudgementConfig judgement) {
  return PlaybackEngineConfig{PlaybackSoundLanguage::Chinese,
                              PlaybackDanceMode::EightKey,
                              false,
                              42U,
                              {bpm, 0.0},
                              start_seconds,
                              end_seconds,
                              judgement};
}

std::vector<std::uint8_t> render(PlaybackEngine& engine,
                                 std::uint32_t frames) {
  std::vector<std::uint8_t> output(static_cast<std::size_t>(frames) * 4U);
  engine.render(output.data(), frames);
  return output;
}

std::int16_t sample(const std::vector<std::uint8_t>& bytes,
                    std::size_t frame, std::size_t channel) {
  const auto offset = frame * 4U + channel * 2U;
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
  return static_cast<std::int16_t>(bits);
}

void test_song_only_matches_offline() {
  const auto song = make_song(100U);
  PlaybackSoundBank bank(std::vector<PlaybackSoundClip>{});
  const auto bpm = 160.0;
  const auto end = 100.0 / static_cast<double>(kRate);
  PlaybackEngine engine;
  assert(engine.initialize(song, ChartDraft({}, {}, {}, {}, {}, {}), bank,
                           engine_config(bpm, 0.0, end,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);

  auto reference = otgy::core::render_playback_pcm(song, 0.0);
  assert(reference);
  auto reference_pcm = std::move(reference).take_pcm();
  const auto expected = std::move(reference_pcm).take_bytes();

  std::vector<std::uint8_t> actual(100U * 4U);
  std::size_t frame = 0U;
  while (frame < 100U) {
    const auto block = static_cast<std::uint32_t>(
        std::min<std::size_t>(7U, 100U - frame));
    const auto bytes = render(engine, block);
    std::copy(bytes.begin(), bytes.end(),
              actual.begin() + static_cast<std::ptrdiff_t>(frame) * 4);
    frame += block;
  }
  assert(actual == expected);
  const auto state = engine.state();
  assert(state.finished);
  assert(state.rendered_frames == 100U);
}

void test_scheduled_npc_clip_and_voice_tail() {
  const auto song = make_song(1000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  PlaybackEngine engine;
  assert(engine.initialize(song, npc_chart(0U), bank,
                           engine_config(bpm, 0.0,
                                         1000.0 / static_cast<double>(kRate),
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);

  const auto first = render(engine, 4U);
  assert(sample(first, 0U, 0U) == 100);
  assert(sample(first, 0U, 1U) == 100);
  assert(sample(first, 1U, 0U) == 101);
  assert(sample(first, 1U, 1U) == 99);
  assert(sample(first, 3U, 0U) == 103);
  assert(sample(first, 3U, 1U) == 97);

  const auto second = render(engine, 4U);
  assert(sample(second, 0U, 0U) == 4);
  assert(sample(second, 0U, 1U) == -4);
  assert(sample(second, 1U, 0U) == 5);
  assert(sample(second, 1U, 1U) == -5);
}

void test_key_trigger_judges_and_plays() {
  const auto song = make_song(8000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  const auto plan_chart = pair_chart(0U);
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             plan_chart,
             {PlaybackSoundLanguage::Chinese, PlaybackDanceMode::EightKey,
              false, 42U, {bpm, 0.0}},
             plan) == otgy::core::PlaybackPlanError::None);
  assert(plan.player_notes.size() == 1U);
  const auto expected = plan.player_notes[0].expected_sound;

  PlaybackEngine engine;
  assert(engine.initialize(song, plan_chart, bank,
                           engine_config(bpm, 0.0, 1.0,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  engine.push_trigger(expected);
  const auto bytes = render(engine, 100U);
  // The NPC clip and the pressed clip both start at frame zero.
  assert(sample(bytes, 0U, 0U) == 200);
  assert(sample(bytes, 0U, 1U) == 200);
  assert(sample(bytes, 1U, 0U) == 201);
  assert(sample(bytes, 1U, 1U) == 199);

  JudgementResult results[4]{};
  assert(engine.drain_judgements(results, 4U) == 1U);
  assert(results[0].grade == JudgementGrade::Perfect);
  assert(results[0].expected_sound == expected);
  assert(results[0].pressed_sound == expected);
  assert(results[0].queue_delay_ms >= 0.0);
}

void test_wrong_key_plays_the_miss_sound() {
  const auto song = make_song(8000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  const auto chart = pair_chart(0U);
  PlaybackPlan plan;
  assert(otgy::core::build_playback_plan(
             chart,
             {PlaybackSoundLanguage::Chinese, PlaybackDanceMode::EightKey,
              false, 42U, {bpm, 0.0}},
             plan) == otgy::core::PlaybackPlanError::None);
  const auto expected = plan.player_notes[0].expected_sound;
  const auto wrong = expected == PlaybackSoundId::F1_2
                         ? PlaybackSoundId::F1_4
                         : PlaybackSoundId::F1_2;

  PlaybackEngine engine;
  assert(engine.initialize(song, chart, bank,
                           engine_config(bpm, 0.0, 1.0,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  engine.push_trigger(wrong);
  const auto bytes = render(engine, 100U);
  // The NPC note still sounds (100); the wrong key is replaced by the miss
  // sound (kMissSample).
  assert(sample(bytes, 0U, 0U) == 100 + kMissSample);
  JudgementResult results[4]{};
  assert(engine.drain_judgements(results, 4U) == 1U);
  assert(results[0].grade == JudgementGrade::Miss);
  assert(results[0].pressed_sound == wrong);
}

void test_timeout_miss_plays_the_miss_sound() {
  const auto song = make_song(2000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  PlaybackEngine engine;
  assert(engine.initialize(song, pair_chart(0U), bank,
                           engine_config(bpm, 0.0, 1.0,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  // 160 BPM with the wide Bad window: the row-0 note times out after 0.1875 s.
  for (int block = 0; block < 15; ++block) {
    const auto bytes = render(engine, 100U);
    (void)bytes;
    JudgementResult early[4]{};
    assert(engine.drain_judgements(early, 4U) == 0U);
  }
  const auto bytes = render(engine, 100U);
  JudgementResult results[4]{};
  assert(engine.drain_judgements(results, 4U) == 1U);
  assert(results[0].grade == JudgementGrade::Miss);
  // Frame 1500 of the song is 0 on both channels; the miss clip starts there.
  assert(sample(bytes, 0U, 0U) == kMissSample);
  assert(sample(bytes, 0U, 1U) == kMissSample);
}

void test_no_pending_note_is_silent() {
  const auto song = make_song(8000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  const auto chart = pair_chart(16U);
  PlaybackEngine engine;
  assert(engine.initialize(song, chart, bank,
                           engine_config(bpm, 0.0, 1.0,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  engine.push_trigger(PlaybackSoundId::F1_2);
  const auto bytes = render(engine, 100U);
  // No NPC trigger and no key sound: only the song remains.
  assert(sample(bytes, 0U, 0U) == 0);
  assert(sample(bytes, 1U, 0U) == 1);
  JudgementResult results[4]{};
  assert(engine.drain_judgements(results, 4U) == 0U);
}

void test_miss_is_emitted_after_deadline() {
  const auto song = make_song(8000U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  const auto chart = pair_chart(0U);
  PlaybackEngine engine;
  assert(engine.initialize(song, chart, bank,
                           engine_config(bpm, 0.0, 1.0,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  bool saw_miss = false;
  bool saw_expected_note = false;
  for (std::size_t block = 0U; block < 20U; ++block) {
    static_cast<void>(render(engine, 400U));
    JudgementResult results[8]{};
    const auto count = engine.drain_judgements(results, 8U);
    for (std::size_t index = 0U; index < count; ++index) {
      if (results[index].grade == JudgementGrade::Miss) {
        saw_miss = true;
        saw_expected_note = results[index].expected_sound ==
                            PlaybackSoundId::F1_1 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_2 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_3 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_4 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_6 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_7 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_8 ||
                            results[index].expected_sound ==
                                PlaybackSoundId::F1_9;
      }
    }
  }
  assert(saw_miss);
  assert(saw_expected_note);
}

void test_end_truncates_and_finishes() {
  const auto song = make_song(100U);
  PlaybackSoundBank bank(std::vector<PlaybackSoundClip>{});
  const auto bpm = 160.0;
  PlaybackEngine engine;
  assert(engine.initialize(song, ChartDraft({}, {}, {}, {}, {}, {}), bank,
                           engine_config(bpm, 0.0,
                                         4.0 / static_cast<double>(kRate),
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  const auto bytes = render(engine, 8U);
  assert(sample(bytes, 0U, 0U) == 0);
  assert(sample(bytes, 3U, 0U) == 3);
  assert(sample(bytes, 4U, 0U) == 0);
  assert(sample(bytes, 7U, 0U) == 0);
  const auto state = engine.state();
  assert(state.finished);
  assert(state.rendered_frames == 4U);
}

void test_prestart_clip_keeps_tail() {
  const auto song = make_song(100U);
  const auto bank = all_note_bank(100);
  const auto bpm = 160.0;
  const auto start_seconds = 2.0 / static_cast<double>(kRate);
  const auto end_seconds = 100.0 / static_cast<double>(kRate);
  PlaybackEngine engine;
  assert(engine.initialize(song, npc_chart(0U), bank,
                           engine_config(bpm, start_seconds, end_seconds,
                                         wide_judgement(bpm))) ==
         PlaybackEngineError::None);
  const auto bytes = render(engine, 4U);
  // The clip started at frame 0 but playback starts at frame 2, so only its
  // last two frames (both 100) are mixed.
  assert(sample(bytes, 0U, 0U) == 102);
  assert(sample(bytes, 1U, 0U) == 103);
  assert(sample(bytes, 2U, 0U) == 4);
  assert(sample(bytes, 3U, 0U) == 5);
}

}  // namespace

int main() {
  test_song_only_matches_offline();
  test_scheduled_npc_clip_and_voice_tail();
  test_key_trigger_judges_and_plays();
  test_wrong_key_plays_the_miss_sound();
  test_timeout_miss_plays_the_miss_sound();
  test_no_pending_note_is_silent();
  test_miss_is_emitted_after_deadline();
  test_end_truncates_and_finishes();
  test_prestart_clip_keeps_tail();
  return 0;
}
