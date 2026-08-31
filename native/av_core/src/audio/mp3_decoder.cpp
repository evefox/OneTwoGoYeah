// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/mp3_decoder.hpp"

#include <mad.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

class MadDecoderState final {
 public:
  MadDecoderState() noexcept {
    mad_stream_init(&stream);
    mad_frame_init(&frame);
    mad_synth_init(&synth);
  }

  ~MadDecoderState() {
    mad_synth_finish(&synth);
    mad_frame_finish(&frame);
    mad_stream_finish(&stream);
  }

  MadDecoderState(const MadDecoderState&) = delete;
  MadDecoderState& operator=(const MadDecoderState&) = delete;

  mad_stream stream{};
  mad_frame frame{};
  mad_synth synth{};
};

enum class XingKind : std::uint8_t {
  None,
  Xing,
  Info,
};

[[nodiscard]] XingKind parse_xing_header(mad_bitptr bits,
                                         unsigned int bit_count) noexcept {
  constexpr unsigned long kXingMagic =
      ('X' << 24U) | ('i' << 16U) | ('n' << 8U) | 'g';
  constexpr unsigned long kInfoMagic =
      ('I' << 24U) | ('n' << 16U) | ('f' << 8U) | 'o';
  constexpr unsigned long kFrames = 0x00000001UL;
  constexpr unsigned long kBytes = 0x00000002UL;
  constexpr unsigned long kToc = 0x00000004UL;
  constexpr unsigned long kScale = 0x00000008UL;

  if (bit_count < 64U) {
    return XingKind::None;
  }

  const auto magic = mad_bit_read(&bits, 32U);
  const auto kind = magic == kXingMagic
                        ? XingKind::Xing
                        : (magic == kInfoMagic ? XingKind::Info
                                               : XingKind::None);
  if (kind == XingKind::None) {
    return kind;
  }

  const auto flags = mad_bit_read(&bits, 32U);
  bit_count -= 64U;

  const auto consume = [&bits, &bit_count](unsigned int count) noexcept {
    if (bit_count < count) {
      return false;
    }
    while (count != 0U) {
      const auto chunk = std::min(count, 32U);
      (void)mad_bit_read(&bits, chunk);
      bit_count -= chunk;
      count -= chunk;
    }
    return true;
  };

  if ((flags & kFrames) != 0U && !consume(32U)) {
    return XingKind::None;
  }
  if ((flags & kBytes) != 0U && !consume(32U)) {
    return XingKind::None;
  }
  if ((flags & kToc) != 0U && !consume(800U)) {
    return XingKind::None;
  }
  if ((flags & kScale) != 0U && !consume(32U)) {
    return XingKind::None;
  }
  return kind;
}

[[nodiscard]] std::size_t leading_id3_tag_size(
    const unsigned char* data, std::size_t available) noexcept {
  if (available >= 3U && data[0] == 'T' && data[1] == 'A' &&
      data[2] == 'G') {
    return 128U;
  }

  if (available < 10U || data[0] != 'I' || data[1] != 'D' ||
      data[2] != '3' || data[3] == 0xffU || data[4] == 0xffU ||
      data[6] >= 0x80U || data[7] >= 0x80U || data[8] >= 0x80U ||
      data[9] >= 0x80U) {
    return 0U;
  }

  const std::size_t payload_size =
      (static_cast<std::size_t>(data[6]) << 21U) |
      (static_cast<std::size_t>(data[7]) << 14U) |
      (static_cast<std::size_t>(data[8]) << 7U) |
      static_cast<std::size_t>(data[9]);
  const std::size_t footer_size = (data[5] & 0x10U) != 0U ? 10U : 0U;
  return 10U + payload_size + footer_size;
}

[[nodiscard]] Pcm16::Sample scale_sample(mad_fixed_t sample) noexcept {
  // This is the exact fixed-point rounding and clipping order used by the
  // ArrowVortex reference decoder.
  sample += (1L << (MAD_F_FRACBITS - 16));
  if (sample >= MAD_F_ONE) {
    sample = MAD_F_ONE - 1;
  } else if (sample < -MAD_F_ONE) {
    sample = -MAD_F_ONE;
  }
  return static_cast<Pcm16::Sample>(
      sample >> (MAD_F_FRACBITS + 1 - 16));
}

[[nodiscard]] Mp3DecodeError map_pcm_error(PcmError error) noexcept {
  switch (error) {
    case PcmError::SizeOverflow:
      return Mp3DecodeError::SizeOverflow;
    case PcmError::AllocationFailure:
      return Mp3DecodeError::AllocationFailure;
    case PcmError::None:
    case PcmError::InvalidSampleRate:
    case PcmError::ChannelFrameCountMismatch:
    case PcmError::NullChannelData:
      return Mp3DecodeError::InvalidData;
  }
  return Mp3DecodeError::InvalidData;
}

[[nodiscard]] bool can_append_samples(
    const std::vector<Pcm16::Sample>& samples,
    std::size_t additional) noexcept {
  return additional <= samples.max_size() - samples.size();
}

}  // namespace

Mp3DecodeResult::Mp3DecodeResult(Pcm16 pcm,
                                 AudioSourceInfo source_info) noexcept
    : storage_(std::move(pcm)), source_info_(source_info) {}

Mp3DecodeResult::Mp3DecodeResult(Mp3DecodeError error) noexcept
    : storage_(error), source_info_{AudioSourceCodec::Mp3, 0U, 0U} {}

bool Mp3DecodeResult::has_value() const noexcept {
  return std::holds_alternative<Pcm16>(storage_);
}

const Pcm16* Mp3DecodeResult::pcm() const noexcept {
  return std::get_if<Pcm16>(&storage_);
}

Pcm16* Mp3DecodeResult::pcm() noexcept {
  return std::get_if<Pcm16>(&storage_);
}

const AudioSourceInfo* Mp3DecodeResult::source_info() const noexcept {
  return has_value() ? &source_info_ : nullptr;
}

Mp3DecodeError Mp3DecodeResult::error() const noexcept {
  if (const auto* error = std::get_if<Mp3DecodeError>(&storage_)) {
    return *error;
  }
  return Mp3DecodeError::None;
}

Pcm16 Mp3DecodeResult::take_pcm() && {
  return std::move(std::get<Pcm16>(storage_));
}

Mp3DecodeResult decode_mp3(const std::uint8_t* data,
                           std::size_t size) noexcept {
  if (size != 0U && data == nullptr) {
    return Mp3DecodeResult{Mp3DecodeError::NullInput};
  }
  if (size == 0U) {
    return Mp3DecodeResult{Mp3DecodeError::EmptyInput};
  }

  constexpr std::size_t kGuardSize = MAD_BUFFER_GUARD;
  const auto mad_length_limit =
      static_cast<std::size_t>(std::numeric_limits<unsigned long>::max());
  const auto input_allocation_limit =
      std::vector<unsigned char>{}.max_size();
  if (size > mad_length_limit - kGuardSize ||
      size > std::numeric_limits<std::size_t>::max() - kGuardSize ||
      input_allocation_limit < kGuardSize ||
      size > input_allocation_limit - kGuardSize) {
    return Mp3DecodeResult{Mp3DecodeError::InputTooLarge};
  }

  try {
    const auto guarded_size = size + kGuardSize;
    std::vector<unsigned char> guarded(guarded_size, 0U);
    std::copy_n(data, size, guarded.data());

    MadDecoderState decoder;
    mad_stream_buffer(&decoder.stream, guarded.data(),
                      static_cast<unsigned long>(guarded.size()));

    std::vector<Pcm16::Sample> left;
    std::vector<Pcm16::Sample> right;
    std::uint32_t sample_rate = 0U;
    unsigned int source_channels = 0U;
    bool is_first_frame = true;
    std::uint64_t bitrate_sample_sum = 0U;

    while (true) {
      auto decoded = mad_frame_decode(&decoder.frame, &decoder.stream);
      if (decoded == -1 && decoder.stream.error == MAD_ERROR_BUFLEN) {
        break;
      }

      if (decoded == -1 && decoder.stream.error == MAD_ERROR_LOSTSYNC &&
          decoder.stream.this_frame != nullptr) {
        const auto remaining = static_cast<std::size_t>(
            decoder.stream.bufend - decoder.stream.this_frame);
        const auto tag_size =
            leading_id3_tag_size(decoder.stream.this_frame, remaining);
        if (tag_size != 0U && tag_size <= mad_length_limit) {
          mad_stream_skip(&decoder.stream,
                          static_cast<unsigned long>(tag_size));
          continue;
        }
      }

      if (decoded == -1 && decoder.stream.error == MAD_ERROR_BADDATAPTR) {
        // ArrowVortex treats a valid frame lacking Layer III bit reservoir
        // history as decoded silence, matching its historical playback path.
        decoded = 0;
      }

      if (decoded == 0) {
        if (is_first_frame) {
          const auto xing = parse_xing_header(decoder.stream.anc_ptr,
                                              decoder.stream.anc_bitlen);
          if (xing == XingKind::Xing) {
            continue;
          }
          is_first_frame = false;
        }

        const auto frame_rate =
            static_cast<std::uint32_t>(decoder.frame.header.samplerate);
        const auto frame_channels =
            static_cast<unsigned int>(MAD_NCHANNELS(&decoder.frame.header));
        if (sample_rate == 0U) {
          if (frame_rate == 0U ||
              (frame_channels != 1U && frame_channels != 2U)) {
            return Mp3DecodeResult{Mp3DecodeError::InvalidData};
          }
          sample_rate = frame_rate;
          source_channels = frame_channels;
        } else if (sample_rate != frame_rate ||
                   source_channels != frame_channels) {
          return Mp3DecodeResult{Mp3DecodeError::StreamFormatChanged};
        }

        mad_synth_frame(&decoder.synth, &decoder.frame);
        const auto frame_count =
            static_cast<std::size_t>(decoder.synth.pcm.length);
        if (!can_append_samples(left, frame_count) ||
            !can_append_samples(right, frame_count)) {
          return Mp3DecodeResult{Mp3DecodeError::SizeOverflow};
        }

        const auto frame_bitrate =
            static_cast<std::uint64_t>(decoder.frame.header.bitrate);
        if (frame_count != 0U &&
            frame_bitrate > std::numeric_limits<std::uint64_t>::max() /
                                frame_count) {
          return Mp3DecodeResult{Mp3DecodeError::SizeOverflow};
        }
        const auto weighted_bitrate = frame_bitrate * frame_count;
        if (weighted_bitrate >
            std::numeric_limits<std::uint64_t>::max() - bitrate_sample_sum) {
          return Mp3DecodeResult{Mp3DecodeError::SizeOverflow};
        }
        bitrate_sample_sum += weighted_bitrate;

        const auto old_size = left.size();
        left.resize(old_size + frame_count);
        right.resize(old_size + frame_count);
        for (std::size_t index = 0U; index < frame_count; ++index) {
          left[old_size + index] =
              scale_sample(decoder.synth.pcm.samples[0][index]);
          right[old_size + index] = scale_sample(
              decoder.synth.pcm.samples[source_channels == 1U ? 0U : 1U]
                                      [index]);
        }
        continue;
      }

      if (decoder.stream.error == MAD_ERROR_BADCRC) {
        mad_frame_mute(&decoder.frame);
        mad_synth_mute(&decoder.synth);
        continue;
      }
      if (!MAD_RECOVERABLE(decoder.stream.error)) {
        return Mp3DecodeResult{Mp3DecodeError::InvalidData};
      }
    }

    if (left.empty() || sample_rate == 0U) {
      return Mp3DecodeResult{Mp3DecodeError::InvalidData};
    }

    auto pcm = Pcm16::copy_from_channels(sample_rate, left.data(), left.size(),
                                         right.data(), right.size());
    if (!pcm) {
      return Mp3DecodeResult{map_pcm_error(pcm.error())};
    }
    const auto measured_bitrate =
        bitrate_sample_sum / static_cast<std::uint64_t>(left.size());
    return Mp3DecodeResult{
        std::move(pcm).take_pcm(),
        {AudioSourceCodec::Mp3, measured_bitrate, 0U}};
  } catch (const std::length_error&) {
    return Mp3DecodeResult{Mp3DecodeError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return Mp3DecodeResult{Mp3DecodeError::AllocationFailure};
  }
}

}  // namespace otgy::core
