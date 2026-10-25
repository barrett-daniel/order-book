#pragma once
#include "order.hpp"

class OrderFactory {
public:
    Order get_limit_order(Side side_, Quantity quantity_, Price price_) {
        return {OrderType::Limit, side_, quantity_, price_, m_next_id++};
    }

    Order get_market_order(Side side_, Quantity quantity_) {
        return {OrderType::Market, side_, quantity_, std::numeric_limits<Price>::quiet_NaN(), m_next_id++};
    }

private:
    OrderId m_next_id = 0;
};
