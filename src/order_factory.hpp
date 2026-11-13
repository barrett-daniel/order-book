#pragma once
#include "order.hpp"

class OrderFactory {
public:
    Order get_limit_order(Side side_, Quantity quantity_, Price price_, TimeInForce tif_) {
        return {OrderType::Limit, side_, quantity_, price_, next_id_++, tif_};
    }

    Order get_market_order(Side side_, Quantity quantity_, TimeInForce tif_) {
        return {OrderType::Market, side_, quantity_, std::numeric_limits<Price>::quiet_NaN(), next_id_++, tif_};
    }

private:
    OrderId next_id_ = 100;
};
