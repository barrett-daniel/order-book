#pragma once
#include <cstdint>

using Price = int64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;

enum class OrderType {
    Market,
    Limit
};

enum class Side {
    Buy,
    Sell
};

enum class TimeInForce {
    ImmediateOrCancel,
    FillOrKill,
    GoodTillCancel,
};

