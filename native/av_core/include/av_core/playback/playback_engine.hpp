// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"
#include "av_core/chart/chart_draft.hpp"
#include "av_core/playback/playback_judgement.hpp"
#include "av_core/playback/playback_plan.hpp"
#include "av_core/playback/playback_sound.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace otgy::core {

struct PlaybackEngineConfig final {
  PlaybackSoundLanguage language;
  PlaybackDanceMode dance_mode;
  bool transition_sounds_enabled;
  std::uint64_t random_seed;
  TimingParameters timing;
  double start_seconds;
  double end_seconds;
  JudgementConfig judgement;
  /// When false, presses are a free soundboard: the pressed sound always
  /// plays, no note is matched or consumed, and no Miss is produced.
  bool judgement_enabled;
};

enum class PlaybackEngineError : std::uint8_t {
  None = 0,
  InvalidConfiguration = 1,
  AllocationFailure = 2,
  SoundTimingFailure = 3,
  MissingSoundAsset = 4,
  SizeOverflow = 5,
};

[[nodiscard]] constexpr std::string_view playback_engine_error_message(
    PlaybackEngineError error) noexcept {
  switch (error) {
    case PlaybackEngineError::None:
      return {};
    case PlaybackEngineError::InvalidConfiguration:
      return "playback engine configuration is invalid";
    case PlaybackEngineError::AllocationFailure:
      return "unable to allocate playback engine storage";
    case PlaybackEngineError::SoundTimingFailure:
      return "playback engine could not project a sound row onto audio time";
    case PlaybackEngineError::MissingSoundAsset:
      return "playback engine is missing a required sound asset";
    case PlaybackEngineError::SizeOverflow:
      return "playback engine dimensions exceed the representable size";
  }
  return "unknown playback engine error";
}

struct PlaybackEngineState final {
  std::uint64_t rendered_frames;
  double position_seconds;
  bool finished;
};

/// Real-time simulation renderer. `initialize` may allocate; `render`,
/// `push_trigger` and `drain_judgements` do not. The engine references the
/// song, sound bank and chart plan for its lifetime; the caller must keep the
/// song and bank alive until the engine is reset or destroyed.
class PlaybackEngine final {
 public:
  PlaybackEngine() noexcept = default;
  ~PlaybackEngine() = default;
  PlaybackEngine(const PlaybackEngine&) = delete;
  PlaybackEngine& operator=(const PlaybackEngine&) = delete;
  PlaybackEngine(PlaybackEngine&&) = delete;
  PlaybackEngine& operator=(PlaybackEngine&&) = delete;

  [[nodiscard]] PlaybackEngineError initialize(
      const Pcm16& song, const ChartDraft& chart,
      const PlaybackSoundBank& sound_bank,
      const PlaybackEngineConfig& config) noexcept;

  [[nodiscard]] bool initialized() const noexcept {
    return initialized_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint32_t sample_rate() const noexcept {
    return sample_rate_;
  }
  [[nodiscard]] PlaybackEngineState state() const noexcept;

  /// Producer side of the key queue. Records the native steady-clock time.
  /// Safe to call from one UI thread while the audio thread renders.
  void push_trigger(PlaybackSoundId sound) noexcept;

  /// Writes `frames` interleaved stereo PCM16LE frames. Called from the audio
  /// thread. The output buffer must hold `frames * 4` bytes. Frames after the
  /// configured end become silence.
  void render(std::uint8_t* output, std::uint32_t frames) noexcept;

  /// Consumer side of the judgement queue. Returns the number of results
  /// written.
  [[nodiscard]] std::size_t drain_judgements(
      JudgementResult* output, std::size_t capacity) noexcept;

  void reset() noexcept;

 private:
  static constexpr std::size_t kKeyCapacity = 64U;
  static constexpr std::size_t kJudgementCapacity = 128U;
  static constexpr std::size_t kVoiceCapacity = 32U;

  struct KeyEvent final {
    PlaybackSoundId sound;
    std::chrono::steady_clock::time_point time;
  };

  struct ScheduledTrigger final {
    std::int64_t frame;
    PlaybackSoundId sound;
    std::size_t order;
  };

  struct Voice final {
    const Pcm16* clip;
    std::uint64_t clip_frame;
    std::uint64_t remaining;
  };

  void clear_queues() noexcept;
  void mix_voice_segment(Voice& voice, std::uint8_t* output,
                         std::uint32_t offset, std::uint32_t max_frames,
                         std::uint32_t sample_rate) noexcept;
  [[nodiscard]] Voice* start_voice(const Pcm16& clip,
                                   std::uint64_t skipped_frames) noexcept;
  void mix_existing_voices(std::uint8_t* output, std::uint32_t frames) noexcept;
  void push_judgement(const JudgementResult& result) noexcept;

  const Pcm16* song_ = nullptr;
  const PlaybackSoundBank* sound_bank_ = nullptr;
  PlaybackEngineConfig config_{};
  PlaybackPlan plan_;
  std::vector<ScheduledTrigger> triggers_;
  JudgementSession judgement_;
  std::size_t next_trigger_ = 0U;

  std::uint32_t sample_rate_ = 0U;
  std::int64_t start_frame_ = 0;
  std::int64_t end_frame_ = 0;
  std::int64_t song_frame_ = 0;
  std::atomic<std::uint64_t> rendered_frames_{0U};
  std::atomic<bool> finished_{false};
  std::atomic<bool> initialized_{false};

  std::array<Voice, kVoiceCapacity> voices_{};

  std::array<KeyEvent, kKeyCapacity> key_events_{};
  std::atomic<std::uint32_t> key_head_{0U};
  std::atomic<std::uint32_t> key_tail_{0U};

  std::array<JudgementResult, kJudgementCapacity> judgements_{};
  std::atomic<std::uint32_t> judgement_head_{0U};
  std::atomic<std::uint32_t> judgement_tail_{0U};
};

}  // namespace otgy::core
