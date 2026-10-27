#pragma once
#include <cstdint>

using Price = int64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;

enum class OrderType : uint8_t {
    Market = 0,
    Limit = 1
};

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

enum class TimeInForce : uint8_t {
    ImmediateOrCancel = 0,
    FillOrKill = 1,
    GoodTillCancel = 2,
};

enum class OrderOperation : uint8_t{
    Add = 0,
    Modify = 1,
    Cancel = 2
};

