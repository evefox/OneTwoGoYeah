// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export.hpp"
#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

template <typename Value>
[[nodiscard]] std::optional<Value> parse_number(std::string_view text) {
  Value value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] bool write_file(const char* path,
                              const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

}  // namespace

int main(int argc, char* argv[]) {
  std::cout.imbue(std::locale::classic());
  const bool has_end = argc == 8;
  if (argc != 6 && !has_end) {
    return 1;
  }

  const auto encoded_input = read_file(argv[1]);
  const auto offset = parse_number<double>(argv[3]);
  const auto quality_value = parse_number<unsigned int>(argv[4]);
  std::optional<double> bpm;
  std::optional<std::uint64_t> end_row;
  if (has_end) {
    bpm = parse_number<double>(argv[6]);
    end_row = parse_number<std::uint64_t>(argv[7]);
  }
  if (encoded_input.empty() || !offset || !quality_value ||
      *quality_value > 255U || (has_end && (!bpm || !end_row))) {
    return 1;
  }

  std::optional<otgy::core::Pcm16> pcm;
  std::optional<otgy::core::AudioSourceInfo> source_info;
  const std::string_view codec{argv[2]};
  if (codec == "mp3") {
    auto decoded =
        otgy::core::decode_mp3(encoded_input.data(), encoded_input.size());
    if (!decoded) {
      return 1;
    }
    source_info = *decoded.source_info();
    pcm.emplace(std::move(decoded).take_pcm());
  } else if (codec == "ogg-vorbis") {
    auto decoded = otgy::core::decode_ogg_vorbis(encoded_input.data(),
                                                 encoded_input.size());
    if (!decoded) {
      return 1;
    }
    source_info = *decoded.source_info();
    pcm.emplace(std::move(decoded).take_pcm());
  } else {
    return 1;
  }

  const auto quality = static_cast<std::uint8_t>(*quality_value);
  auto snapshot =
      has_end
          ? otgy::core::create_audio_export_snapshot_with_end(
                *pcm, 0U, *offset, *bpm,
                otgy::core::AudioEndSpec{otgy::core::AudioEndMode::Beat,
                                         *end_row},
                quality, *source_info, "audio")
          : otgy::core::create_audio_export_snapshot(
                *pcm, 0U, *offset, quality, *source_info, "audio");
  if (!snapshot) {
    return 1;
  }
  auto exported =
      otgy::core::encode_audio_export_snapshot(*snapshot.snapshot());
  if (!exported) {
    return 1;
  }
  const auto output = std::move(exported).take_output();
  if (!write_file(argv[5], output.bytes)) {
    return 1;
  }

  std::cout << "{\"schemaVersion\":1,\"codec\":\"ogg-vorbis\""
               ",\"sampleRate\":"
            << pcm->sample_rate() << ",\"frameCount\":"
            << output.output_frame_count << ",\"signedShiftFrames\":"
            << output.signed_shift_frames << ",\"quality\":"
            << static_cast<unsigned int>(quality) << ",\"byteCount\":"
            << output.bytes.size() << "}\n";
  return std::cout ? 0 : 1;
}
