#pragma once

#include <iostream>
#include <boost/container_hash/hash.hpp>
#include <utility>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include "../order.hpp"
#include "../order_book.hpp"
#include "../order_factory.hpp"

#include "itrade_notifier.hpp"
#include "server_types.hpp"

class Exchange {
public:
    explicit Exchange(ITradeNotifier& sink);

    void handle(ClientId client, const NetworkRequest& req);

private:
    struct Owner {
        ClientId client_id;
        OrderId client_order_id;

        bool operator==(const Owner&) const = default;
    };

    struct OwnerHash {
        std::size_t operator()(const Owner& owner) const {
            std::size_t seed = 0;
            boost::hash_combine(seed, owner.client_id);
            boost::hash_combine(seed, owner.client_order_id);
            return seed;
        }
    };

    // map of server order ids, to client and their client order ids
    using OrderBimap = boost::bimap<
        boost::bimaps::unordered_set_of<OrderId>,
        boost::bimaps::unordered_set_of<Owner, OwnerHash>
    >;

    ITradeNotifier& trade_notifier_;
    OrderFactory order_factory_;
    OrderBook order_book_;
    OrderBimap order_owners_;

    void add(ClientId client_id, const Order& request);

    void modify(ClientId c, const OrderModify& request);

    void cancel(ClientId client_id, const OrderCancel& request);

    void publish(std::vector<Trade>& trades);

    /**
     * removes orders which are no longer resting from the exchange map
     */
    void prune_owners(OrderId order_id, const std::vector<Trade>& trades);
};
