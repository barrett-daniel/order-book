#pragma once
#include <format>
#include <stdexcept>

#include "types.hpp"

struct Order {
    OrderType type;
    Side side;
    Quantity quantity;
    Price price;
    OrderId id;
    TimeInForce tif;

    Order(OrderType type_, Side side_, Quantity quantity_, Price price_, OrderId id_, TimeInForce tif_)
        : type{type_}
        , side{side_}
        , quantity{quantity_}
        , price{price_}
        , id{id_}
        , tif(tif_)
    {}

    void fill(Quantity quantity_to_fill) {
        if (quantity_to_fill > quantity) {
            throw std::logic_error(std::format(
                "Order: {} cannot be filled with quantity {} because the remaining quantity is only {}",
                id,
                quantity_to_fill,
                quantity
            ));
        }

        quantity -= quantity_to_fill;
    }

    std::string to_string() const {
        if (type == OrderType::Market) {
            return std::format("[Order {}: {} {}]",
                id,
                side == Side::Buy ? "BUY" : "SELL",
                quantity
            );
        }

        // limit
        return std::format("[Order {} : {} {} @ {}]",
            id,
            side == Side::Buy ? "BUY" : "SELL",
            quantity,
            price
        );
    }
};

struct OrderModify {
    OrderId id;
    Price price;
    Quantity quantity;
};

using OrderCancel = OrderId;
using OrderPointer = std::shared_ptr<Order>;
