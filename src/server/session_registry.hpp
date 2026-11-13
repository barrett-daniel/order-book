#pragma once

#include <iostream>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <utility>

#include "../order.hpp"

#include "constants.hpp"
#include "exchange.hpp"
#include "session.hpp"
#include "server_types.hpp"

/**
 * Data structure managing the pool of currently active sessions
 */
class SessionRegistry : public ITradeNotifier {
public:
    void add(ClientId client_id, std::weak_ptr<Session> s);

    void remove(ClientId client_id);

    void on_trade(ClientId client_id, const NetworkTradeInfo& trade_info) override;

private:
    std::unordered_map<ClientId, std::weak_ptr<Session> > sessions_;

    std::shared_ptr<Session> find(ClientId client_id);

    /**
     * Encode the network trade data into bytes
     *
     * @param trade_info to parse into bytes
     * @return the encoded bytes
     */
    static std::vector<uint8_t> encode(const NetworkTradeInfo& trade_info);
};
