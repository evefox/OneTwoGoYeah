// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/playback/playback_sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

constexpr std::uint8_t kMaximumSoundId =
    static_cast<std::uint8_t>(PlaybackSoundId::Turn2);
constexpr double kSignedFrameLimit = 0x1p63;

[[nodiscard]] bool valid_language(PlaybackSoundLanguage language) noexcept {
  return language == PlaybackSoundLanguage::Chinese ||
         language == PlaybackSoundLanguage::English;
}

[[nodiscard]] bool valid_dance_mode(PlaybackDanceMode mode) noexcept {
  return mode == PlaybackDanceMode::FourKey ||
         mode == PlaybackDanceMode::EightKey;
}

[[nodiscard]] bool valid_sound_id(PlaybackSoundId id) noexcept {
  return static_cast<std::uint8_t>(id) <= kMaximumSoundId;
}

[[nodiscard]] std::uint16_t read_u16(const std::uint8_t* data) noexcept {
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* data) noexcept {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] bool fourcc(const std::uint8_t* data, char a, char b, char c,
                          char d) noexcept {
  return data[0] == static_cast<std::uint8_t>(a) &&
         data[1] == static_cast<std::uint8_t>(b) &&
         data[2] == static_cast<std::uint8_t>(c) &&
         data[3] == static_cast<std::uint8_t>(d);
}

class DecodedWavResult final {
 public:
  explicit DecodedWavResult(Pcm16 pcm) noexcept : storage_(std::move(pcm)) {}
  explicit DecodedWavResult(PlaybackSoundBankError error) noexcept
      : storage_(error) {}

  [[nodiscard]] explicit operator bool() const noexcept {
    return std::holds_alternative<Pcm16>(storage_);
  }
  [[nodiscard]] PlaybackSoundBankError error() const noexcept {
    const auto* value = std::get_if<PlaybackSoundBankError>(&storage_);
    return value == nullptr ? PlaybackSoundBankError::None : *value;
  }
  [[nodiscard]] Pcm16 take_pcm() && {
    return std::move(std::get<Pcm16>(storage_));
  }

 private:
  std::variant<Pcm16, PlaybackSoundBankError> storage_;
};

[[nodiscard]] DecodedWavResult decode_wav(const std::uint8_t* bytes,
                                          std::size_t size) noexcept {
  if (bytes == nullptr || size < 12U || !fourcc(bytes, 'R', 'I', 'F', 'F') ||
      !fourcc(bytes + 8U, 'W', 'A', 'V', 'E')) {
    return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
  }
  const auto riff_size = static_cast<std::uint64_t>(read_u32(bytes + 4U));
  const auto riff_end = riff_size + 8U;
  if (riff_end > static_cast<std::uint64_t>(size) || riff_end < 12U) {
    return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
  }

  const std::uint8_t* format = nullptr;
  std::size_t format_size = 0U;
  const std::uint8_t* samples = nullptr;
  std::size_t sample_bytes = 0U;
  std::size_t offset = 12U;
  const auto limit = static_cast<std::size_t>(riff_end);
  while (offset < limit) {
    if (limit - offset < 8U) {
      return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
    }
    const auto chunk_size =
        static_cast<std::size_t>(read_u32(bytes + offset + 4U));
    const auto chunk_offset = offset + 8U;
    if (chunk_size > limit - chunk_offset) {
      return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
    }
    if (fourcc(bytes + offset, 'f', 'm', 't', ' ')) {
      if (format != nullptr) {
        return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
      }
      format = bytes + chunk_offset;
      format_size = chunk_size;
    } else if (fourcc(bytes + offset, 'd', 'a', 't', 'a')) {
      if (samples != nullptr) {
        return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
      }
      samples = bytes + chunk_offset;
      sample_bytes = chunk_size;
    }
    const auto padded_size = chunk_size + (chunk_size & 1U);
    if (padded_size > limit - chunk_offset) {
      return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
    }
    offset = chunk_offset + padded_size;
  }
  if (offset != limit || format == nullptr || format_size < 16U ||
      samples == nullptr) {
    return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
  }

  const auto audio_format = read_u16(format);
  const auto channel_count = read_u16(format + 2U);
  const auto sample_rate = read_u32(format + 4U);
  const auto byte_rate = read_u32(format + 8U);
  const auto block_align = read_u16(format + 12U);
  const auto bits_per_sample = read_u16(format + 14U);
  const auto expected_align = static_cast<std::uint16_t>(channel_count * 2U);
  const auto expected_byte_rate =
      static_cast<std::uint64_t>(sample_rate) * expected_align;
  if (audio_format != 1U || (channel_count != 1U && channel_count != 2U) ||
      sample_rate == 0U || bits_per_sample != 16U ||
      block_align != expected_align || expected_byte_rate > UINT32_MAX ||
      byte_rate != static_cast<std::uint32_t>(expected_byte_rate)) {
    return DecodedWavResult{PlaybackSoundBankError::UnsupportedWav};
  }
  if (sample_bytes % block_align != 0U) {
    return DecodedWavResult{PlaybackSoundBankError::InvalidWav};
  }
  const auto frame_count = sample_bytes / block_align;
  auto result = Pcm16::allocate(sample_rate, frame_count);
  if (!result) {
    return DecodedWavResult{
        result.error() == PcmError::AllocationFailure
            ? PlaybackSoundBankError::AllocationFailure
            : PlaybackSoundBankError::SizeOverflow};
  }
  auto pcm = std::move(result).take_pcm();
  auto* left = pcm.mutable_channel_data(PcmChannel::Left);
  auto* right = pcm.mutable_channel_data(PcmChannel::Right);
  for (std::size_t frame = 0U; frame < frame_count; ++frame) {
    const auto source = frame * block_align;
    const auto left_bits = read_u16(samples + source);
    const auto left_value = left_bits <= 0x7fffU
                                ? static_cast<std::int32_t>(left_bits)
                                : static_cast<std::int32_t>(left_bits) - 65536;
    left[frame] = static_cast<Pcm16::Sample>(left_value);
    if (channel_count == 1U) {
      right[frame] = left[frame];
    } else {
      const auto right_bits = read_u16(samples + source + 2U);
      const auto right_value =
          right_bits <= 0x7fffU ? static_cast<std::int32_t>(right_bits)
                               : static_cast<std::int32_t>(right_bits) - 65536;
      right[frame] = static_cast<Pcm16::Sample>(right_value);
    }
  }
  return DecodedWavResult{std::move(pcm)};
}

[[nodiscard]] std::size_t asset_key(PlaybackSoundLanguage language,
                                    PlaybackSoundId id) noexcept {
  constexpr std::size_t kSoundsPerLanguage =
      static_cast<std::size_t>(PlaybackSoundId::Turn2) + 1U;
  return static_cast<std::size_t>(language) * kSoundsPerLanguage +
         static_cast<std::size_t>(id);
}

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

struct SoundTrigger final {
  std::uint64_t row;
  PlaybackSoundId sound;
  std::size_t order;
};

using EventLookup =
    std::unordered_map<std::uint64_t, const ChartRoleEvent*>;

void append_role_triggers(const ChartDraft& chart, PlaybackSoundConfig config,
                          StableRandom& random,
                          std::vector<SoundTrigger>& triggers) {
  EventLookup events;
  events.reserve(chart.role_events().size());
  for (const auto& event : chart.role_events()) {
    events.emplace(event.id.value, &event);
  }
  const auto append = [&triggers](const ChartRoleEvent& event,
                                  PlaybackSoundId sound) {
    triggers.push_back({event.row, sound, triggers.size()});
  };

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
          append(event, PlaybackSoundId::Turn1);
        }
      } else if (event.type == ChartRoleEventType::Note) {
        const auto choice = choose_note(random, config.dance_mode);
        note_choices.push_back(choice);
        append(event, choice.npc);
      } else if (event.type == ChartRoleEventType::MoveEnd) {
        const bool yeah = (random.next() & 1U) != 0U;
        end_choices.push_back(yeah);
        append(event, yeah ? PlaybackSoundId::M1Yeah
                           : PlaybackSoundId::M1Go);
      }
    }

    std::size_t note_index = 0U;
    std::size_t end_index = 0U;
    for (const auto id : group.player_event_ids) {
      const auto found = events.find(id.value);
      if (found == events.end() || found->second->role != ChartRole::Player) {
        continue;
      }
      const auto& event = *found->second;
      if (event.type == ChartRoleEventType::BlockStart) {
        if (config.transition_sounds_enabled) {
          append(event, PlaybackSoundId::Turn1);
        }
      } else if (event.type == ChartRoleEventType::Note) {
        if (note_index < note_choices.size()) {
          append(event, note_choices[note_index].player);
        }
        ++note_index;
      } else if (event.type == ChartRoleEventType::MoveEnd) {
        if (end_index < end_choices.size()) {
          append(event, end_choices[end_index] ? PlaybackSoundId::F1Yeah
                                               : PlaybackSoundId::F1Go);
        }
        ++end_index;
      }
    }
  }
}

void append_showtime_triggers(const ChartDraft& chart,
                              PlaybackSoundConfig config,
                              std::vector<SoundTrigger>& triggers) {
  if (config.transition_sounds_enabled) {
    for (const auto& event : chart.showtime_start_events()) {
      triggers.push_back(
          {event.row, PlaybackSoundId::Turn2, triggers.size()});
    }
  }
  for (const auto& event : chart.showtime_move_events()) {
    triggers.push_back(
        {event.row, PlaybackSoundId::SpaceBar, triggers.size()});
  }
}

[[nodiscard]] bool project_frame(std::uint64_t row, std::uint32_t sample_rate,
                                 TimingParameters timing,
                                 std::int64_t& frame) noexcept {
  const auto seconds = sixteenth_row_to_seconds(
      timing, static_cast<double>(row));
  if (!seconds) {
    return false;
  }
  const auto rounded =
      std::round(*seconds.value() * static_cast<double>(sample_rate));
  if (!std::isfinite(rounded) || rounded <= -kSignedFrameLimit ||
      rounded >= kSignedFrameLimit) {
    return false;
  }
  frame = static_cast<std::int64_t>(rounded);
  return true;
}

[[nodiscard]] bool subtract_frames(std::int64_t left, std::int64_t right,
                                   std::int64_t& result) noexcept {
  if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
      (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
    return false;
  }
  result = left - right;
  return true;
}

[[nodiscard]] std::int16_t read_sample_le(
    const std::vector<std::uint8_t>& bytes, std::size_t offset) noexcept {
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
  const auto value = bits <= 0x7fffU ? static_cast<std::int32_t>(bits)
                                    : static_cast<std::int32_t>(bits) - 65536;
  return static_cast<std::int16_t>(value);
}

void write_sample_le(std::vector<std::uint8_t>& bytes, std::size_t offset,
                     std::int16_t sample) noexcept {
  const auto bits = static_cast<std::uint16_t>(sample);
  bytes[offset] = static_cast<std::uint8_t>(bits & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>(bits >> 8U);
}

[[nodiscard]] std::int16_t saturating_add(std::int16_t left,
                                          std::int16_t right) noexcept {
  const auto sum = static_cast<std::int32_t>(left) +
                   static_cast<std::int32_t>(right);
  return static_cast<std::int16_t>(
      std::max<std::int32_t>(-32768, std::min<std::int32_t>(32767, sum)));
}

[[nodiscard]] bool resampled_frame_count(const Pcm16& clip,
                                         std::uint32_t target_rate,
                                         std::uint64_t& output) noexcept {
  const auto frames = static_cast<std::uint64_t>(clip.frame_count());
  if (frames == 0U) {
    output = 0U;
    return true;
  }
  if (frames > UINT64_MAX / target_rate) {
    return false;
  }
  const auto numerator = frames * target_rate;
  const auto rate = static_cast<std::uint64_t>(clip.sample_rate());
  output = numerator / rate + (numerator % rate == 0U ? 0U : 1U);
  return true;
}

[[nodiscard]] std::int16_t resampled_sample(const Pcm16& clip,
                                            PcmChannel channel,
                                            std::uint64_t output_frame,
                                            std::uint32_t target_rate) noexcept {
  const auto source_rate = static_cast<std::uint64_t>(clip.sample_rate());
  const auto numerator = output_frame * source_rate;
  const auto base = numerator / target_rate;
  const auto remainder = numerator % target_rate;
  const auto last = static_cast<std::uint64_t>(clip.frame_count() - 1U);
  const auto first_index = std::min(base, last);
  const auto second_index = std::min(first_index + 1U, last);
  const auto* samples = clip.channel_data(channel);
  const auto first = static_cast<std::int64_t>(samples[first_index]);
  const auto second = static_cast<std::int64_t>(samples[second_index]);
  const auto scaled = (second - first) * static_cast<std::int64_t>(remainder);
  const auto divisor = static_cast<std::int64_t>(target_rate);
  const auto rounded = scaled >= 0 ? (scaled + divisor / 2) / divisor
                                   : (scaled - divisor / 2) / divisor;
  return static_cast<std::int16_t>(first + rounded);
}

[[nodiscard]] PlaybackPcmError mix_trigger(
    std::vector<std::uint8_t>& output, std::uint64_t output_frames,
    std::uint32_t target_rate, std::int64_t destination_start,
    const Pcm16& clip) noexcept {
  std::uint64_t clip_frames = 0U;
  if (!resampled_frame_count(clip, target_rate, clip_frames)) {
    return PlaybackPcmError::SizeOverflow;
  }
  if (clip_frames == 0U ||
      (destination_start >= 0 &&
       static_cast<std::uint64_t>(destination_start) >= output_frames)) {
    return PlaybackPcmError::None;
  }

  std::uint64_t source_begin = 0U;
  std::uint64_t destination_begin = 0U;
  if (destination_start < 0) {
    const auto skipped = static_cast<std::uint64_t>(-(destination_start + 1)) +
                         1U;
    if (skipped >= clip_frames) {
      return PlaybackPcmError::None;
    }
    source_begin = skipped;
  } else {
    destination_begin = static_cast<std::uint64_t>(destination_start);
  }
  const auto available_clip = clip_frames - source_begin;
  const auto available_output = output_frames - destination_begin;
  const auto mixed_frames = std::min(available_clip, available_output);
  constexpr std::size_t kBytesPerFrame = 4U;
  for (std::uint64_t index = 0U; index < mixed_frames; ++index) {
    const auto destination = destination_begin + index;
    if (destination >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() /
                                   kBytesPerFrame)) {
      return PlaybackPcmError::SizeOverflow;
    }
    const auto byte_offset = static_cast<std::size_t>(destination) *
                             kBytesPerFrame;
    const auto source = source_begin + index;
    const auto left = resampled_sample(clip, PcmChannel::Left, source,
                                       target_rate);
    const auto right = resampled_sample(clip, PcmChannel::Right, source,
                                        target_rate);
    write_sample_le(output, byte_offset,
                    saturating_add(read_sample_le(output, byte_offset), left));
    write_sample_le(
        output, byte_offset + 2U,
        saturating_add(read_sample_le(output, byte_offset + 2U), right));
  }
  return PlaybackPcmError::None;
}

}  // namespace

PlaybackSoundBank::PlaybackSoundBank(
    std::vector<PlaybackSoundClip>&& clips) noexcept
    : clips_(std::move(clips)) {}

const Pcm16* PlaybackSoundBank::find(PlaybackSoundLanguage language,
                                     PlaybackSoundId id) const noexcept {
  for (const auto& clip : clips_) {
    if (clip.language == language && clip.id == id) {
      return &clip.pcm;
    }
  }
  return nullptr;
}

PlaybackSoundBankResult::PlaybackSoundBankResult(
    PlaybackSoundBank bank) noexcept
    : storage_(std::move(bank)) {}

PlaybackSoundBankResult::PlaybackSoundBankResult(
    PlaybackSoundBankError error) noexcept
    : storage_(error) {}

PlaybackSoundBankResult::operator bool() const noexcept {
  return std::holds_alternative<PlaybackSoundBank>(storage_);
}

const PlaybackSoundBank* PlaybackSoundBankResult::bank() const noexcept {
  return std::get_if<PlaybackSoundBank>(&storage_);
}

PlaybackSoundBankError PlaybackSoundBankResult::error() const noexcept {
  const auto* error = std::get_if<PlaybackSoundBankError>(&storage_);
  return error == nullptr ? PlaybackSoundBankError::None : *error;
}

PlaybackSoundBank PlaybackSoundBankResult::take_bank() && {
  return std::move(std::get<PlaybackSoundBank>(storage_));
}

PlaybackSoundBankResult decode_playback_sound_bank(
    const PlaybackSoundAsset* assets, std::size_t asset_count) noexcept {
  if (asset_count != 0U && assets == nullptr) {
    return PlaybackSoundBankResult{PlaybackSoundBankError::InvalidAsset};
  }
  constexpr std::size_t kAssetKeyCount =
      2U * (static_cast<std::size_t>(PlaybackSoundId::Turn2) + 1U);
  std::array<bool, kAssetKeyCount> seen{};
  std::vector<PlaybackSoundClip> clips;
  try {
    clips.reserve(asset_count);
    for (std::size_t index = 0U; index < asset_count; ++index) {
      const auto& asset = assets[index];
      if (!valid_language(asset.language) || !valid_sound_id(asset.id) ||
          asset.wav_bytes == nullptr) {
        return PlaybackSoundBankResult{PlaybackSoundBankError::InvalidAsset};
      }
      const auto key = asset_key(asset.language, asset.id);
      if (seen[key]) {
        return PlaybackSoundBankResult{PlaybackSoundBankError::DuplicateAsset};
      }
      seen[key] = true;
      auto decoded = decode_wav(asset.wav_bytes, asset.wav_size);
      if (!decoded) {
        return PlaybackSoundBankResult{decoded.error()};
      }
      clips.push_back(
          {asset.language, asset.id, std::move(decoded).take_pcm()});
    }
  } catch (const std::bad_alloc&) {
    return PlaybackSoundBankResult{PlaybackSoundBankError::AllocationFailure};
  } catch (const std::length_error&) {
    return PlaybackSoundBankResult{PlaybackSoundBankError::SizeOverflow};
  }
  return PlaybackSoundBankResult{PlaybackSoundBank{std::move(clips)}};
}

PlaybackPcmResult render_chart_playback_pcm(
    const Pcm16& song, const ChartDraft& chart,
    const PlaybackSoundBank& sound_bank, PlaybackSoundConfig config,
    double timeline_start_seconds) noexcept {
  if (!valid_language(config.language) ||
      !valid_dance_mode(config.dance_mode)) {
    return PlaybackPcmResult{PlaybackPcmError::InvalidSoundConfiguration};
  }
  auto rendered = render_playback_pcm(song, timeline_start_seconds);
  if (!rendered) {
    return rendered;
  }

  try {
    std::vector<SoundTrigger> triggers;
    triggers.reserve(chart.role_events().size() +
                     chart.showtime_start_events().size() +
                     chart.showtime_move_events().size());
    StableRandom random{config.random_seed};
    append_role_triggers(chart, config, random, triggers);
    append_showtime_triggers(chart, config, triggers);
    if (triggers.empty()) {
      return rendered;
    }

    struct ProjectedTrigger final {
      std::int64_t frame;
      PlaybackSoundId sound;
      std::size_t order;
    };
    std::vector<ProjectedTrigger> projected;
    projected.reserve(triggers.size());
    const auto sample_rate = rendered.pcm()->sample_rate();
    for (const auto& trigger : triggers) {
      std::int64_t frame = 0;
      if (!project_frame(trigger.row, sample_rate, config.timing, frame)) {
        return PlaybackPcmResult{PlaybackPcmError::SoundTimingFailure};
      }
      projected.push_back({frame, trigger.sound, trigger.order});
    }
    std::stable_sort(projected.begin(), projected.end(),
                     [](const ProjectedTrigger& left,
                        const ProjectedTrigger& right) {
                       if (left.frame != right.frame) {
                         return left.frame < right.frame;
                       }
                       return left.order < right.order;
                     });

    const auto rounded_start = std::round(
        timeline_start_seconds * static_cast<double>(sample_rate));
    const auto start_frame = static_cast<std::int64_t>(rounded_start);
    auto pcm = std::move(rendered).take_pcm();
    const auto output_frames = pcm.frame_count();
    auto output = std::move(pcm).take_bytes();
    for (const auto& trigger : projected) {
      const auto* clip = sound_bank.find(config.language, trigger.sound);
      if (clip == nullptr) {
        return PlaybackPcmResult{PlaybackPcmError::MissingSoundAsset};
      }
      std::int64_t destination = 0;
      if (!subtract_frames(trigger.frame, start_frame, destination)) {
        return PlaybackPcmResult{PlaybackPcmError::SoundTimingFailure};
      }
      const auto error = mix_trigger(output, output_frames, sample_rate,
                                     destination, *clip);
      if (error != PlaybackPcmError::None) {
        return PlaybackPcmResult{error};
      }
    }
    return PlaybackPcmResult{PlaybackPcm{
        sample_rate, output_frames, std::move(output)}};
  } catch (const std::bad_alloc&) {
    return PlaybackPcmResult{PlaybackPcmError::AllocationFailure};
  } catch (const std::length_error&) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }
}

PlaybackPcmResult render_rhythm_test_playback_pcm(
    const Pcm16& song, const PlaybackSoundBank& sound_bank,
    RhythmTestSoundConfig config, double timeline_start_seconds) noexcept {
  if (!valid_language(config.language)) {
    return PlaybackPcmResult{PlaybackPcmError::InvalidSoundConfiguration};
  }
  auto rendered = render_playback_pcm(song, timeline_start_seconds);
  if (!rendered) {
    return rendered;
  }
  const auto* clip =
      sound_bank.find(config.language, PlaybackSoundId::SpaceBar);
  if (clip == nullptr) {
    return PlaybackPcmResult{PlaybackPcmError::MissingSoundAsset};
  }

  const auto song_duration = static_cast<double>(song.frame_count()) /
                             static_cast<double>(song.sample_rate());
  const auto last_row_result =
      seconds_to_sixteenth_row(config.timing, song_duration);
  if (!last_row_result ||
      *last_row_result.value() >=
          static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    return PlaybackPcmResult{PlaybackPcmError::SoundTimingFailure};
  }
  if (*last_row_result.value() < 0.0) {
    return rendered;
  }
  constexpr std::uint64_t kRowsPerMeasure = 16U;
  const auto last_row =
      static_cast<std::uint64_t>(std::floor(*last_row_result.value()));
  const auto last_measure_row =
      last_row - (last_row % kRowsPerMeasure);

  try {
    const auto sample_rate = rendered.pcm()->sample_rate();
    const auto rounded_start = std::round(
        timeline_start_seconds * static_cast<double>(sample_rate));
    const auto start_frame = static_cast<std::int64_t>(rounded_start);
    auto pcm = std::move(rendered).take_pcm();
    const auto output_frames = pcm.frame_count();
    auto output = std::move(pcm).take_bytes();
    for (std::uint64_t row = 0U;; row += kRowsPerMeasure) {
      std::int64_t trigger_frame = 0;
      if (!project_frame(row, sample_rate, config.timing, trigger_frame)) {
        return PlaybackPcmResult{PlaybackPcmError::SoundTimingFailure};
      }
      std::int64_t destination = 0;
      if (!subtract_frames(trigger_frame, start_frame, destination)) {
        return PlaybackPcmResult{PlaybackPcmError::SoundTimingFailure};
      }
      const auto error = mix_trigger(output, output_frames, sample_rate,
                                     destination, *clip);
      if (error != PlaybackPcmError::None) {
        return PlaybackPcmResult{error};
      }
      if (row == last_measure_row) {
        break;
      }
    }
    return PlaybackPcmResult{PlaybackPcm{
        sample_rate, output_frames, std::move(output)}};
  } catch (const std::bad_alloc&) {
    return PlaybackPcmResult{PlaybackPcmError::AllocationFailure};
  } catch (const std::length_error&) {
    return PlaybackPcmResult{PlaybackPcmError::SizeOverflow};
  }
}

}  // namespace otgy::core
