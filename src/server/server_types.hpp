#pragma once

using ParseError = std::string;
using NetworkRequest = std::variant<Order, OrderModify, OrderCancel, ParseError>;
using ClientId = uint64_t;

struct NetworkTradeInfo {
    OrderId order_id;
    ClientId other_client_id;
    Quantity quantity;
    Price price;
};
