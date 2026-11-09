#pragma once

#include "server_types.hpp"

class ITradeExecutor {
public:
    virtual ~ITradeExecutor() = default;
    virtual void on_trade(ClientId client_id, const NetworkTradeInfo& trade_info) = 0;
};