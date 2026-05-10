#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace cpplox {

inline constexpr int kUint8Count =
    static_cast<int>(std::numeric_limits<uint8_t>::max()) + 1;

} // namespace cpplox
