// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/chart/chart_draft.hpp"
#include "av_core/playback/playback_pcm.hpp"
#include "av_core/timing/timing_conversion.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class PlaybackSoundLanguage : std::uint8_t {
  Chinese = 0,
  English = 1,
};

enum class PlaybackDanceMode : std::uint8_t {
  FourKey = 0,
  EightKey = 1,
};

enum class PlaybackSoundId : std::uint8_t {
  F1_1 = 0,
  F1_2 = 1,
  F1_3 = 2,
  F1_4 = 3,
  F1_6 = 4,
  F1_7 = 5,
  F1_8 = 6,
  F1_9 = 7,
  F1Go = 8,
  F1Yeah = 9,
  M1_1 = 10,
  M1_2 = 11,
  M1_3 = 12,
  M1_4 = 13,
  M1_6 = 14,
  M1_7 = 15,
  M1_8 = 16,
  M1_9 = 17,
  M1Go = 18,
  M1Yeah = 19,
  SpaceBar = 20,
  Turn1 = 21,
  Turn2 = 22,
};

struct PlaybackSoundAsset final {
  PlaybackSoundLanguage language;
  PlaybackSoundId id;
  const std::uint8_t* wav_bytes;
  std::size_t wav_size;
};

enum class PlaybackSoundBankError : std::uint8_t {
  None = 0,
  InvalidAsset = 1,
  DuplicateAsset = 2,
  InvalidWav = 3,
  UnsupportedWav = 4,
  SizeOverflow = 5,
  AllocationFailure = 6,
};

[[nodiscard]] constexpr std::string_view playback_sound_bank_error_message(
    PlaybackSoundBankError error) noexcept {
  switch (error) {
    case PlaybackSoundBankError::None:
      return {};
    case PlaybackSoundBankError::InvalidAsset:
      return "playback sound asset has an invalid language, id or byte view";
    case PlaybackSoundBankError::DuplicateAsset:
      return "playback sound bank contains a duplicate language and id";
    case PlaybackSoundBankError::InvalidWav:
      return "playback sound asset is not a valid RIFF/WAVE file";
    case PlaybackSoundBankError::UnsupportedWav:
      return "playback sound WAV must be mono or stereo PCM16";
    case PlaybackSoundBankError::SizeOverflow:
      return "playback sound dimensions exceed the representable size";
    case PlaybackSoundBankError::AllocationFailure:
      return "unable to allocate playback sound samples";
  }
  return "unknown playback sound bank error";
}

struct PlaybackSoundClip final {
  PlaybackSoundLanguage language;
  PlaybackSoundId id;
  Pcm16 pcm;
};

class PlaybackSoundBank final {
 public:
  explicit PlaybackSoundBank(std::vector<PlaybackSoundClip>&& clips) noexcept;

  [[nodiscard]] const Pcm16* find(PlaybackSoundLanguage language,
                                  PlaybackSoundId id) const noexcept;

 private:
  std::vector<PlaybackSoundClip> clips_;
};

class PlaybackSoundBankResult final {
 public:
  explicit PlaybackSoundBankResult(PlaybackSoundBank bank) noexcept;
  explicit PlaybackSoundBankResult(PlaybackSoundBankError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const PlaybackSoundBank* bank() const noexcept;
  [[nodiscard]] PlaybackSoundBankError error() const noexcept;
  [[nodiscard]] PlaybackSoundBank take_bank() &&;

 private:
  std::variant<PlaybackSoundBank, PlaybackSoundBankError> storage_;
};

[[nodiscard]] PlaybackSoundBankResult decode_playback_sound_bank(
    const PlaybackSoundAsset* assets, std::size_t asset_count) noexcept;

struct PlaybackSoundConfig final {
  PlaybackSoundLanguage language;
  PlaybackDanceMode dance_mode;
  bool transition_sounds_enabled;
  std::uint64_t random_seed;
  TimingParameters timing;
};

struct RhythmTestSoundConfig final {
  PlaybackSoundLanguage language;
  TimingParameters timing;
};

/// Renders song PCM and chart preview sounds into one interleaved PCM16LE
/// device buffer. The source song, chart and sound bank are not modified.
[[nodiscard]] PlaybackPcmResult render_chart_playback_pcm(
    const Pcm16& song, const ChartDraft& chart,
    const PlaybackSoundBank& sound_bank, PlaybackSoundConfig config,
    double timeline_start_seconds) noexcept;

/// Renders song PCM with SpaceBar mixed at every four-beat measure boundary.
/// Chart events are intentionally not accepted by this operation and therefore
/// cannot affect its output. The source song and sound bank are not modified.
[[nodiscard]] PlaybackPcmResult render_rhythm_test_playback_pcm(
    const Pcm16& song, const PlaybackSoundBank& sound_bank,
    RhythmTestSoundConfig config, double timeline_start_seconds) noexcept;

}  // namespace otgy::core
