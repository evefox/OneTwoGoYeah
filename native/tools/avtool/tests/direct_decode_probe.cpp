// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] int print_pcm(const otgy::core::Pcm16& pcm) {
  const auto hash = otgy::core::pcm16le_sha256_hex(pcm);
  std::cout << otgy::core::kPlanarPcm16LeFormat << '\n'
            << pcm.sample_rate() << '\n'
            << pcm.frame_count() << '\n';
  std::cout.write(hash.data(), static_cast<std::streamsize>(hash.size()));
  std::cout << '\n';
  return std::cout ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32)
  static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
  if (argc != 3) {
    return 1;
  }

  const auto encoded = read_file(argv[1]);
  if (encoded.empty()) {
    return 1;
  }

  const std::string_view codec{argv[2]};
  if (codec == "mp3") {
    const auto result =
        otgy::core::decode_mp3(encoded.data(), encoded.size());
    return result ? print_pcm(*result.pcm()) : 1;
  }
  if (codec == "ogg-vorbis") {
    const auto result =
        otgy::core::decode_ogg_vorbis(encoded.data(), encoded.size());
    return result ? print_pcm(*result.pcm()) : 1;
  }
  return 1;
}
