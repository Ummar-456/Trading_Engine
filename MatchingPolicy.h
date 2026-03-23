#pragma once

#include "Types.h"

namespace hft {

template <typename Derived>
struct MatchingPolicy {
    [[nodiscard]] bool can_match(Price bid, Price ask) const noexcept {
        return static_cast<const Derived*>(this)->can_match_impl(bid, ask);
    }
    [[nodiscard]] Price exec_price(Price bid, Price ask) const noexcept {
        return static_cast<const Derived*>(this)->exec_price_impl(bid, ask);
    }
};

struct PriceTimePolicy : MatchingPolicy<PriceTimePolicy> {
    [[nodiscard]] bool  can_match_impl(Price bid, Price ask) const noexcept {
        return bid >= ask;
    }
    [[nodiscard]] Price exec_price_impl(Price /*bid*/, Price ask) const noexcept {
        return ask;
    }
};

struct MidpointPolicy : MatchingPolicy<MidpointPolicy> {
    [[nodiscard]] bool  can_match_impl(Price bid, Price ask) const noexcept {
        return bid >= ask;
    }
    [[nodiscard]] Price exec_price_impl(Price bid, Price ask) const noexcept {
        return (bid + ask) / 2;
    }
};

struct AggressorPolicy : MatchingPolicy<AggressorPolicy> {
    [[nodiscard]] bool  can_match_impl(Price bid, Price ask) const noexcept {
        return bid >= ask;
    }
    [[nodiscard]] Price exec_price_impl(Price bid, Price /*ask*/) const noexcept {
        return bid;
    }
};

} // namespace hft
