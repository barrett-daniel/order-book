#include "session_registry.hpp"

void SessionRegistry::add(ClientId client_id, std::weak_ptr<Session> s) {
    sessions_[client_id] = std::move(s);
}

void SessionRegistry::remove(ClientId client_id) {
    sessions_.erase(client_id);
}

void SessionRegistry::on_trade(ClientId client_id, const NetworkTradeInfo& trade_info) {
    if (auto sell_session = find(client_id)) {
        sell_session->deliver(encode(trade_info));
    }
}

std::shared_ptr<Session> SessionRegistry::find(ClientId client_id) {
    auto it = sessions_.find(client_id);
    return it == sessions_.end() ? nullptr : it->second.lock();
}

std::vector<uint8_t> SessionRegistry::encode(const NetworkTradeInfo& trade_info) {
    std::vector<uint8_t> bytes(RESPONSE_SIZE);

    std::memcpy(bytes.data(), MAGIC, MAGIC_LEN);

    OrderId order_id = boost::endian::native_to_big(trade_info.order_id);
    ClientId other_client_id = boost::endian::native_to_big(trade_info.other_client_id);
    Quantity quantity = boost::endian::native_to_big(trade_info.quantity);
    Price price = boost::endian::native_to_big(trade_info.price);

    std::memcpy(bytes.data() + 4, &order_id, sizeof(OrderId));
    std::memcpy(bytes.data() + 12, &other_client_id, sizeof(ClientId));
    std::memcpy(bytes.data() + 20, &quantity, sizeof(Quantity));
    std::memcpy(bytes.data() + 28, &price, sizeof(Price));

    return bytes;
}
