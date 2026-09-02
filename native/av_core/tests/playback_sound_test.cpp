// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_sound.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartDraft;
using otgy::core::ChartDrillGroup;
using otgy::core::ChartDrillGroupId;
using otgy::core::ChartEventId;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::Pcm16;
using otgy::core::PlaybackDanceMode;
using otgy::core::PlaybackSoundAsset;
using otgy::core::PlaybackSoundBank;
using otgy::core::PlaybackSoundBankError;
using otgy::core::PlaybackSoundConfig;
using otgy::core::PlaybackSoundId;
using otgy::core::PlaybackSoundLanguage;
using otgy::core::RhythmTestSoundConfig;

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value & 0xffU));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value & 0xffU));
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void set_u32(std::vector<std::uint8_t>& output, std::size_t offset,
             std::uint32_t value) {
  output[offset] = static_cast<std::uint8_t>(value & 0xffU);
  output[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  output[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
  output[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::vector<std::uint8_t> wav(
    std::uint32_t sample_rate,
    std::initializer_list<std::array<std::int16_t, 2U>> frames,
    bool mono = true, bool odd_junk = false) {
  std::vector<std::uint8_t> output{'R', 'I', 'F', 'F', 0,   0,
                                   0,   0,   'W', 'A', 'V', 'E'};
  output.insert(output.end(), {'f', 'm', 't', ' '});
  append_u32(output, 16U);
  append_u16(output, 1U);
  append_u16(output, mono ? 1U : 2U);
  append_u32(output, sample_rate);
  append_u32(output, sample_rate * (mono ? 2U : 4U));
  append_u16(output, mono ? 2U : 4U);
  append_u16(output, 16U);
  if (odd_junk) {
    output.insert(output.end(), {'J', 'U', 'N', 'K'});
    append_u32(output, 1U);
    output.push_back(0x7fU);
    output.push_back(0U);
  }
  output.insert(output.end(), {'d', 'a', 't', 'a'});
  append_u32(output,
             static_cast<std::uint32_t>(frames.size() * (mono ? 2U : 4U)));
  for (const auto frame : frames) {
    append_u16(output, static_cast<std::uint16_t>(frame[0]));
    if (!mono) {
      append_u16(output, static_cast<std::uint16_t>(frame[1]));
    }
  }
  set_u32(output, 4U, static_cast<std::uint32_t>(output.size() - 8U));
  return output;
}

PlaybackSoundBank bank_from(
    const std::vector<std::pair<PlaybackSoundId,
                                std::vector<std::uint8_t>>>& files,
    PlaybackSoundLanguage language = PlaybackSoundLanguage::Chinese) {
  std::vector<PlaybackSoundAsset> assets;
  assets.reserve(files.size());
  for (const auto& file : files) {
    assets.push_back(
        {language, file.first, file.second.data(), file.second.size()});
  }
  auto result =
      otgy::core::decode_playback_sound_bank(assets.data(), assets.size());
  assert(result);
  return std::move(result).take_bank();
}

Pcm16 silence(std::uint32_t sample_rate, std::size_t frames) {
  auto result = Pcm16::allocate(sample_rate, frames);
  assert(result);
  return std::move(result).take_pcm();
}

std::int16_t sample(const otgy::core::PlaybackPcm& pcm, std::size_t frame,
                    std::size_t channel = 0U) {
  const auto offset = frame * 4U + channel * 2U;
  const auto& bytes = pcm.bytes();
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
  const auto value = bits <= static_cast<std::uint16_t>(0x7fffU)
                         ? static_cast<std::int32_t>(bits)
                         : static_cast<std::int32_t>(bits) - 65536;
  return static_cast<std::int16_t>(value);
}

PlaybackSoundConfig config(PlaybackDanceMode mode = PlaybackDanceMode::FourKey,
                           bool transitions = true,
                           std::uint64_t seed = 7U) {
  return {PlaybackSoundLanguage::Chinese, mode, transitions, seed, {60.0, 0.0}};
}

ChartDraft full_chart() {
  constexpr ChartDrillGroupId kGroup{1U};
  constexpr otgy::core::ChartDanceMoveDrillId kDrill{1U};
  std::vector<ChartRoleEvent> events{
      {{1U}, 0U, ChartRole::Npc, ChartRoleEventType::BlockStart, kGroup,
       kDrill},
      {{2U}, 1U, ChartRole::Npc, ChartRoleEventType::Note, kGroup, kDrill},
      {{3U}, 2U, ChartRole::Npc, ChartRoleEventType::Note, kGroup, kDrill},
      {{4U}, 3U, ChartRole::Npc, ChartRoleEventType::MoveEnd, kGroup, kDrill},
      {{5U}, 4U, ChartRole::Player, ChartRoleEventType::BlockStart, kGroup,
       kDrill},
      {{6U}, 5U, ChartRole::Player, ChartRoleEventType::Note, kGroup, kDrill},
      {{7U}, 6U, ChartRole::Player, ChartRoleEventType::Note, kGroup, kDrill},
      {{8U}, 7U, ChartRole::Player, ChartRoleEventType::MoveEnd, kGroup,
       kDrill},
  };
  ChartDrillGroup group{kGroup,
                        kDrill,
                        {{1U}, {2U}, {3U}, {4U}},
                        {{5U}, {6U}, {7U}, {8U}}};
  return {std::move(events),
          {{{9U}, 8U, {1U}}},
          {{{10U}, 9U, {1U}, kDrill, 1U}},
          {std::move(group)},
          {},
          {}};
}

std::vector<std::pair<PlaybackSoundId, std::vector<std::uint8_t>>>
all_one_frame_sounds() {
  std::vector<std::pair<PlaybackSoundId, std::vector<std::uint8_t>>> files;
  for (std::uint8_t value = 0U;
       value <= static_cast<std::uint8_t>(PlaybackSoundId::Turn2); ++value) {
    const auto id = static_cast<PlaybackSoundId>(value);
    const auto marker = static_cast<std::int16_t>(100 + value);
    files.push_back({id, wav(4U, {{{marker, marker}}})});
  }
  return files;
}

void test_wav_decoding_and_errors() {
  const auto mono = wav(4U, {{{100, 0}}, {{-200, 0}}}, true, true);
  const auto stereo = wav(4U, {{{300, -400}}}, false);
  const auto bank = bank_from({{PlaybackSoundId::F1_1, mono},
                               {PlaybackSoundId::F1_2, stereo}});
  const auto* mono_pcm =
      bank.find(PlaybackSoundLanguage::Chinese, PlaybackSoundId::F1_1);
  const auto* stereo_pcm =
      bank.find(PlaybackSoundLanguage::Chinese, PlaybackSoundId::F1_2);
  assert(mono_pcm != nullptr && stereo_pcm != nullptr);
  assert(mono_pcm->channel_data(otgy::core::PcmChannel::Left)[1] == -200);
  assert(mono_pcm->channel_data(otgy::core::PcmChannel::Right)[1] == -200);
  assert(stereo_pcm->channel_data(otgy::core::PcmChannel::Left)[0] == 300);
  assert(stereo_pcm->channel_data(otgy::core::PcmChannel::Right)[0] == -400);

  std::vector<std::uint8_t> invalid{'n', 'o', 't', 'w', 'a', 'v'};
  PlaybackSoundAsset bad{PlaybackSoundLanguage::Chinese,
                         PlaybackSoundId::F1_1, invalid.data(),
                         invalid.size()};
  auto result = otgy::core::decode_playback_sound_bank(&bad, 1U);
  assert(!result && result.error() == PlaybackSoundBankError::InvalidWav);

  const std::array<PlaybackSoundAsset, 2U> duplicate{{
      {PlaybackSoundLanguage::Chinese, PlaybackSoundId::F1_1, mono.data(),
       mono.size()},
      {PlaybackSoundLanguage::Chinese, PlaybackSoundId::F1_1, mono.data(),
       mono.size()},
  }};
  result = otgy::core::decode_playback_sound_bank(duplicate.data(),
                                                   duplicate.size());
  assert(!result && result.error() == PlaybackSoundBankError::DuplicateAsset);
}

void test_mapping_pairing_and_switch() {
  const auto bank = bank_from(all_one_frame_sounds());
  const auto chart = full_chart();
  const auto song = silence(4U, 12U);
  auto rendered = otgy::core::render_chart_playback_pcm(
      song, chart, bank, config(), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) ==
         100 + static_cast<int>(PlaybackSoundId::Turn1));
  const auto first_npc_note = sample(*rendered.pcm(), 1U) - 100;
  const auto second_npc_note = sample(*rendered.pcm(), 2U) - 100;
  constexpr std::array<int, 4U> kFourKeyNpcIds{{11, 13, 14, 16}};
  assert(std::find(kFourKeyNpcIds.begin(), kFourKeyNpcIds.end(),
                   first_npc_note) != kFourKeyNpcIds.end());
  assert(std::find(kFourKeyNpcIds.begin(), kFourKeyNpcIds.end(),
                   second_npc_note) != kFourKeyNpcIds.end());
  assert(sample(*rendered.pcm(), 5U) - 100 == first_npc_note - 10);
  assert(sample(*rendered.pcm(), 6U) - 100 == second_npc_note - 10);
  assert(sample(*rendered.pcm(), 7U) - 100 ==
         sample(*rendered.pcm(), 3U) - 100 - 10);
  assert(sample(*rendered.pcm(), 8U) ==
         100 + static_cast<int>(PlaybackSoundId::Turn2));
  assert(sample(*rendered.pcm(), 9U) ==
         100 + static_cast<int>(PlaybackSoundId::SpaceBar));

  rendered = otgy::core::render_chart_playback_pcm(
      song, chart, bank, config(PlaybackDanceMode::FourKey, false), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 0);
  assert(sample(*rendered.pcm(), 4U) == 0);
  assert(sample(*rendered.pcm(), 8U) == 0);
  assert(sample(*rendered.pcm(), 9U) ==
         100 + static_cast<int>(PlaybackSoundId::SpaceBar));
}

void test_eight_key_range_and_incomplete_pairing() {
  const auto bank = bank_from(all_one_frame_sounds());
  const auto chart = full_chart();
  const auto song = silence(4U, 12U);
  for (std::uint64_t seed = 0U; seed < 64U; ++seed) {
    const auto rendered = otgy::core::render_chart_playback_pcm(
        song, chart, bank,
        config(PlaybackDanceMode::EightKey, false, seed), 0.0);
    assert(rendered);
    const auto id = sample(*rendered.pcm(), 1U) - 100;
    assert(id >= 10 && id <= 17);
  }

  constexpr ChartDrillGroupId kGroup{1U};
  constexpr otgy::core::ChartDanceMoveDrillId kDrill{1U};
  const ChartDraft incomplete{
      {{{1U}, 1U, ChartRole::Player, ChartRoleEventType::Note, kGroup, kDrill},
       {{2U}, 2U, ChartRole::Player, ChartRoleEventType::MoveEnd, kGroup,
        kDrill}},
      {},
      {},
      {{kGroup, kDrill, {}, {{1U}, {2U}}}},
      {},
      {}};
  const auto rendered = otgy::core::render_chart_playback_pcm(
      song, incomplete, bank, config(), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 1U) == 0);
  assert(sample(*rendered.pcm(), 2U) == 0);
}

ChartDraft show_move_chart() {
  return {{}, {}, {{{1U}, 0U, {1U}, {1U}, 1U}}, {}, {}, {}};
}

void test_resampling_tail_and_saturation() {
  const auto clip = wav(2U, {{{0, 0}}, {{1000, 1000}}});
  const auto bank = bank_from({{PlaybackSoundId::SpaceBar, clip}});
  const auto song = silence(4U, 8U);
  auto rendered = otgy::core::render_chart_playback_pcm(
      song, show_move_chart(), bank, config(), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 0);
  assert(sample(*rendered.pcm(), 1U) == 500);
  assert(sample(*rendered.pcm(), 2U) == 1000);
  assert(sample(*rendered.pcm(), 3U) == 1000);

  const auto tail_clip =
      wav(4U, {{{100, 100}}, {{200, 200}}, {{300, 300}}, {{400, 400}}});
  const auto tail_bank =
      bank_from({{PlaybackSoundId::SpaceBar, tail_clip}});
  rendered = otgy::core::render_chart_playback_pcm(
      song, show_move_chart(), tail_bank, config(), 0.5);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 300);
  assert(sample(*rendered.pcm(), 1U) == 400);

  const auto loud = wav(4U, {{{30000, 30000}}});
  const auto loud_bank = bank_from({{PlaybackSoundId::SpaceBar, loud},
                                    {PlaybackSoundId::Turn2, loud}});
  const ChartDraft overlapping{
      {},
      {{{1U}, 0U, {1U}}},
      {{{2U}, 0U, {1U}, {1U}, 1U}},
      {},
      {},
      {}};
  rendered = otgy::core::render_chart_playback_pcm(
      song, overlapping, loud_bank, config(), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 32767);

  const auto quiet = wav(4U, {{{-30000, -30000}}});
  const auto quiet_bank = bank_from({{PlaybackSoundId::SpaceBar, quiet},
                                     {PlaybackSoundId::Turn2, quiet}});
  rendered = otgy::core::render_chart_playback_pcm(
      song, overlapping, quiet_bank, config(), 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == -32768);
}

void test_missing_asset_and_language() {
  const auto empty_bank = bank_from({});
  const auto song = silence(4U, 4U);
  auto rendered = otgy::core::render_chart_playback_pcm(
      song, show_move_chart(), empty_bank, config(), 0.0);
  assert(!rendered &&
         rendered.error() == otgy::core::PlaybackPcmError::MissingSoundAsset);

  const auto chinese = wav(4U, {{{100, 100}}});
  const auto english = wav(4U, {{{200, 200}}});
  std::array<PlaybackSoundAsset, 2U> assets{{
      {PlaybackSoundLanguage::Chinese, PlaybackSoundId::SpaceBar,
       chinese.data(), chinese.size()},
      {PlaybackSoundLanguage::English, PlaybackSoundId::SpaceBar,
       english.data(), english.size()},
  }};
  auto decoded =
      otgy::core::decode_playback_sound_bank(assets.data(), assets.size());
  assert(decoded);
  auto bilingual = std::move(decoded).take_bank();
  auto english_config = config();
  english_config.language = PlaybackSoundLanguage::English;
  rendered = otgy::core::render_chart_playback_pcm(
      song, show_move_chart(), bilingual, english_config, 0.0);
  assert(rendered && sample(*rendered.pcm(), 0U) == 200);
}

void test_rhythm_test_measure_clicks_and_resume() {
  const auto click = wav(
      4U, {{{100, 100}}, {{200, 200}}, {{300, 300}}, {{400, 400}}});
  const auto bank = bank_from({{PlaybackSoundId::SpaceBar, click}});
  const auto song = silence(4U, 40U);
  const RhythmTestSoundConfig rhythm_config{
      PlaybackSoundLanguage::Chinese, {60.0, 0.0}};

  auto rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, bank, rhythm_config, 0.0);
  assert(rendered);
  for (const auto frame : {0U, 16U, 32U}) {
    assert(sample(*rendered.pcm(), frame) == 100);
    assert(sample(*rendered.pcm(), frame + 1U) == 200);
    assert(sample(*rendered.pcm(), frame + 2U) == 300);
    assert(sample(*rendered.pcm(), frame + 3U) == 400);
  }
  assert(sample(*rendered.pcm(), 4U) == 0);
  assert(song.channel_data(otgy::core::PcmChannel::Left)[0] == 0);

  rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, bank, rhythm_config, 4.625);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 400);
  assert(sample(*rendered.pcm(), 1U) == 0);
  assert(sample(*rendered.pcm(), 13U) == 100);
}

void test_rhythm_test_offsets_validation_and_saturation() {
  const auto click = wav(4U, {{{100, 100}}});
  const auto bank = bank_from({{PlaybackSoundId::SpaceBar, click}});
  const auto song = silence(4U, 24U);

  auto rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, bank,
      {PlaybackSoundLanguage::Chinese, {60.0, 0.5}}, 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 0U) == 0);
  assert(sample(*rendered.pcm(), 14U) == 100);

  rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, bank,
      {PlaybackSoundLanguage::Chinese, {60.0, -0.5}}, 0.0);
  assert(rendered);
  assert(sample(*rendered.pcm(), 2U) == 100);
  assert(sample(*rendered.pcm(), 18U) == 100);

  rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, bank,
      {PlaybackSoundLanguage::Chinese, {0.0, 0.0}}, 0.0);
  assert(!rendered &&
         rendered.error() == otgy::core::PlaybackPcmError::SoundTimingFailure);

  const auto empty_bank = bank_from({});
  rendered = otgy::core::render_rhythm_test_playback_pcm(
      song, empty_bank,
      {PlaybackSoundLanguage::Chinese, {60.0, 0.0}}, 0.0);
  assert(!rendered &&
         rendered.error() == otgy::core::PlaybackPcmError::MissingSoundAsset);

  auto loud_song_result = Pcm16::allocate(4U, 4U);
  assert(loud_song_result);
  auto loud_song = std::move(loud_song_result).take_pcm();
  loud_song.mutable_channel_data(otgy::core::PcmChannel::Left)[0] = 32700;
  loud_song.mutable_channel_data(otgy::core::PcmChannel::Right)[0] = 32700;
  rendered = otgy::core::render_rhythm_test_playback_pcm(
      loud_song, bank,
      {PlaybackSoundLanguage::Chinese, {60.0, 0.0}}, 0.0);
  assert(rendered && sample(*rendered.pcm(), 0U) == 32767);
}

}  // namespace

int main() {
  test_wav_decoding_and_errors();
  test_mapping_pairing_and_switch();
  test_eight_key_range_and_incomplete_pairing();
  test_resampling_tail_and_saturation();
  test_missing_asset_and_language();
  test_rhythm_test_measure_clicks_and_resume();
  test_rhythm_test_offsets_validation_and_saturation();
  return 0;
}
