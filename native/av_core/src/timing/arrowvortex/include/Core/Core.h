#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

using std::max;
using std::min;

// ArrowVortex's embedded aubio subset assumes calloc always succeeds. The
// av_core public boundary is noexcept, so turn allocation failure and size
// overflow into bad_alloc before the unchanged reference code dereferences a
// null pointer. detect_onsets maps that exception to its stable error code.
inline void* otgy_arrowvortex_calloc(std::size_t count, std::size_t size) {
  if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) {
    throw std::bad_alloc{};
  }
  const auto bytes = count * size;
  void* allocation = std::calloc(count, size);
  if (allocation == nullptr && bytes != 0U) {
    throw std::bad_alloc{};
  }
  return allocation;
}
