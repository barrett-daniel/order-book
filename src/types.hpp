#pragma once
#include <cstdint>
#include <magic_enum/magic_enum.hpp>

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
    IOC = 0,
    FOK = 1,
    GTC = 2,
};

enum class OrderOperation : int {
    Add = 0,
    Modify = 1,
    Cancel = 2
};

/**
 * Converts an input value to the templated enum type
 *
 * @tparam E The enum to cast to
 * @param value The value of the data to be cast
 * @return The associated enum value of the input
 */
template <typename E, typename T>
inline std::optional<E> parse_enum(T value) {
    if constexpr (std::is_integral_v<T>) {
        return magic_enum::enum_cast<E>(value);
    }
    else {
        return magic_enum::enum_cast<E>(value, magic_enum::case_insensitive);
    }
}
