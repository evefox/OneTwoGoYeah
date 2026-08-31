// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/onset_detector.hpp"

#include "onset_detector_internal.hpp"
#include "pcm_mix.hpp"

#include <Editor/FindOnsets.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace otgy::core {

OnsetDetectionResult::OnsetDetectionResult(
    std::vector<Onset> onsets) noexcept
    : storage_(std::move(onsets)) {}

OnsetDetectionResult::OnsetDetectionResult(
    OnsetDetectionError error) noexcept
    : storage_(error) {}

bool OnsetDetectionResult::has_value() const noexcept {
  return std::holds_alternative<std::vector<Onset>>(storage_);
}

const std::vector<Onset>* OnsetDetectionResult::onsets() const noexcept {
  return std::get_if<std::vector<Onset>>(&storage_);
}

std::vector<Onset>* OnsetDetectionResult::onsets() noexcept {
  return std::get_if<std::vector<Onset>>(&storage_);
}

OnsetDetectionError OnsetDetectionResult::error() const noexcept {
  if (const auto* error = std::get_if<OnsetDetectionError>(&storage_)) {
    return *error;
  }
  return OnsetDetectionError::None;
}

std::vector<Onset> OnsetDetectionResult::take_onsets() && {
  return std::move(std::get<std::vector<Onset>>(storage_));
}

OnsetDetectionResult detail::detect_onsets_prefix(
    const Pcm16& pcm, std::size_t frame_count) noexcept {
  constexpr std::size_t kWindowLength = 256U;
  constexpr auto kReferenceIntMax =
      static_cast<std::uint64_t>(std::numeric_limits<int>::max());

  if (static_cast<std::uint64_t>(pcm.sample_rate()) > kReferenceIntMax ||
      frame_count > kReferenceIntMax || frame_count > pcm.frame_count()) {
    return OnsetDetectionResult{OnsetDetectionError::InputTooLarge};
  }
  if (frame_count < kWindowLength) {
    return OnsetDetectionResult{std::vector<Onset>{}};
  }

  try {
    std::vector<float> mono(frame_count);
    const auto* left = pcm.channel_data(PcmChannel::Left);
    const auto* right = pcm.channel_data(PcmChannel::Right);
    for (std::size_t frame = 0U; frame < frame_count; ++frame) {
      mono[frame] = detail::mix_pcm16_frame(left[frame], right[frame]);
    }

    Vector<Vortex::Onset> reference_onsets;
    Vortex::FindOnsets(mono.data(), static_cast<int>(pcm.sample_rate()),
                       static_cast<int>(frame_count), 1,
                       reference_onsets);

    std::vector<Onset> onsets;
    onsets.reserve(reference_onsets.size());
    std::transform(reference_onsets.begin(), reference_onsets.end(),
                   std::back_inserter(onsets),
                   [](const Vortex::Onset& onset) {
                     return Onset{static_cast<std::uint64_t>(onset.pos),
                                  onset.strength};
                   });
    return OnsetDetectionResult{std::move(onsets)};
  } catch (const std::length_error&) {
    return OnsetDetectionResult{OnsetDetectionError::InputTooLarge};
  } catch (const std::bad_alloc&) {
    return OnsetDetectionResult{OnsetDetectionError::AllocationFailure};
  }
}

OnsetDetectionResult detect_onsets(const Pcm16& pcm) noexcept {
  return detail::detect_onsets_prefix(pcm, pcm.frame_count());
}

}  // namespace otgy::core
