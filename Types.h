#pragma once

#include <cstdint>
#include <limits>

namespace hft {

using Price   = int64_t;
using Qty     = int32_t;
using OrderId = uint64_t;

static constexpr Price TICK_SCALE    = 1'000;
static constexpr Price INVALID_PRICE = std::numeric_limits<Price>::max();

enum class Side    : uint8_t { Buy = 0, Sell = 1 };
enum class OType   : uint8_t { Limit = 0, Market = 1, Stop = 2 };
enum class OStatus : uint8_t { New = 0, PartiallyFilled = 1, Filled = 2,
                                Cancelled = 3, Rejected = 4 };

[[nodiscard]] constexpr Price to_ticks(double p) noexcept {
    return static_cast<Price>(p * static_cast<double>(TICK_SCALE) + 0.5);
}

[[nodiscard]] constexpr double from_ticks(Price t) noexcept {
    return static_cast<double>(t) / static_cast<double>(TICK_SCALE);
}

} // namespace hft
