// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/ogg_vorbis_encoder.hpp"

#include "vorbis_export_config.hpp"

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace otgy::core {
namespace {

constexpr int kOggSerialNumber = 0x4F544759;
constexpr std::size_t kFramesPerChunk = 1024U;

class VorbisInfoState final {
 public:
  VorbisInfoState() noexcept { vorbis_info_init(&value); }
  ~VorbisInfoState() { vorbis_info_clear(&value); }
  VorbisInfoState(const VorbisInfoState&) = delete;
  VorbisInfoState& operator=(const VorbisInfoState&) = delete;
  vorbis_info value{};
};

class VorbisCommentState final {
 public:
  VorbisCommentState() noexcept { vorbis_comment_init(&value); }
  ~VorbisCommentState() { vorbis_comment_clear(&value); }
  VorbisCommentState(const VorbisCommentState&) = delete;
  VorbisCommentState& operator=(const VorbisCommentState&) = delete;
  vorbis_comment value{};
};

class VorbisDspState final {
 public:
  ~VorbisDspState() {
    if (initialized) {
      vorbis_dsp_clear(&value);
    }
  }
  VorbisDspState(const VorbisDspState&) = delete;
  VorbisDspState& operator=(const VorbisDspState&) = delete;
  VorbisDspState() = default;
  vorbis_dsp_state value{};
  bool initialized = false;
};

class VorbisBlockState final {
 public:
  ~VorbisBlockState() {
    if (initialized) {
      vorbis_block_clear(&value);
    }
  }
  VorbisBlockState(const VorbisBlockState&) = delete;
  VorbisBlockState& operator=(const VorbisBlockState&) = delete;
  VorbisBlockState() = default;
  vorbis_block value{};
  bool initialized = false;
};

class OggStreamState final {
 public:
  ~OggStreamState() {
    if (initialized) {
      ogg_stream_clear(&value);
    }
  }
  OggStreamState(const OggStreamState&) = delete;
  OggStreamState& operator=(const OggStreamState&) = delete;
  OggStreamState() = default;
  ogg_stream_state value{};
  bool initialized = false;
};

[[nodiscard]] bool valid_utf8_text(std::string_view text) noexcept {
  if (text.empty() || text.find('\0') != std::string_view::npos) {
    return false;
  }
  for (std::size_t offset = 0U; offset < text.size();) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80U) {
      ++offset;
      continue;
    }
    std::size_t count = 0U;
    std::uint32_t value = 0U;
    std::uint32_t minimum = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
      count = 2U;
      value = first & 0x1FU;
      minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      count = 3U;
      value = first & 0x0FU;
      minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      count = 4U;
      value = first & 0x07U;
      minimum = 0x10000U;
    } else {
      return false;
    }
    if (offset + count > text.size()) {
      return false;
    }
    for (std::size_t index = 1U; index < count; ++index) {
      const auto byte = static_cast<unsigned char>(text[offset + index]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      value = value << 6U | static_cast<std::uint32_t>(byte & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
      return false;
    }
    offset += count;
  }
  return true;
}

void add_comment(vorbis_comment& comments, char* key,
                 const std::string& value) noexcept {
  if (valid_utf8_text(value)) {
    vorbis_comment_add_tag(&comments, key,
                           const_cast<char*>(value.c_str()));
  }
}

[[nodiscard]] OggVorbisEncodeError append_page(
    std::vector<std::uint8_t>& output, const ogg_page& page) {
  if (page.header_len < 0 || page.body_len < 0) {
    return OggVorbisEncodeError::EncodingFailed;
  }
  const auto header_size = static_cast<std::size_t>(page.header_len);
  const auto body_size = static_cast<std::size_t>(page.body_len);
  if (header_size > output.max_size() - output.size() ||
      body_size > output.max_size() - output.size() - header_size) {
    return OggVorbisEncodeError::SizeOverflow;
  }
  if (header_size != 0U) {
    output.insert(output.end(), page.header, page.header + header_size);
  }
  if (body_size != 0U) {
    output.insert(output.end(), page.body, page.body + body_size);
  }
  return OggVorbisEncodeError::None;
}

[[nodiscard]] bool is_cancelled(OggVorbisCancelCallback callback,
                                void* context) noexcept {
  return callback != nullptr && callback(context);
}

void report_progress(OggVorbisProgressCallback callback, void* context,
                     std::size_t completed, std::size_t total) noexcept {
  if (callback != nullptr) {
    callback({completed, total}, context);
  }
}

}  // namespace

OggVorbisEncodeResult::OggVorbisEncodeResult(
    std::vector<std::uint8_t> bytes) noexcept
    : storage_(std::move(bytes)) {}

OggVorbisEncodeResult::OggVorbisEncodeResult(
    OggVorbisEncodeError error) noexcept
    : storage_(error) {}

OggVorbisEncodeResult::operator bool() const noexcept {
  return std::holds_alternative<std::vector<std::uint8_t>>(storage_);
}

const std::vector<std::uint8_t>* OggVorbisEncodeResult::bytes() const noexcept {
  return std::get_if<std::vector<std::uint8_t>>(&storage_);
}

OggVorbisEncodeError OggVorbisEncodeResult::error() const noexcept {
  const auto* error = std::get_if<OggVorbisEncodeError>(&storage_);
  return error == nullptr ? OggVorbisEncodeError::None : *error;
}

std::vector<std::uint8_t> OggVorbisEncodeResult::take_bytes() && {
  return std::move(std::get<std::vector<std::uint8_t>>(storage_));
}

OggVorbisEncodeResult encode_ogg_vorbis(
    const Pcm16& pcm, std::uint8_t quality,
    const OggVorbisMetadata& metadata,
    OggVorbisProgressCallback progress_callback,
    OggVorbisCancelCallback cancel_callback,
    void* callback_context) noexcept {
  if (quality < 1U || quality > 5U) {
    return OggVorbisEncodeResult{OggVorbisEncodeError::InvalidQuality};
  }
  if (!detail::valid_vorbis_export_sample_rate(pcm.sample_rate())) {
    return OggVorbisEncodeResult{
        OggVorbisEncodeError::UnsupportedSampleRate};
  }
  if (is_cancelled(cancel_callback, callback_context)) {
    return OggVorbisEncodeResult{OggVorbisEncodeError::Cancelled};
  }

  try {
    VorbisInfoState info;
    const auto quality_value = detail::vorbis_export_quality_value(quality);
    if (vorbis_encode_init_vbr(&info.value, 2L,
                               static_cast<long>(pcm.sample_rate()),
                               quality_value) != 0) {
      return OggVorbisEncodeResult{
          OggVorbisEncodeError::UnsupportedSampleRate};
    }

    VorbisCommentState comments;
    char title_key[] = "TITLE";
    char artist_key[] = "ARTIST";
    char album_key[] = "ALBUM";
    add_comment(comments.value, title_key, metadata.title);
    add_comment(comments.value, artist_key, metadata.artist);
    add_comment(comments.value, album_key, metadata.album);

    VorbisDspState dsp;
    if (vorbis_analysis_init(&dsp.value, &info.value) != 0) {
      return OggVorbisEncodeResult{
          OggVorbisEncodeError::EncoderInitializationFailed};
    }
    dsp.initialized = true;

    VorbisBlockState block;
    if (vorbis_block_init(&dsp.value, &block.value) != 0) {
      return OggVorbisEncodeResult{
          OggVorbisEncodeError::EncoderInitializationFailed};
    }
    block.initialized = true;

    OggStreamState stream;
    if (ogg_stream_init(&stream.value, kOggSerialNumber) != 0) {
      return OggVorbisEncodeResult{
          OggVorbisEncodeError::AllocationFailure};
    }
    stream.initialized = true;

    ogg_packet identification{};
    ogg_packet comment{};
    ogg_packet setup{};
    if (vorbis_analysis_headerout(&dsp.value, &comments.value,
                                  &identification, &comment, &setup) != 0 ||
        ogg_stream_packetin(&stream.value, &identification) != 0 ||
        ogg_stream_packetin(&stream.value, &comment) != 0 ||
        ogg_stream_packetin(&stream.value, &setup) != 0) {
      return OggVorbisEncodeResult{OggVorbisEncodeError::EncodingFailed};
    }

    std::vector<std::uint8_t> output;
    while (true) {
      ogg_page page{};
      const auto flushed = ogg_stream_flush(&stream.value, &page);
      if (flushed < 0) {
        return OggVorbisEncodeResult{OggVorbisEncodeError::EncodingFailed};
      }
      if (flushed == 0) {
        break;
      }
      const auto append_error = append_page(output, page);
      if (append_error != OggVorbisEncodeError::None) {
        return OggVorbisEncodeResult{append_error};
      }
    }

    const auto* const left = pcm.channel_data(PcmChannel::Left);
    const auto* const right = pcm.channel_data(PcmChannel::Right);
    const auto total_frames = pcm.frame_count();
    std::size_t submitted_frames = 0U;
    bool end_signalled = false;
    bool end_of_stream = false;
    report_progress(progress_callback, callback_context, 0U, total_frames);

    while (!end_of_stream) {
      if (is_cancelled(cancel_callback, callback_context)) {
        return OggVorbisEncodeResult{OggVorbisEncodeError::Cancelled};
      }

      if (submitted_frames < total_frames) {
        const auto frame_count =
            std::min(kFramesPerChunk, total_frames - submitted_frames);
        auto** const buffer = vorbis_analysis_buffer(
            &dsp.value, static_cast<int>(frame_count));
        if (buffer == nullptr) {
          return OggVorbisEncodeResult{
              OggVorbisEncodeError::AllocationFailure};
        }
        for (std::size_t frame = 0U; frame < frame_count; ++frame) {
          buffer[0][frame] = static_cast<float>(
              left[submitted_frames + frame] / 32768.0F);
          buffer[1][frame] = static_cast<float>(
              right[submitted_frames + frame] / 32768.0F);
        }
        if (vorbis_analysis_wrote(&dsp.value,
                                  static_cast<int>(frame_count)) != 0) {
          return OggVorbisEncodeResult{
              OggVorbisEncodeError::EncodingFailed};
        }
        submitted_frames += frame_count;
        report_progress(progress_callback, callback_context, submitted_frames,
                        total_frames);
      } else if (!end_signalled) {
        if (vorbis_analysis_wrote(&dsp.value, 0) != 0) {
          return OggVorbisEncodeResult{
              OggVorbisEncodeError::EncodingFailed};
        }
        end_signalled = true;
      }

      bool processed_block = false;
      while (true) {
        const auto block_available =
            vorbis_analysis_blockout(&dsp.value, &block.value);
        if (block_available < 0) {
          return OggVorbisEncodeResult{
              OggVorbisEncodeError::EncodingFailed};
        }
        if (block_available == 0) {
          break;
        }
        processed_block = true;
        if (vorbis_analysis(&block.value, nullptr) != 0 ||
            vorbis_bitrate_addblock(&block.value) != 0) {
          return OggVorbisEncodeResult{
              OggVorbisEncodeError::EncodingFailed};
        }

        while (true) {
          ogg_packet packet{};
          const auto packet_available =
              vorbis_bitrate_flushpacket(&dsp.value, &packet);
          if (packet_available < 0) {
            return OggVorbisEncodeResult{
                OggVorbisEncodeError::EncodingFailed};
          }
          if (packet_available == 0) {
            break;
          }
          if (ogg_stream_packetin(&stream.value, &packet) != 0) {
            return OggVorbisEncodeResult{
                OggVorbisEncodeError::EncodingFailed};
          }

          while (true) {
            ogg_page page{};
            const auto page_available =
                ogg_stream_pageout(&stream.value, &page);
            if (page_available < 0) {
              return OggVorbisEncodeResult{
                  OggVorbisEncodeError::EncodingFailed};
            }
            if (page_available == 0) {
              break;
            }
            const auto append_error = append_page(output, page);
            if (append_error != OggVorbisEncodeError::None) {
              return OggVorbisEncodeResult{append_error};
            }
            if (ogg_page_eos(&page) != 0) {
              end_of_stream = true;
            }
          }
        }
      }

      if (end_signalled && !end_of_stream && !processed_block) {
        return OggVorbisEncodeResult{OggVorbisEncodeError::EncodingFailed};
      }
    }

    if (is_cancelled(cancel_callback, callback_context)) {
      return OggVorbisEncodeResult{OggVorbisEncodeError::Cancelled};
    }
    return OggVorbisEncodeResult{std::move(output)};
  } catch (const std::length_error&) {
    return OggVorbisEncodeResult{OggVorbisEncodeError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return OggVorbisEncodeResult{OggVorbisEncodeError::AllocationFailure};
  }
}

}  // namespace otgy::core
