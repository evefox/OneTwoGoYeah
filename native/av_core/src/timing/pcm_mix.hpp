// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/audio/pcm16.hpp"

namespace otgy::core::detail {

[[nodiscard]] inline float mix_pcm16_frame(Pcm16::Sample left,
                                           Pcm16::Sample right) noexcept {
  return static_cast<float>(static_cast<int>(left) +
                            static_cast<int>(right)) /
         65'536.0F;
}

}  // namespace otgy::core::detail
