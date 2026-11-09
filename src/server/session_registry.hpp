#pragma once

#include <iostream>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <utility>

#include "../order.hpp"

#include "constants.hpp"
#include "exchange.hpp"
#include "session.hpp"
#include "session_registry.hpp"
#include "server_types.hpp"

class SessionRegistry : public ITradeExecutor {
public:
    void add(ClientId client_id, std::weak_ptr<Session> s) {
        sessions_[client_id] = std::move(s);
    }

    void remove(ClientId client_id) {
        sessions_.erase(client_id);
    }

    void on_trade(ClientId client_id, const NetworkTradeInfo& trade_info) override {
        if (auto sell_session = find(client_id)) {
            sell_session->deliver(encode(trade_info));
        }
    }

private:
    std::unordered_map<ClientId, std::weak_ptr<Session>> sessions_;

    std::shared_ptr<Session> find(ClientId client_id) {
        auto it = sessions_.find(client_id);
        return it == sessions_.end() ? nullptr : it->second.lock();
    }

    static std::vector<uint8_t> encode(const NetworkTradeInfo& trade_info) {
        std::vector<uint8_t> bytes(RESPONSE_SIZE);

        std::memcpy(bytes.data(), MAGIC, MAGIC_LEN);

        OrderId order_id         = boost::endian::native_to_big(trade_info.order_id);
        ClientId other_client_id = boost::endian::native_to_big(trade_info.other_client_id);
        Quantity quantity        = boost::endian::native_to_big(trade_info.quantity);
        Price price              = boost::endian::native_to_big(trade_info.price);

        std::memcpy(bytes.data() + 4,  &order_id,        sizeof(OrderId));
        std::memcpy(bytes.data() + 12, &other_client_id, sizeof(OrderId));
        std::memcpy(bytes.data() + 20, &quantity,        sizeof(Quantity));
        std::memcpy(bytes.data() + 28, &price,           sizeof(Price));

        return bytes;
    }
};