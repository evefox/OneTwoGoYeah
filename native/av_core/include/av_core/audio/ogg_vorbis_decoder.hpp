// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/audio_source_info.hpp"
#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

/// Stable failures returned by the in-memory Ogg Vorbis decoder.
enum class OggVorbisDecodeError : std::uint8_t {
  None = 0,
  NullInput = 1,
  EmptyInput = 2,
  InputTooLarge = 3,
  InvalidData = 4,
  StreamFormatChanged = 5,
  SizeOverflow = 6,
  AllocationFailure = 7,
};

[[nodiscard]] constexpr std::string_view ogg_vorbis_decode_error_message(
    OggVorbisDecodeError error) noexcept {
  switch (error) {
    case OggVorbisDecodeError::None:
      return {};
    case OggVorbisDecodeError::NullInput:
      return "Ogg Vorbis input must not be null when its size is non-zero";
    case OggVorbisDecodeError::EmptyInput:
      return "Ogg Vorbis input is empty";
    case OggVorbisDecodeError::InputTooLarge:
      return "Ogg Vorbis input exceeds the decoder cursor limit";
    case OggVorbisDecodeError::InvalidData:
      return "input does not contain a complete decodable Ogg Vorbis stream";
    case OggVorbisDecodeError::StreamFormatChanged:
      return "Ogg Vorbis sample rate or channel count changes between links";
    case OggVorbisDecodeError::SizeOverflow:
      return "decoded Ogg Vorbis dimensions exceed the representable size";
    case OggVorbisDecodeError::AllocationFailure:
      return "unable to allocate decoded Ogg Vorbis samples";
  }
  return "unknown Ogg Vorbis decode error";
}

class OggVorbisDecodeResult final {
 public:
  explicit OggVorbisDecodeResult(
      Pcm16 pcm,
      AudioSourceInfo source_info = {AudioSourceCodec::OggVorbis, 0U,
                                     0U}) noexcept;
  explicit OggVorbisDecodeResult(OggVorbisDecodeError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const Pcm16* pcm() const noexcept;
  [[nodiscard]] Pcm16* pcm() noexcept;
  [[nodiscard]] const AudioSourceInfo* source_info() const noexcept;
  [[nodiscard]] OggVorbisDecodeError error() const noexcept;

  /// Moves the canonical PCM out of a successful result.
  [[nodiscard]] Pcm16 take_pcm() &&;

 private:
  std::variant<Pcm16, OggVorbisDecodeError> storage_;
  AudioSourceInfo source_info_;
};

/// Decodes one complete caller-owned Ogg Vorbis byte range in memory.
///
/// The returned PCM always uses canonical planar Left/Right PCM16 and keeps
/// the source sample rate. Mono is duplicated into both channels; inputs with
/// more than two channels use only channels zero and one. The decoder never
/// retains or modifies input, performs no resampling, and returns no partial
/// PCM on failure.
[[nodiscard]] OggVorbisDecodeResult decode_ogg_vorbis(
    const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace otgy::core
