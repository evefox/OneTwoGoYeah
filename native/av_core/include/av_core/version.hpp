// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

namespace otgy::core {

struct Version final {
  std::uint32_t major;
  std::uint32_t minor;
  std::uint32_t patch;
};

[[nodiscard]] constexpr Version version() noexcept {
  return Version{0U, 1U, 0U};
}

[[nodiscard]] std::string_view version_string() noexcept;

}  // namespace otgy::core
