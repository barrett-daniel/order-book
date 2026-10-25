#pragma once
#include <format>

#include "types.hpp"

struct Trade {
    OrderId buy_id;
    OrderId sell_id;
    Quantity quantity;
    Price price;

    std::string to_string() const {
        return std::format("[Trade : buy_id={} sell_id={}, quantity={}, price={}]",
            buy_id,
            sell_id,
            quantity,
            price
        );
    }
};
