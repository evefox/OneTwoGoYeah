// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

enum class OggVorbisEncodeError : std::uint8_t {
  None = 0,
  InvalidQuality = 1,
  UnsupportedSampleRate = 2,
  EncoderInitializationFailed = 3,
  EncodingFailed = 4,
  Cancelled = 5,
  SizeOverflow = 6,
  AllocationFailure = 7,
};

[[nodiscard]] constexpr std::string_view ogg_vorbis_encode_error_message(
    OggVorbisEncodeError error) noexcept {
  switch (error) {
    case OggVorbisEncodeError::None:
      return {};
    case OggVorbisEncodeError::InvalidQuality:
      return "Ogg Vorbis quality must be between 1 and 5";
    case OggVorbisEncodeError::UnsupportedSampleRate:
      return "PCM sample rate is not supported by the pinned Vorbis encoder";
    case OggVorbisEncodeError::EncoderInitializationFailed:
      return "unable to initialize the pinned Vorbis encoder";
    case OggVorbisEncodeError::EncodingFailed:
      return "pinned Vorbis encoding failed";
    case OggVorbisEncodeError::Cancelled:
      return "Ogg Vorbis encoding was cancelled";
    case OggVorbisEncodeError::SizeOverflow:
      return "encoded Ogg Vorbis output exceeds the representable size";
    case OggVorbisEncodeError::AllocationFailure:
      return "unable to allocate Ogg Vorbis encoder state";
  }
  return "unknown Ogg Vorbis encode error";
}

struct OggVorbisMetadata final {
  std::string title;
  std::string artist;
  std::string album;
};

struct OggVorbisEncodeProgress final {
  std::size_t completed_frames;
  std::size_t total_frames;
};

using OggVorbisProgressCallback = void (*)(OggVorbisEncodeProgress progress,
                                           void* context) noexcept;
using OggVorbisCancelCallback = bool (*)(void* context) noexcept;

class OggVorbisEncodeResult final {
 public:
  explicit OggVorbisEncodeResult(std::vector<std::uint8_t> bytes) noexcept;
  explicit OggVorbisEncodeResult(OggVorbisEncodeError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>* bytes() const noexcept;
  [[nodiscard]] OggVorbisEncodeError error() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> take_bytes() &&;

 private:
  std::variant<std::vector<std::uint8_t>, OggVorbisEncodeError> storage_;
};

/// Encodes canonical PCM with the pinned in-process libogg/libVorbis build.
///
/// Integer quality 1..5 maps exactly to libvorbis VBR 0.1f..0.5f. The Ogg
/// serial number, comment order, PCM conversion and page production are fixed
/// so identical inputs produce identical bytes. Cancellation is cooperative
/// and never returns a partial byte buffer.
[[nodiscard]] OggVorbisEncodeResult encode_ogg_vorbis(
    const Pcm16& pcm, std::uint8_t quality,
    const OggVorbisMetadata& metadata = {},
    OggVorbisProgressCallback progress_callback = nullptr,
    OggVorbisCancelCallback cancel_callback = nullptr,
    void* callback_context = nullptr) noexcept;

}  // namespace otgy::core
