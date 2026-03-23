#pragma once

#include "Types.h"
#include <type_traits>

namespace hft {

struct alignas(64) Order {
    OrderId  id;            //  8
    Price    price;         //  8
    Qty      quantity;      //  4
    Qty      filled;        //  4
    uint64_t arrival_tsc;   //  8
    Side     side;          //  1
    OType    type;          //  1
    OStatus  status;        //  1
    uint8_t  _pad[29];      // 29 => total 64

    Order() = default;

    Order(OrderId id_, Side side_, OType type_, Price price_, Qty qty_,
          uint64_t tsc = 0) noexcept
        : id{id_}, price{price_}, quantity{qty_}, filled{0}
        , arrival_tsc{tsc}, side{side_}, type{type_}
        , status{OStatus::New}, _pad{} {}

    [[nodiscard]] bool   is_filled()  const noexcept { return filled >= quantity; }
    [[nodiscard]] Qty    remaining()  const noexcept { return quantity - filled; }
    [[nodiscard]] bool   is_buy()     const noexcept { return side == Side::Buy; }

    [[nodiscard]] bool   is_valid()   const noexcept {
        if (quantity <= 0) return false;
        if (type != OType::Market && price <= 0) return false;
        return true;
    }
};

static_assert(sizeof(Order)  == 64,  "Order must be exactly one cache line");
static_assert(alignof(Order) == 64,  "Order must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<Order>);

} // namespace hft
