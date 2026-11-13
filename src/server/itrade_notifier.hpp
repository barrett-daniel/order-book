#pragma once

#include "server_types.hpp"

class ITradeNotifier {
public:
    virtual ~ITradeNotifier() = default;

    virtual void on_trade(ClientId client_id, const NetworkTradeInfo& trade_info) = 0;
};
