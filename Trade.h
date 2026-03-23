#pragma once

#include "Types.h"
#include <type_traits>

namespace hft {

struct Trade {
    OrderId  buy_id;
    OrderId  sell_id;
    Price    exec_price;
    Qty      quantity;
    uint32_t _pad{0};
    uint64_t tsc;
};

static_assert(sizeof(Trade) == 40);
static_assert(std::is_trivially_copyable_v<Trade>);

} // namespace hft
