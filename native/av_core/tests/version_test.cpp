// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/version.hpp"

#include <iostream>
#include <string_view>

int main() {
  constexpr auto version = otgy::core::version();

  if (version.major != 0U || version.minor != 1U || version.patch != 0U) {
    std::cerr << "unexpected structured av_core version\n";
    return 1;
  }

  if (otgy::core::version_string() != std::string_view{"0.1.0"}) {
    std::cerr << "unexpected av_core version string\n";
    return 1;
  }

  return 0;
}
