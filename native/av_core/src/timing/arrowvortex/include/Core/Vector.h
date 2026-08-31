#pragma once

#include <vector>

namespace Vortex {

template <typename T>
using Vector = std::vector<T>;

}  // namespace Vortex

template <typename T>
using Vector = Vortex::Vector<T>;
