// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/audio_source_info.hpp"
#include "av_core/audio/pcm16.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace otgy::core {

/// Stable failures returned by the in-memory MP3 decoder.
enum class Mp3DecodeError : std::uint8_t {
  None = 0,
  NullInput = 1,
  EmptyInput = 2,
  InputTooLarge = 3,
  InvalidData = 4,
  StreamFormatChanged = 5,
  SizeOverflow = 6,
  AllocationFailure = 7,
};

[[nodiscard]] constexpr std::string_view mp3_decode_error_message(
    Mp3DecodeError error) noexcept {
  switch (error) {
    case Mp3DecodeError::None:
      return {};
    case Mp3DecodeError::NullInput:
      return "MP3 input must not be null when its size is non-zero";
    case Mp3DecodeError::EmptyInput:
      return "MP3 input is empty";
    case Mp3DecodeError::InputTooLarge:
      return "MP3 input exceeds the decoder buffer limit";
    case Mp3DecodeError::InvalidData:
      return "input does not contain a complete decodable MP3 stream";
    case Mp3DecodeError::StreamFormatChanged:
      return "MP3 sample rate or channel layout changes between frames";
    case Mp3DecodeError::SizeOverflow:
      return "decoded MP3 dimensions exceed the representable size";
    case Mp3DecodeError::AllocationFailure:
      return "unable to allocate decoded MP3 samples";
  }
  return "unknown MP3 decode error";
}

class Mp3DecodeResult final {
 public:
  explicit Mp3DecodeResult(Pcm16 pcm,
                           AudioSourceInfo source_info = {
                               AudioSourceCodec::Mp3, 0U, 0U}) noexcept;
  explicit Mp3DecodeResult(Mp3DecodeError error) noexcept;

  [[nodiscard]] bool has_value() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return has_value();
  }

  [[nodiscard]] const Pcm16* pcm() const noexcept;
  [[nodiscard]] Pcm16* pcm() noexcept;
  [[nodiscard]] const AudioSourceInfo* source_info() const noexcept;
  [[nodiscard]] Mp3DecodeError error() const noexcept;

  /// Moves the canonical PCM out of a successful result.
  [[nodiscard]] Pcm16 take_pcm() &&;

 private:
  std::variant<Pcm16, Mp3DecodeError> storage_;
  AudioSourceInfo source_info_;
};

/// Decodes one complete caller-owned MP3 byte range in memory.
///
/// The returned PCM always uses canonical planar Left/Right PCM16 and keeps
/// the MP3 sample rate. Mono frames are duplicated into both channels. The
/// decoder never retains or modifies input, performs no resampling, and
/// returns no partial PCM on failure.
[[nodiscard]] Mp3DecodeResult decode_mp3(const std::uint8_t* data,
                                         std::size_t size) noexcept;

}  // namespace otgy::core
