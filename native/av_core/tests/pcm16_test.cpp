// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16.hpp"
#include "av_core/audio/pcm16_hash.hpp"

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

using otgy::core::Pcm16;
using otgy::core::PcmChannel;
using otgy::core::PcmError;

void test_allocate_and_dimensions() {
  auto result = Pcm16::allocate(48'000U, 3U);
  assert(result.has_value());
  assert(result.error() == PcmError::None);
  assert(result.pcm() != nullptr);

  Pcm16 pcm = std::move(result).take_pcm();
  assert(pcm.sample_rate() == 48'000U);
  assert(Pcm16::channel_count() == 2U);
  assert(pcm.frame_count() == 3U);
  assert(pcm.channel_byte_size() == 3U * sizeof(Pcm16::Sample));
  assert(pcm.total_byte_size() == 2U * 3U * sizeof(Pcm16::Sample));

  const auto* left = pcm.channel_data(PcmChannel::Left);
  const auto* right = pcm.channel_data(PcmChannel::Right);
  assert(left != nullptr);
  assert(right != nullptr);
  assert(left[0] == 0);
  assert(right[2] == 0);

  left = nullptr;
  right = nullptr;
  assert(pcm.channel_data(static_cast<PcmChannel>(99)) == nullptr);
  assert(pcm.mutable_channel_data(static_cast<PcmChannel>(99)) == nullptr);
}

void test_copy_owns_samples() {
  std::array<Pcm16::Sample, 3> left{{-10, 20, 30}};
  std::array<Pcm16::Sample, 3> right{{40, -50, 60}};

  auto result = Pcm16::copy_from_channels(44'100U, left.data(), left.size(),
                                          right.data(), right.size());
  assert(result);
  Pcm16 pcm = std::move(result).take_pcm();
  assert(pcm.channel_data(PcmChannel::Left)[0] == -10);
  assert(pcm.channel_data(PcmChannel::Right)[1] == -50);

  // Input buffers are copied, not borrowed.
  left[0] = 111;
  right[0] = 222;
  assert(pcm.channel_data(PcmChannel::Left)[0] == -10);
  assert(pcm.channel_data(PcmChannel::Right)[0] == 40);

  // A copy is independent while the frame count remains fixed.
  Pcm16 clone = pcm;
  clone.mutable_channel_data(PcmChannel::Left)[0] = 999;
  assert(pcm.channel_data(PcmChannel::Left)[0] == -10);
  assert(clone.channel_data(PcmChannel::Left)[0] == 999);

  // Mutable access cannot resize the owned channel and is bounded by the
  // immutable frame count exposed by the value.
  clone.mutable_channel_data(PcmChannel::Right)[2] = -777;
  assert(clone.channel_data(PcmChannel::Right)[2] == -777);
  assert(clone.frame_count() == 3U);
}

void test_zero_frames() {
  auto result = Pcm16::copy_from_channels(8'000U, nullptr, 0U, nullptr, 0U);
  assert(result);
  Pcm16& pcm = *result.pcm();
  assert(pcm.frame_count() == 0U);
  assert(pcm.channel_byte_size() == 0U);
  assert(pcm.total_byte_size() == 0U);
  assert(pcm.channel_data(PcmChannel::Left) == nullptr);
  assert(pcm.channel_data(PcmChannel::Right) == nullptr);
  assert(pcm.mutable_channel_data(PcmChannel::Left) == nullptr);
  assert(pcm.mutable_channel_data(PcmChannel::Right) == nullptr);

  const auto hash = otgy::core::pcm16le_sha256_hex(pcm);
  assert(std::string_view(hash.data(), hash.size()) ==
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert(otgy::core::kPlanarPcm16LeFormat ==
         "pcm16le-planar-left-right");
}

void test_golden_pcm16le_hash_projection() {
  const std::string path =
      std::string{OTGY_PCM16_HASH_FIXTURE_DIR} +
      "/generated_mono_8000.planar.pcm16le";
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  const std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{},
  };
  assert(bytes.size() % (Pcm16::kChannelCount * Pcm16::kBytesPerSample) ==
         0U);

  const auto frame_count =
      bytes.size() / (Pcm16::kChannelCount * Pcm16::kBytesPerSample);
  std::vector<Pcm16::Sample> left(frame_count);
  std::vector<Pcm16::Sample> right(frame_count);
  for (std::size_t channel = 0U; channel < Pcm16::kChannelCount; ++channel) {
    auto& samples = channel == 0U ? left : right;
    const auto channel_offset = channel * frame_count * 2U;
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
      const auto byte_offset = channel_offset + frame * 2U;
      const auto bits = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(bytes[byte_offset]) |
          (static_cast<std::uint16_t>(bytes[byte_offset + 1U]) << 8U));
      const auto signed_value =
          bits <= static_cast<std::uint16_t>(
                      std::numeric_limits<Pcm16::Sample>::max())
              ? static_cast<std::int32_t>(bits)
              : static_cast<std::int32_t>(bits) - 65'536;
      samples[frame] = static_cast<Pcm16::Sample>(signed_value);
    }
  }

  auto result = Pcm16::copy_from_channels(
      8'000U, left.data(), left.size(), right.data(), right.size());
  assert(result);
  assert(result.pcm()->frame_count() == 9'216U);
  const auto hash = otgy::core::pcm16le_sha256_hex(*result.pcm());
  assert(std::string_view(hash.data(), hash.size()) ==
         "b1d85a34af1e9e16f7530f6efdbde26e3f8e5e5a71fc8c7345aed2345a77a451");
}

void test_stable_errors() {
  auto invalid_rate = Pcm16::allocate(0U, 1U);
  assert(!invalid_rate);
  assert(invalid_rate.pcm() == nullptr);
  assert(invalid_rate.error() == PcmError::InvalidSampleRate);

  const std::array<Pcm16::Sample, 1> one{{1}};
  auto mismatch = Pcm16::copy_from_channels(44'100U, one.data(), 1U,
                                             nullptr, 0U);
  assert(!mismatch);
  assert(mismatch.error() == PcmError::ChannelFrameCountMismatch);

  auto null_data = Pcm16::copy_from_channels(44'100U, nullptr, 1U, nullptr,
                                             1U);
  assert(!null_data);
  assert(null_data.error() == PcmError::NullChannelData);

  auto overflow = Pcm16::allocate(
      44'100U, std::numeric_limits<std::size_t>::max());
  assert(!overflow);
  assert(overflow.error() == PcmError::SizeOverflow);

  assert(!otgy::core::pcm_error_message(PcmError::InvalidSampleRate).empty());
  assert(otgy::core::pcm_error_message(PcmError::None).empty());
}

void test_dimension_validation_precedes_access() {
  // A null pointer with an overflowing frame count is reported as a size
  // error only after the channel shape is known to be valid; no memory is
  // touched while validating dimensions.
  const auto error = otgy::core::validate_pcm_dimensions(
      44'100U, std::numeric_limits<std::size_t>::max());
  assert(error == PcmError::SizeOverflow);
  assert(otgy::core::validate_pcm_dimensions(0U, 0U) ==
         PcmError::InvalidSampleRate);
  assert(otgy::core::validate_pcm_dimensions(44'100U, 0U) ==
         PcmError::None);
}

}  // namespace

int main() {
  test_allocate_and_dimensions();
  test_copy_owns_samples();
  test_zero_frames();
  test_golden_pcm16le_hash_projection();
  test_stable_errors();
  test_dimension_validation_precedes_access();
  return 0;
}
