// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"
#include "av_core/version.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

enum class AudioFormat : std::uint8_t {
  Mp3,
  OggVorbis,
};

void configure_machine_streams() {
#if defined(_WIN32)
  static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
  static_cast<void>(_setmode(_fileno(stderr), _O_BINARY));
#endif
  std::cout.imbue(std::locale::classic());
  std::cerr.imbue(std::locale::classic());
}

void print_usage(std::ostream& output) {
  output << "用法: avtool <命令> [选项]\n"
            "\n"
            "当前可用命令:\n"
            "  decode <audio> --pcm-hash\n"
            "               解码 MP3/Ogg 并输出规范 PCM 哈希 JSON\n"
            "  --help       显示此帮助\n"
            "  --version    显示 av_core 版本\n";
}

[[nodiscard]] constexpr char ascii_lower(char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value - 'A' + 'a')
             : value;
}

[[nodiscard]] bool ascii_case_equal(std::string_view left,
                                    std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (ascii_lower(left[index]) != ascii_lower(right[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<AudioFormat> audio_format_from_path(
    std::string_view path) noexcept {
  const auto extension_start = path.find_last_of('.');
  const auto separator = path.find_last_of("/\\");
  if (extension_start == std::string_view::npos ||
      (separator != std::string_view::npos && extension_start < separator)) {
    return std::nullopt;
  }

  const auto extension = path.substr(extension_start);
  if (ascii_case_equal(extension, ".mp3")) {
    return AudioFormat::Mp3;
  }
  if (ascii_case_equal(extension, ".ogg")) {
    return AudioFormat::OggVorbis;
  }
  return std::nullopt;
}

[[nodiscard]] std::string_view format_name(AudioFormat format) noexcept {
  switch (format) {
    case AudioFormat::Mp3:
      return "mp3";
    case AudioFormat::OggVorbis:
      return "ogg-vorbis";
  }
  return {};
}

[[nodiscard]] bool read_file(const char* path,
                             std::vector<std::uint8_t>& bytes) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    return false;
  }

  const auto end = input.tellg();
  if (end < 0) {
    return false;
  }
  const auto size = static_cast<std::uintmax_t>(end);
  if (size > bytes.max_size() ||
      size > static_cast<std::uintmax_t>(
                 std::numeric_limits<std::streamsize>::max())) {
    return false;
  }

  bytes.resize(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  if (!input) {
    return false;
  }
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      return false;
    }
  }
  return !input.bad();
}

void print_machine_error(std::string_view code) {
  std::cerr << "{\"schemaVersion\":1,\"error\":\"" << code << "\"}\n";
}

[[nodiscard]] int print_decode_result(AudioFormat format,
                                      const otgy::core::Pcm16& pcm) {
  const auto hash = otgy::core::pcm16le_sha256_hex(pcm);
  std::cout << "{\"schemaVersion\":1,\"codec\":\"" << format_name(format)
            << "\",\"pcmFormat\":\"" << otgy::core::kPlanarPcm16LeFormat
            << "\",\"sampleRate\":" << pcm.sample_rate()
            << ",\"frameCount\":" << pcm.frame_count()
            << ",\"pcmSha256\":\"";
  std::cout.write(hash.data(), static_cast<std::streamsize>(hash.size()));
  std::cout << "\"}\n";
  return std::cout ? 0 : 5;
}

[[nodiscard]] int run_decode(const char* path) {
  const auto format = audio_format_from_path(path);
  if (!format) {
    print_machine_error("unsupported-audio-format");
    return 3;
  }

  std::vector<std::uint8_t> encoded;
  if (!read_file(path, encoded)) {
    print_machine_error("audio-read-failed");
    return 3;
  }

  switch (*format) {
    case AudioFormat::Mp3: {
      auto result = otgy::core::decode_mp3(encoded.data(), encoded.size());
      if (!result) {
        print_machine_error("audio-decode-failed");
        return 4;
      }
      return print_decode_result(*format, *result.pcm());
    }
    case AudioFormat::OggVorbis: {
      auto result =
          otgy::core::decode_ogg_vorbis(encoded.data(), encoded.size());
      if (!result) {
        print_machine_error("audio-decode-failed");
        return 4;
      }
      return print_decode_result(*format, *result.pcm());
    }
  }

  print_machine_error("internal-error");
  return 5;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    configure_machine_streams();

    if (argc == 1) {
      print_usage(std::cout);
      return 0;
    }

    const std::string_view command{argv[1]};
    if (command == "--help" || command == "-h" || command == "help") {
      print_usage(std::cout);
      return 0;
    }

    if (command == "--version" || command == "version") {
      std::cout << "avtool " << otgy::core::version_string() << '\n';
      return 0;
    }

    if (command == "decode") {
      if (argc != 4 || std::string_view{argv[3]} != "--pcm-hash") {
        print_usage(std::cerr);
        return 2;
      }
      return run_decode(argv[2]);
    }

    std::cerr << "未知命令: " << command << '\n';
    print_usage(std::cerr);
    return 2;
  } catch (...) {
    print_machine_error("internal-error");
    return 5;
  }
}
