// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/pcm16_hash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace otgy::core {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                    unsigned shift) noexcept {
  return (value >> shift) | (value << (32U - shift));
}

class Sha256 final {
 public:
  void update(const std::uint8_t* data, std::size_t size) noexcept {
    total_bytes_ += static_cast<std::uint64_t>(size);

    while (size > 0U) {
      const auto copied = std::min(size, block_.size() - block_size_);
      std::copy_n(data, copied, block_.data() + block_size_);
      data += copied;
      size -= copied;
      block_size_ += copied;

      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0U;
      }
    }
  }

  [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept {
    const std::uint64_t bit_length = total_bytes_ * 8U;
    block_[block_size_++] = 0x80U;

    if (block_size_ > 56U) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_),
                block_.end(), 0U);
      transform(block_.data());
      block_size_ = 0U;
    }

    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_),
              block_.begin() + 56, 0U);
    for (std::size_t index = 0U; index < 8U; ++index) {
      block_[63U - index] =
          static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    transform(block_.data());

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t word = 0U; word < state_.size(); ++word) {
      for (std::size_t byte = 0U; byte < 4U; ++byte) {
        digest[word * 4U + byte] = static_cast<std::uint8_t>(
            state_[word] >> ((3U - byte) * 8U));
      }
    }
    return digest;
  }

 private:
  void transform(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0U; index < 16U; ++index) {
      const std::size_t offset = index * 4U;
      words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                     (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                     (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                     static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
      const auto s0 = rotate_right(words[index - 15U], 7U) ^
                      rotate_right(words[index - 15U], 18U) ^
                      (words[index - 15U] >> 3U);
      const auto s1 = rotate_right(words[index - 2U], 17U) ^
                      rotate_right(words[index - 2U], 19U) ^
                      (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0U; index < words.size(); ++index) {
      const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                        rotate_right(e, 25U);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto temporary1 =
          h + sum1 + choose + kRoundConstants[index] + words[index];
      const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                        rotate_right(a, 22U);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temporary2 = sum0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{{
      0x6a09e667U,
      0xbb67ae85U,
      0x3c6ef372U,
      0xa54ff53aU,
      0x510e527fU,
      0x9b05688cU,
      0x1f83d9abU,
      0x5be0cd19U,
  }};
  std::array<std::uint8_t, 64> block_{};
  std::size_t block_size_ = 0U;
  std::uint64_t total_bytes_ = 0U;
};

}  // namespace

std::array<char, 64> pcm16le_sha256_hex(const Pcm16& pcm) noexcept {
  Sha256 sha256;
  std::array<std::uint8_t, 4096> encoded{};
  constexpr std::size_t kSamplesPerChunk = encoded.size() / 2U;

  for (const auto channel : {PcmChannel::Left, PcmChannel::Right}) {
    const auto* samples = pcm.channel_data(channel);
    std::size_t offset = 0U;
    while (offset < pcm.frame_count()) {
      const auto sample_count =
          std::min(kSamplesPerChunk, pcm.frame_count() - offset);
      for (std::size_t index = 0U; index < sample_count; ++index) {
        const auto bits = static_cast<std::uint16_t>(samples[offset + index]);
        encoded[index * 2U] = static_cast<std::uint8_t>(bits & 0xffU);
        encoded[index * 2U + 1U] = static_cast<std::uint8_t>(bits >> 8U);
      }
      sha256.update(encoded.data(), sample_count * 2U);
      offset += sample_count;
    }
  }

  const auto digest = sha256.finish();
  constexpr std::array<char, 16> kHex{{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
  }};
  std::array<char, 64> result{};
  for (std::size_t index = 0U; index < digest.size(); ++index) {
    result[index * 2U] = kHex[digest[index] >> 4U];
    result[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  return result;
}

}  // namespace otgy::core
