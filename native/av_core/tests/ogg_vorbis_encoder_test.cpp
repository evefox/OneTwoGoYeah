// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/ogg_vorbis_encoder.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using otgy::core::OggVorbisEncodeError;
using otgy::core::OggVorbisEncodeProgress;
using otgy::core::Pcm16;
using otgy::core::PcmChannel;

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Pcm16 load_golden_input() {
  const auto bytes = read_file(OTGY_OGG_EXPORT_PCM_FIXTURE);
  constexpr std::size_t kFrameCount = 8'192U;
  constexpr std::uint32_t kSampleRate = 11'025U;
  assert(bytes.size() == kFrameCount * Pcm16::kChannelCount *
                             Pcm16::kBytesPerSample);

  auto result = Pcm16::allocate(kSampleRate, kFrameCount);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  for (std::size_t channel = 0U; channel < Pcm16::kChannelCount; ++channel) {
    auto* const samples = pcm.mutable_channel_data(
        channel == 0U ? PcmChannel::Left : PcmChannel::Right);
    const auto channel_offset = channel * kFrameCount * 2U;
    for (std::size_t frame = 0U; frame < kFrameCount; ++frame) {
      const auto byte_offset = channel_offset + frame * 2U;
      const auto bits = static_cast<std::uint16_t>(bytes[byte_offset]) |
                        static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(bytes[byte_offset + 1U])
                            << 8U);
      samples[frame] = static_cast<Pcm16::Sample>(bits);
    }
  }
  return pcm;
}

[[nodiscard]] std::string golden_path(std::uint8_t quality) {
  return std::string{OTGY_OGG_EXPORT_FIXTURE_DIR} +
         "/generated_stereo_11025.quality" + std::to_string(quality) +
         ".ogg";
}

void write_file(const std::string& path,
                const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output.is_open());
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  assert(output.good());
}

[[nodiscard]] Pcm16 make_pcm() {
  auto result = Pcm16::allocate(8'000U, 4'096U);
  assert(result);
  auto pcm = std::move(result).take_pcm();
  auto* const left = pcm.mutable_channel_data(PcmChannel::Left);
  auto* const right = pcm.mutable_channel_data(PcmChannel::Right);
  for (std::size_t frame = 0U; frame < pcm.frame_count(); ++frame) {
    left[frame] = static_cast<Pcm16::Sample>(
        static_cast<int>((frame * 97U) % 60'001U) - 30'000);
    right[frame] = static_cast<Pcm16::Sample>(
        static_cast<int>((frame * 193U + 7U) % 58'001U) - 29'000);
  }
  return pcm;
}

[[nodiscard]] bool contains_bytes(const std::vector<std::uint8_t>& bytes,
                                  std::string_view text) {
  return std::search(bytes.begin(), bytes.end(), text.begin(), text.end(),
                     [](std::uint8_t left, char right) {
                       return left == static_cast<std::uint8_t>(right);
                     }) !=
         bytes.end();
}

void test_deterministic_qualities_and_decode() {
  const auto pcm = make_pcm();
  for (const std::uint8_t quality : {1U, 3U, 5U}) {
    const auto first = otgy::core::encode_ogg_vorbis(pcm, quality);
    const auto second = otgy::core::encode_ogg_vorbis(pcm, quality);
    assert(first && second);
    assert(*first.bytes() == *second.bytes());
    assert(first.bytes()->size() > 4U);
    assert((*first.bytes())[0U] == 'O');
    assert((*first.bytes())[1U] == 'g');
    assert((*first.bytes())[2U] == 'g');
    assert((*first.bytes())[3U] == 'S');

    const auto decoded = otgy::core::decode_ogg_vorbis(
        first.bytes()->data(), first.bytes()->size());
    assert(decoded);
    assert(decoded.pcm()->sample_rate() == pcm.sample_rate());
    assert(decoded.pcm()->frame_count() == pcm.frame_count());
  }
}

void test_quality_goldens() {
  const auto pcm = load_golden_input();
  for (const std::uint8_t quality : {1U, 3U, 5U}) {
    const auto encoded = otgy::core::encode_ogg_vorbis(pcm, quality);
    assert(encoded);
    assert(*encoded.bytes() == read_file(golden_path(quality)));
  }
}

void generate_quality_goldens() {
  const auto pcm = load_golden_input();
  for (const std::uint8_t quality : {1U, 3U, 5U}) {
    const auto encoded = otgy::core::encode_ogg_vorbis(pcm, quality);
    assert(encoded);
    write_file(golden_path(quality), *encoded.bytes());
  }
}

void test_metadata_order_and_invalid_omission() {
  const auto pcm = make_pcm();
  const otgy::core::OggVorbisMetadata metadata{
      "标题", "Artist", "Album"};
  const auto result = otgy::core::encode_ogg_vorbis(pcm, 3U, metadata);
  assert(result);
  assert(contains_bytes(*result.bytes(), "TITLE=标题"));
  assert(contains_bytes(*result.bytes(), "ARTIST=Artist"));
  assert(contains_bytes(*result.bytes(), "ALBUM=Album"));

  constexpr std::string_view kTitle = "TITLE=";
  constexpr std::string_view kArtist = "ARTIST=";
  constexpr std::string_view kAlbum = "ALBUM=";
  const auto title = std::search(result.bytes()->begin(), result.bytes()->end(),
                                 kTitle.begin(), kTitle.end());
  const auto artist = std::search(result.bytes()->begin(), result.bytes()->end(),
                                  kArtist.begin(), kArtist.end());
  const auto album = std::search(result.bytes()->begin(), result.bytes()->end(),
                                 kAlbum.begin(), kAlbum.end());
  assert(title < artist && artist < album);

  const std::string invalid{"bad\xFF", 4U};
  const auto omitted = otgy::core::encode_ogg_vorbis(
      pcm, 3U, {invalid, {}, {}});
  assert(omitted);
  assert(!contains_bytes(*omitted.bytes(), "TITLE="));
}

struct CallbackState final {
  std::array<OggVorbisEncodeProgress, 16> progress{};
  std::size_t progress_count = 0U;
  std::size_t cancel_at;
};

void capture_progress(OggVorbisEncodeProgress progress,
                      void* context) noexcept {
  auto& state = *static_cast<CallbackState*>(context);
  assert(state.progress_count < state.progress.size());
  state.progress[state.progress_count++] = progress;
}

bool cancel_when_ready(void* context) noexcept {
  const auto& state = *static_cast<const CallbackState*>(context);
  return state.progress_count != 0U &&
         state.progress[state.progress_count - 1U].completed_frames >=
             state.cancel_at;
}

bool cancel_immediately(void*) noexcept {
  return true;
}

void test_progress_and_cancellation() {
  const auto pcm = make_pcm();
  CallbackState completed{};
  completed.cancel_at = std::numeric_limits<std::size_t>::max();
  const auto result = otgy::core::encode_ogg_vorbis(
      pcm, 3U, {}, capture_progress, cancel_when_ready, &completed);
  assert(result);
  assert(completed.progress_count != 0U);
  assert(completed.progress[0U].completed_frames == 0U);
  assert(completed.progress[completed.progress_count - 1U].completed_frames ==
         pcm.frame_count());
  for (std::size_t index = 0U; index < completed.progress_count; ++index) {
    assert(completed.progress[index].total_frames == pcm.frame_count());
  }

  CallbackState cancelled{};
  cancelled.cancel_at = 1'024U;
  const auto cancelled_result = otgy::core::encode_ogg_vorbis(
      pcm, 3U, {}, capture_progress, cancel_when_ready, &cancelled);
  assert(!cancelled_result);
  assert(cancelled_result.bytes() == nullptr);
  assert(cancelled_result.error() == OggVorbisEncodeError::Cancelled);

  const auto immediate = otgy::core::encode_ogg_vorbis(
      pcm, 3U, {}, nullptr, cancel_immediately, nullptr);
  assert(!immediate && immediate.error() == OggVorbisEncodeError::Cancelled);
}

void test_errors_and_ownership() {
  const auto pcm = make_pcm();
  assert(otgy::core::encode_ogg_vorbis(pcm, 0U).error() ==
         OggVorbisEncodeError::InvalidQuality);
  assert(otgy::core::encode_ogg_vorbis(pcm, 6U).error() ==
         OggVorbisEncodeError::InvalidQuality);

  auto encoded = otgy::core::encode_ogg_vorbis(pcm, 3U);
  assert(encoded);
  const auto copy = *encoded.bytes();
  auto moved = std::move(encoded).take_bytes();
  assert(copy == moved);
  moved[0U] = 0U;
  assert(copy[0U] == 'O');

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  OggVorbisEncodeError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::ogg_vorbis_encode_error_message(
        static_cast<OggVorbisEncodeError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::ogg_vorbis_encode_error_message(
              static_cast<OggVorbisEncodeError>(255))
              .empty());
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--generate-goldens") {
    generate_quality_goldens();
    return 0;
  }
  assert(argc == 1);
  test_deterministic_qualities_and_decode();
  test_quality_goldens();
  test_metadata_order_and_invalid_omission();
  test_progress_and_cancellation();
  test_errors_and_ownership();
  return 0;
}
