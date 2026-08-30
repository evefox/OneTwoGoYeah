// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/ogg_vorbis_decoder.hpp"

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

struct MemoryInput final {
  const std::uint8_t* data;
  std::size_t size;
  std::size_t position;
};

std::size_t read_memory(void* destination, std::size_t item_size,
                        std::size_t item_count, void* source) noexcept {
  if (destination == nullptr || source == nullptr || item_size == 0U ||
      item_count == 0U) {
    return 0U;
  }

  auto& input = *static_cast<MemoryInput*>(source);
  const auto available_items = (input.size - input.position) / item_size;
  const auto copied_items = std::min(item_count, available_items);
  const auto copied_bytes = copied_items * item_size;
  if (copied_bytes != 0U) {
    std::memcpy(destination, input.data + input.position, copied_bytes);
    input.position += copied_bytes;
  }
  return copied_items;
}

int seek_memory(void* source, ogg_int64_t offset, int origin) noexcept {
  if (source == nullptr) {
    return -1;
  }

  auto& input = *static_cast<MemoryInput*>(source);
  ogg_int64_t base = 0;
  switch (origin) {
    case SEEK_SET:
      break;
    case SEEK_CUR:
      base = static_cast<ogg_int64_t>(input.position);
      break;
    case SEEK_END:
      base = static_cast<ogg_int64_t>(input.size);
      break;
    default:
      return -1;
  }

  if ((offset > 0 &&
       base > std::numeric_limits<ogg_int64_t>::max() - offset) ||
      (offset < 0 &&
       base < std::numeric_limits<ogg_int64_t>::min() - offset)) {
    return -1;
  }
  const auto target = base + offset;
  if (target < 0 ||
      static_cast<std::uint64_t>(target) >
          static_cast<std::uint64_t>(input.size)) {
    return -1;
  }

  input.position = static_cast<std::size_t>(target);
  return 0;
}

long tell_memory(void* source) noexcept {
  if (source == nullptr) {
    return -1L;
  }
  const auto& input = *static_cast<const MemoryInput*>(source);
  if (input.position > static_cast<std::size_t>(LONG_MAX)) {
    return -1L;
  }
  return static_cast<long>(input.position);
}

class VorbisFile final {
 public:
  VorbisFile() = default;

  ~VorbisFile() {
    if (opened_) {
      (void)ov_clear(&file_);
    }
  }

  VorbisFile(const VorbisFile&) = delete;
  VorbisFile& operator=(const VorbisFile&) = delete;

  [[nodiscard]] bool open(MemoryInput* input) noexcept {
    const ov_callbacks callbacks{
        read_memory,
        seek_memory,
        nullptr,
        tell_memory,
    };
    opened_ =
        ov_open_callbacks(input, &file_, nullptr, 0L, callbacks) == 0;
    return opened_;
  }

  [[nodiscard]] OggVorbis_File* get() noexcept {
    return &file_;
  }

 private:
  OggVorbis_File file_{};
  bool opened_ = false;
};

enum class OggPageValidation : std::uint8_t {
  Complete,
  Invalid,
  AllocationFailure,
};

[[nodiscard]] OggPageValidation validate_complete_ogg_pages(
    const std::uint8_t* data, std::size_t size) noexcept {
  ogg_sync_state sync{};
  (void)ogg_sync_init(&sync);

  constexpr std::size_t kChunkSize = 4096U;
  std::size_t position = 0U;
  bool saw_page = false;
  bool final_page_is_eos = false;
  while (position < size) {
    const auto chunk_size = std::min(kChunkSize, size - position);
    auto* const buffer =
        ogg_sync_buffer(&sync, static_cast<long>(chunk_size));
    if (buffer == nullptr) {
      (void)ogg_sync_clear(&sync);
      return OggPageValidation::AllocationFailure;
    }
    std::memcpy(buffer, data + position, chunk_size);
    if (ogg_sync_wrote(&sync, static_cast<long>(chunk_size)) != 0) {
      (void)ogg_sync_clear(&sync);
      return OggPageValidation::Invalid;
    }
    position += chunk_size;

    while (true) {
      ogg_page page{};
      const auto page_result = ogg_sync_pageout(&sync, &page);
      if (page_result < 0) {
        (void)ogg_sync_clear(&sync);
        return OggPageValidation::Invalid;
      }
      if (page_result == 0) {
        break;
      }
      saw_page = true;
      final_page_is_eos = ogg_page_eos(&page) != 0;
    }
  }

  const auto has_unconsumed_bytes = sync.fill != sync.returned;
  (void)ogg_sync_clear(&sync);
  if (!saw_page || !final_page_is_eos || has_unconsumed_bytes) {
    return OggPageValidation::Invalid;
  }
  return OggPageValidation::Complete;
}

[[nodiscard]] OggVorbisDecodeError map_pcm_error(PcmError error) noexcept {
  switch (error) {
    case PcmError::SizeOverflow:
      return OggVorbisDecodeError::SizeOverflow;
    case PcmError::AllocationFailure:
      return OggVorbisDecodeError::AllocationFailure;
    case PcmError::None:
    case PcmError::InvalidSampleRate:
    case PcmError::ChannelFrameCountMismatch:
    case PcmError::NullChannelData:
      return OggVorbisDecodeError::InvalidData;
  }
  return OggVorbisDecodeError::InvalidData;
}

[[nodiscard]] Pcm16::Sample read_pcm16le(
    const std::uint8_t* bytes) noexcept {
  const auto bits = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[0]) |
      static_cast<std::uint16_t>(bytes[1]) << 8U);
  const auto value = bits < 0x8000U ? static_cast<std::int32_t>(bits)
                                    : static_cast<std::int32_t>(bits) - 0x10000;
  return static_cast<Pcm16::Sample>(value);
}

}  // namespace

OggVorbisDecodeResult::OggVorbisDecodeResult(Pcm16 pcm) noexcept
    : storage_(std::move(pcm)) {}

OggVorbisDecodeResult::OggVorbisDecodeResult(
    OggVorbisDecodeError error) noexcept
    : storage_(error) {}

bool OggVorbisDecodeResult::has_value() const noexcept {
  return std::holds_alternative<Pcm16>(storage_);
}

const Pcm16* OggVorbisDecodeResult::pcm() const noexcept {
  return std::get_if<Pcm16>(&storage_);
}

Pcm16* OggVorbisDecodeResult::pcm() noexcept {
  return std::get_if<Pcm16>(&storage_);
}

OggVorbisDecodeError OggVorbisDecodeResult::error() const noexcept {
  if (const auto* error = std::get_if<OggVorbisDecodeError>(&storage_)) {
    return *error;
  }
  return OggVorbisDecodeError::None;
}

Pcm16 OggVorbisDecodeResult::take_pcm() && {
  return std::move(std::get<Pcm16>(storage_));
}

OggVorbisDecodeResult decode_ogg_vorbis(const std::uint8_t* data,
                                        std::size_t size) noexcept {
  if (size != 0U && data == nullptr) {
    return OggVorbisDecodeResult{OggVorbisDecodeError::NullInput};
  }
  if (size == 0U) {
    return OggVorbisDecodeResult{OggVorbisDecodeError::EmptyInput};
  }
  if (size > static_cast<std::size_t>(LONG_MAX) ||
      size > static_cast<std::uint64_t>(
                 std::numeric_limits<ogg_int64_t>::max())) {
    return OggVorbisDecodeResult{OggVorbisDecodeError::InputTooLarge};
  }

  try {
    const auto page_validation = validate_complete_ogg_pages(data, size);
    if (page_validation == OggPageValidation::AllocationFailure) {
      return OggVorbisDecodeResult{
          OggVorbisDecodeError::AllocationFailure};
    }
    if (page_validation != OggPageValidation::Complete) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
    }

    MemoryInput input{data, size, 0U};
    VorbisFile decoder;
    if (!decoder.open(&input)) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
    }

    auto* const file = decoder.get();
    const auto link_count = ov_streams(file);
    if (link_count <= 0) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
    }

    const auto* const first_info = ov_info(file, 0);
    if (first_info == nullptr || first_info->rate <= 0L ||
        static_cast<std::uint64_t>(first_info->rate) >
            std::numeric_limits<std::uint32_t>::max() ||
        first_info->channels <= 0) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
    }
    const auto sample_rate = static_cast<std::uint32_t>(first_info->rate);
    const auto source_channels = first_info->channels;

    for (long link = 1L; link < link_count; ++link) {
      const auto* const info = ov_info(file, static_cast<int>(link));
      if (info == nullptr || info->rate != first_info->rate ||
          info->channels != source_channels) {
        return OggVorbisDecodeResult{
            OggVorbisDecodeError::StreamFormatChanged};
      }
    }

    const auto total_frames = ov_pcm_total(file, -1);
    if (total_frames < 0 ||
        static_cast<std::uint64_t>(total_frames) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::SizeOverflow};
    }
    const auto frame_count = static_cast<std::size_t>(total_frames);

    auto pcm_result = Pcm16::allocate(sample_rate, frame_count);
    if (!pcm_result) {
      return OggVorbisDecodeResult{map_pcm_error(pcm_result.error())};
    }
    Pcm16 pcm = std::move(pcm_result).take_pcm();

    constexpr std::size_t kFramesPerRead = 1024U;
    const auto bytes_per_frame =
        static_cast<std::size_t>(source_channels) * sizeof(Pcm16::Sample);
    if (bytes_per_frame >
        static_cast<std::size_t>(std::numeric_limits<int>::max()) /
            kFramesPerRead) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::SizeOverflow};
    }
    std::vector<std::uint8_t> interleaved(bytes_per_frame * kFramesPerRead);

    auto* const left = pcm.mutable_channel_data(PcmChannel::Left);
    auto* const right = pcm.mutable_channel_data(PcmChannel::Right);
    std::size_t written_frames = 0U;
    while (true) {
      int current_link = -1;
      const auto bytes_read = ov_read(
          file, reinterpret_cast<char*>(interleaved.data()),
          static_cast<int>(interleaved.size()), 0, 2, 1, &current_link);
      if (bytes_read == 0L) {
        break;
      }
      if (bytes_read < 0L || current_link < 0 ||
          current_link >= link_count ||
          static_cast<std::size_t>(bytes_read) % bytes_per_frame != 0U) {
        return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
      }

      const auto* const info = ov_info(file, current_link);
      if (info == nullptr || info->rate != first_info->rate ||
          info->channels != source_channels) {
        return OggVorbisDecodeResult{
            OggVorbisDecodeError::StreamFormatChanged};
      }

      const auto decoded_frames =
          static_cast<std::size_t>(bytes_read) / bytes_per_frame;
      if (decoded_frames > frame_count - written_frames) {
        return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
      }

      for (std::size_t frame = 0U; frame < decoded_frames; ++frame) {
        const auto* const frame_data =
            interleaved.data() + frame * bytes_per_frame;
        left[written_frames + frame] = read_pcm16le(frame_data);
        right[written_frames + frame] = read_pcm16le(
            frame_data + (source_channels == 1 ? 0U : sizeof(Pcm16::Sample)));
      }
      written_frames += decoded_frames;
    }

    if (written_frames != frame_count) {
      return OggVorbisDecodeResult{OggVorbisDecodeError::InvalidData};
    }
    return OggVorbisDecodeResult{std::move(pcm)};
  } catch (const std::length_error&) {
    return OggVorbisDecodeResult{OggVorbisDecodeError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return OggVorbisDecodeResult{OggVorbisDecodeError::AllocationFailure};
  }
}

}  // namespace otgy::core
