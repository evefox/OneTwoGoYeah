// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_engine.hpp"

#include "playback_mix.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdexcept>

namespace otgy::core {
namespace {

[[nodiscard]] bool valid_language(PlaybackSoundLanguage language) noexcept {
  return language == PlaybackSoundLanguage::Chinese ||
         language == PlaybackSoundLanguage::English;
}

[[nodiscard]] bool valid_dance_mode(PlaybackDanceMode mode) noexcept {
  return mode == PlaybackDanceMode::FourKey ||
         mode == PlaybackDanceMode::EightKey;
}

}  // namespace

PlaybackEngineError PlaybackEngine::initialize(
    const Pcm16& song, const ChartDraft& chart,
    const PlaybackSoundBank& sound_bank,
    const PlaybackEngineConfig& config) noexcept {
  reset();
  if (!valid_language(config.language) ||
      !valid_dance_mode(config.dance_mode)) {
    return PlaybackEngineError::InvalidConfiguration;
  }
  if (!std::isfinite(config.start_seconds) ||
      !std::isfinite(config.end_seconds) ||
      config.end_seconds < config.start_seconds ||
      !std::isfinite(config.judgement.offset_ms)) {
    return PlaybackEngineError::InvalidConfiguration;
  }
  const auto sample_rate = song.sample_rate();
  if (sample_rate == 0U) {
    return PlaybackEngineError::InvalidConfiguration;
  }
  const auto start_rounded =
      std::round(config.start_seconds * static_cast<double>(sample_rate));
  const auto end_rounded =
      std::round(config.end_seconds * static_cast<double>(sample_rate));
  if (!std::isfinite(start_rounded) || !std::isfinite(end_rounded) ||
      start_rounded <= -detail::kSignedFrameLimit ||
      start_rounded >= detail::kSignedFrameLimit ||
      end_rounded <= -detail::kSignedFrameLimit ||
      end_rounded >= detail::kSignedFrameLimit) {
    return PlaybackEngineError::InvalidConfiguration;
  }

  try {
    PlaybackSoundConfig plan_config{config.language,
                                    config.dance_mode,
                                    config.transition_sounds_enabled,
                                    config.random_seed,
                                    config.timing};
    PlaybackPlan full_plan;
    const auto plan_error =
        build_playback_plan(chart, plan_config, full_plan);
    if (plan_error == PlaybackPlanError::InvalidConfiguration) {
      return PlaybackEngineError::InvalidConfiguration;
    }
    if (plan_error == PlaybackPlanError::AllocationFailure) {
      return PlaybackEngineError::AllocationFailure;
    }
    if (plan_error == PlaybackPlanError::SizeOverflow) {
      return PlaybackEngineError::SizeOverflow;
    }

    plan_ = std::move(full_plan);
    triggers_.reserve(plan_.audio_triggers.size());
    const auto start_frame = static_cast<std::int64_t>(start_rounded);
    for (const auto& trigger : plan_.audio_triggers) {
      if (trigger.player) {
        continue;
      }
      std::int64_t frame = 0;
      if (!detail::project_frame(trigger.row, sample_rate, config.timing,
                                 frame)) {
        return PlaybackEngineError::SoundTimingFailure;
      }
      if (sound_bank.find(config.language, trigger.sound) == nullptr) {
        return PlaybackEngineError::MissingSoundAsset;
      }
      triggers_.push_back({frame, trigger.sound, trigger.order});
    }
    std::stable_sort(triggers_.begin(), triggers_.end(),
                     [](const ScheduledTrigger& left,
                        const ScheduledTrigger& right) {
                       if (left.frame != right.frame) {
                         return left.frame < right.frame;
                       }
                       return left.order < right.order;
                     });

    // Start the tail of clips that began before the playback start. This
    // mirrors the offline renderer, which skips already-played leading frames
    // but keeps the remainder of an overlapping clip.
    sample_rate_ = sample_rate;
    voices_.fill(Voice{nullptr, 0U, 0U});
    next_trigger_ = 0U;
    while (next_trigger_ < triggers_.size() &&
           triggers_[next_trigger_].frame < start_frame) {
      const auto& trigger = triggers_[next_trigger_];
      const auto* clip = sound_bank.find(config.language, trigger.sound);
      const auto skipped = start_frame - trigger.frame;
      if (clip != nullptr && skipped > 0) {
        static_cast<void>(
            start_voice(*clip, static_cast<std::uint64_t>(skipped)));
      }
      ++next_trigger_;
    }

    auto judgement_config = config.judgement;
    judgement_config.timing = config.timing;
    const auto judgement_error = judgement_.reset(plan_, judgement_config);
    if (judgement_error == JudgementError::InvalidConfiguration) {
      return PlaybackEngineError::InvalidConfiguration;
    }
    if (judgement_error == JudgementError::AllocationFailure) {
      return PlaybackEngineError::AllocationFailure;
    }

    song_ = &song;
    sound_bank_ = &sound_bank;
    config_ = config;
    config_.judgement = judgement_config;
    sample_rate_ = sample_rate;
    start_frame_ = start_frame;
    end_frame_ = static_cast<std::int64_t>(end_rounded);
    song_frame_ = start_frame_;
    rendered_frames_.store(0U, std::memory_order_release);
    finished_.store(end_frame_ <= start_frame_, std::memory_order_release);
    initialized_.store(true, std::memory_order_release);
    return PlaybackEngineError::None;
  } catch (const std::bad_alloc&) {
    reset();
    return PlaybackEngineError::AllocationFailure;
  } catch (const std::length_error&) {
    reset();
    return PlaybackEngineError::SizeOverflow;
  }
}

PlaybackEngineState PlaybackEngine::state() const noexcept {
  if (!initialized_.load(std::memory_order_acquire)) {
    return PlaybackEngineState{0U, 0.0, true};
  }
  const auto rendered = rendered_frames_.load(std::memory_order_acquire);
  const auto position =
      config_.start_seconds +
      static_cast<double>(rendered) / static_cast<double>(sample_rate_);
  return PlaybackEngineState{
      rendered,
      std::min(position, config_.end_seconds),
      finished_.load(std::memory_order_acquire),
  };
}

void PlaybackEngine::push_trigger(PlaybackSoundId sound) noexcept {
  if (!initialized_.load(std::memory_order_acquire)) {
    return;
  }
  const auto head = key_head_.load(std::memory_order_relaxed);
  const auto tail = key_tail_.load(std::memory_order_acquire);
  if (head - tail >= kKeyCapacity) {
    return;
  }
  key_events_[head % kKeyCapacity] = KeyEvent{
      sound, std::chrono::steady_clock::now()};
  key_head_.store(head + 1U, std::memory_order_release);
}

void PlaybackEngine::render(std::uint8_t* output,
                            std::uint32_t frames) noexcept {
  if (output == nullptr || frames == 0U) {
    return;
  }
  const auto byte_count = static_cast<std::size_t>(frames) * 4U;
  if (!initialized_.load(std::memory_order_acquire) ||
      finished_.load(std::memory_order_acquire)) {
    std::fill_n(output, byte_count, std::uint8_t{0});
    return;
  }

  const auto block_start_timeline =
      config_.start_seconds +
      static_cast<double>(rendered_frames_.load(std::memory_order_relaxed)) /
          static_cast<double>(sample_rate_);
  const auto frames_to_end = end_frame_ - song_frame_;
  if (frames_to_end <= 0) {
    finished_.store(true, std::memory_order_release);
    std::fill_n(output, byte_count, std::uint8_t{0});
    return;
  }
  auto frames_this = frames;
  if (frames_to_end < static_cast<std::int64_t>(frames)) {
    frames_this = static_cast<std::uint32_t>(frames_to_end);
  }
  const auto rendered_bytes = static_cast<std::size_t>(frames_this) * 4U;

  detail::mix_song_block(*song_, song_frame_, frames_this, output);
  if (rendered_bytes < byte_count) {
    std::fill(output + rendered_bytes, output + byte_count, std::uint8_t{0});
  }

  mix_existing_voices(output, frames_this);

  const auto block_end_frame =
      song_frame_ + static_cast<std::int64_t>(frames_this);
  while (next_trigger_ < triggers_.size() &&
         triggers_[next_trigger_].frame < block_end_frame) {
    const auto& trigger = triggers_[next_trigger_];
    if (trigger.frame < song_frame_) {
      ++next_trigger_;
      continue;
    }
    const auto offset =
        static_cast<std::uint32_t>(trigger.frame - song_frame_);
    if (offset >= frames_this) {
      break;
    }
    const auto* clip = sound_bank_->find(config_.language, trigger.sound);
    if (clip != nullptr) {
      if (auto* voice = start_voice(*clip, 0U)) {
        mix_voice_segment(*voice, output, offset, frames_this, sample_rate_);
      }
    }
    ++next_trigger_;
  }

  const auto now = std::chrono::steady_clock::now();
  auto key_tail = key_tail_.load(std::memory_order_acquire);
  const auto key_head = key_head_.load(std::memory_order_acquire);
  while (key_tail != key_head) {
    const auto& event = key_events_[key_tail % kKeyCapacity];
    const auto delta =
        std::chrono::duration<double>(event.time - now).count();
    auto input_seconds = block_start_timeline + delta;
    input_seconds =
        std::min(input_seconds, block_start_timeline);
    input_seconds =
        std::max(input_seconds, block_start_timeline - 0.05);
    auto result = judgement_.press(input_seconds, event.sound);
    if (result) {
      const auto feedback_sound =
          result->grade == JudgementGrade::Miss ? PlaybackSoundId::Miss
                                                : event.sound;
      const auto* clip =
          sound_bank_->find(config_.language, feedback_sound);
      if (clip != nullptr) {
        if (auto* voice = start_voice(*clip, 0U)) {
          mix_voice_segment(*voice, output, 0U, frames_this, sample_rate_);
        }
      }
      push_judgement(*result);
    }
    ++key_tail;
  }
  key_tail_.store(key_tail, std::memory_order_release);

  const auto block_end_timeline =
      block_start_timeline +
      static_cast<double>(frames_this) / static_cast<double>(sample_rate_);
  JudgementResult misses[8]{};
  const auto miss_count = judgement_.advance(block_end_timeline, misses, 8U);
  for (std::size_t index = 0U; index < miss_count; ++index) {
    push_judgement(misses[index]);
  }

  song_frame_ += static_cast<std::int64_t>(frames_this);
  rendered_frames_.fetch_add(static_cast<std::uint64_t>(frames_this),
                             std::memory_order_release);
  if (frames_this < frames || song_frame_ >= end_frame_) {
    finished_.store(true, std::memory_order_release);
  }
}

std::size_t PlaybackEngine::drain_judgements(
    JudgementResult* output, std::size_t capacity) noexcept {
  auto tail = judgement_tail_.load(std::memory_order_relaxed);
  const auto head = judgement_head_.load(std::memory_order_acquire);
  std::size_t count = 0U;
  while (tail != head && count < capacity) {
    if (output != nullptr) {
      output[count] = judgements_[tail % kJudgementCapacity];
    }
    ++tail;
    ++count;
  }
  judgement_tail_.store(tail, std::memory_order_release);
  return count;
}

void PlaybackEngine::reset() noexcept {
  initialized_.store(false, std::memory_order_release);
  song_ = nullptr;
  sound_bank_ = nullptr;
  config_ = PlaybackEngineConfig{};
  plan_ = PlaybackPlan{};
  triggers_.clear();
  judgement_ = JudgementSession{};
  next_trigger_ = 0U;
  sample_rate_ = 0U;
  start_frame_ = 0;
  end_frame_ = 0;
  song_frame_ = 0;
  rendered_frames_.store(0U, std::memory_order_release);
  finished_.store(false, std::memory_order_release);
  voices_.fill(Voice{nullptr, 0U, 0U});
  clear_queues();
}

void PlaybackEngine::clear_queues() noexcept {
  key_head_.store(0U, std::memory_order_relaxed);
  key_tail_.store(0U, std::memory_order_relaxed);
  judgement_head_.store(0U, std::memory_order_relaxed);
  judgement_tail_.store(0U, std::memory_order_relaxed);
}

PlaybackEngine::Voice* PlaybackEngine::start_voice(
    const Pcm16& clip, std::uint64_t skipped_frames) noexcept {
  std::uint64_t clip_frames = 0U;
  if (!detail::resampled_frame_count(clip, sample_rate_, clip_frames) ||
      clip_frames == 0U || skipped_frames >= clip_frames) {
    return nullptr;
  }
  for (auto& voice : voices_) {
    if (voice.clip == nullptr) {
      voice = Voice{&clip, skipped_frames, clip_frames - skipped_frames};
      return &voice;
    }
  }
  return nullptr;
}

void PlaybackEngine::mix_voice_segment(Voice& voice, std::uint8_t* output,
                                       std::uint32_t offset,
                                       std::uint32_t max_frames,
                                       std::uint32_t sample_rate) noexcept {
  if (voice.clip == nullptr || voice.remaining == 0U ||
      offset >= max_frames) {
    return;
  }
  const auto available =
      static_cast<std::uint64_t>(max_frames - offset);
  const auto count = std::min(voice.remaining, available);
  constexpr std::size_t kBytesPerFrame = 4U;
  for (std::uint64_t index = 0U; index < count; ++index) {
    const auto source = voice.clip_frame + index;
    const auto left = detail::resampled_sample(*voice.clip, PcmChannel::Left,
                                               source, sample_rate);
    const auto right = detail::resampled_sample(*voice.clip, PcmChannel::Right,
                                                source, sample_rate);
    const auto byte_offset =
        static_cast<std::size_t>(offset + index) * kBytesPerFrame;
    detail::write_sample_le(
        output, byte_offset,
        detail::saturating_add(detail::read_sample_le(output, byte_offset),
                               left));
    detail::write_sample_le(
        output, byte_offset + 2U,
        detail::saturating_add(
            detail::read_sample_le(output, byte_offset + 2U), right));
  }
  voice.clip_frame += count;
  voice.remaining -= count;
  if (voice.remaining == 0U) {
    voice.clip = nullptr;
  }
}

void PlaybackEngine::mix_existing_voices(std::uint8_t* output,
                                         std::uint32_t frames) noexcept {
  for (auto& voice : voices_) {
    if (voice.clip != nullptr) {
      mix_voice_segment(voice, output, 0U, frames, sample_rate_);
    }
  }
}

void PlaybackEngine::push_judgement(const JudgementResult& result) noexcept {
  const auto head = judgement_head_.load(std::memory_order_relaxed);
  const auto tail = judgement_tail_.load(std::memory_order_acquire);
  if (head - tail >= kJudgementCapacity) {
    return;
  }
  judgements_[head % kJudgementCapacity] = result;
  judgement_head_.store(head + 1U, std::memory_order_release);
}

}  // namespace otgy::core
