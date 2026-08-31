// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export.hpp"
#include "av_core/audio/mp3_decoder.hpp"
#include "av_core/audio/ogg_vorbis_decoder.hpp"
#include "av_core/audio/pcm16_hash.hpp"
#include "av_core/version.hpp"

#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
            "  export-audio <audio> --offset <seconds> --quality <q>\n"
            "               --output <file.ogg>\n"
            "               叠加 Offset 并导出 Ogg Vorbis\n"
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

[[nodiscard]] std::optional<double> parse_double(
    std::string_view text) noexcept {
  double value = 0.0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                      value, std::chars_format::general);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::uint8_t> parse_quality(
    std::string_view text) noexcept {
  unsigned int value = 0U;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value, 10);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size() || value > 255U) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(value);
}

enum class OutputWriteResult : std::uint8_t {
  Success,
  TargetExists,
  WriteFailed,
  PublishFailed,
};

[[nodiscard]] OutputWriteResult write_atomic_output(
    const std::filesystem::path& target,
    const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() > static_cast<std::size_t>(
                         std::numeric_limits<std::streamsize>::max())) {
    return OutputWriteResult::WriteFailed;
  }
  std::error_code error;
  if (std::filesystem::exists(target, error)) {
    return OutputWriteResult::TargetExists;
  }
  if (error) {
    return OutputWriteResult::WriteFailed;
  }

  auto temporary = target;
  temporary += ".otgy-tmp";
  for (std::uint32_t suffix = 2U;
       std::filesystem::exists(temporary, error); ++suffix) {
    if (error || suffix == std::numeric_limits<std::uint32_t>::max()) {
      return OutputWriteResult::WriteFailed;
    }
    temporary = target;
    temporary += ".otgy-tmp_" + std::to_string(suffix);
  }
  if (error) {
    return OutputWriteResult::WriteFailed;
  }

  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return OutputWriteResult::WriteFailed;
  }
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  output.close();
  if (!output) {
    std::filesystem::remove(temporary, error);
    return OutputWriteResult::WriteFailed;
  }

  error.clear();
  std::filesystem::create_hard_link(temporary, target, error);
  if (error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    error.clear();
    return std::filesystem::exists(target, error)
               ? OutputWriteResult::TargetExists
               : OutputWriteResult::PublishFailed;
  }

  error.clear();
  if (!std::filesystem::remove(temporary, error) || error) {
    std::error_code rollback_error;
    std::filesystem::remove(target, rollback_error);
    return OutputWriteResult::PublishFailed;
  }
  return OutputWriteResult::Success;
}

[[nodiscard]] std::string_view snapshot_error_code(
    otgy::core::AudioExportSnapshotError error) noexcept {
  switch (error) {
    case otgy::core::AudioExportSnapshotError::InvalidQuality:
      return "invalid-quality";
    case otgy::core::AudioExportSnapshotError::ExceedsSourceQuality:
      return "audio-export-quality-exceeds-source";
    case otgy::core::AudioExportSnapshotError::InvalidOffset:
      return "invalid-offset";
    case otgy::core::AudioExportSnapshotError::RemovesAllFrames:
      return "audio-export-offset-removes-all-frames";
    case otgy::core::AudioExportSnapshotError::None:
    case otgy::core::AudioExportSnapshotError::InvalidSourceInfo:
    case otgy::core::AudioExportSnapshotError::UnsupportedSampleRate:
    case otgy::core::AudioExportSnapshotError::SizeOverflow:
    case otgy::core::AudioExportSnapshotError::AllocationFailure:
      return "audio-export-invalid";
  }
  return "audio-export-invalid";
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

[[nodiscard]] int run_export_audio(const char* input_path,
                                   std::string_view offset_text,
                                   std::string_view quality_text,
                                   const char* output_path) {
  const auto format = audio_format_from_path(input_path);
  if (!format) {
    print_machine_error("unsupported-audio-format");
    return 3;
  }
  const auto output_format = audio_format_from_path(output_path);
  if (!output_format || *output_format != AudioFormat::OggVorbis) {
    print_machine_error("output-must-be-ogg");
    return 3;
  }

  const auto offset = parse_double(offset_text);
  if (!offset) {
    print_machine_error("invalid-offset");
    return 4;
  }
  const auto quality = parse_quality(quality_text);
  if (!quality) {
    print_machine_error("invalid-quality");
    return 4;
  }

  const auto target = std::filesystem::u8path(output_path);
  std::error_code path_error;
  if (std::filesystem::exists(target, path_error)) {
    print_machine_error("output-exists");
    return 3;
  }
  if (path_error) {
    print_machine_error("output-write-failed");
    return 3;
  }

  std::vector<std::uint8_t> encoded_input;
  if (!read_file(input_path, encoded_input)) {
    print_machine_error("audio-read-failed");
    return 3;
  }

  std::optional<otgy::core::Pcm16> pcm;
  std::optional<otgy::core::AudioSourceInfo> source_info;
  switch (*format) {
    case AudioFormat::Mp3: {
      auto result =
          otgy::core::decode_mp3(encoded_input.data(), encoded_input.size());
      if (!result) {
        print_machine_error("audio-decode-failed");
        return 4;
      }
      source_info = *result.source_info();
      pcm.emplace(std::move(result).take_pcm());
      break;
    }
    case AudioFormat::OggVorbis: {
      auto result = otgy::core::decode_ogg_vorbis(encoded_input.data(),
                                                   encoded_input.size());
      if (!result) {
        print_machine_error("audio-decode-failed");
        return 4;
      }
      source_info = *result.source_info();
      pcm.emplace(std::move(result).take_pcm());
      break;
    }
  }

  auto snapshot = otgy::core::create_audio_export_snapshot(
      *pcm, 0U, *offset, *quality, *source_info, "audio");
  if (!snapshot) {
    print_machine_error(snapshot_error_code(snapshot.error()));
    return 4;
  }
  auto exported =
      otgy::core::encode_audio_export_snapshot(*snapshot.snapshot());
  if (!exported) {
    print_machine_error(exported.error() ==
                                otgy::core::AudioExportTaskError::Cancelled
                            ? "audio-export-cancelled"
                            : "audio-export-failed");
    return 4;
  }

  const auto output = std::move(exported).take_output();
  switch (write_atomic_output(target, output.bytes)) {
    case OutputWriteResult::Success:
      break;
    case OutputWriteResult::TargetExists:
      print_machine_error("output-exists");
      return 3;
    case OutputWriteResult::WriteFailed:
      print_machine_error("output-write-failed");
      return 3;
    case OutputWriteResult::PublishFailed:
      print_machine_error("output-publish-failed");
      return 3;
  }

  std::cout << "{\"schemaVersion\":1,\"codec\":\"ogg-vorbis\""
               ",\"sampleRate\":"
            << pcm->sample_rate() << ",\"frameCount\":"
            << output.output_frame_count << ",\"signedShiftFrames\":"
            << output.signed_shift_frames << ",\"quality\":"
            << static_cast<unsigned int>(*quality) << ",\"byteCount\":"
            << output.bytes.size() << "}\n";
  return std::cout ? 0 : 5;
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

    if (command == "export-audio") {
      if (argc != 9 || std::string_view{argv[3]} != "--offset" ||
          std::string_view{argv[5]} != "--quality" ||
          std::string_view{argv[7]} != "--output") {
        print_usage(std::cerr);
        return 2;
      }
      return run_export_audio(argv[2], argv[4], argv[6], argv[8]);
    }

    std::cerr << "未知命令: " << command << '\n';
    print_usage(std::cerr);
    return 2;
  } catch (...) {
    print_machine_error("internal-error");
    return 5;
  }
}
