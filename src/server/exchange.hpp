#pragma once

#include <iostream>
#include <boost/container_hash/hash.hpp>
#include <utility>
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>

#include "../order.hpp"
#include "../order_book.hpp"
#include "../order_factory.hpp"

#include "itrade_executor.hpp"
#include "server_types.hpp"

class Exchange {
public:
    explicit Exchange(ITradeExecutor& sink) : trade_executor_(sink) {}

    void handle(ClientId client, const NetworkRequest& req) {
        if (std::holds_alternative<Order>(req)) {
            add(client, std::get<Order>(req));
            return;
        }
        if (std::holds_alternative<OrderModify>(req)) {
            modify(client, std::get<OrderModify>(req));
            return;
        }
        if (std::holds_alternative<OrderCancel>(req)) {
            cancel(client, std::get<OrderCancel>(req));
            return;
        }
        if (std::holds_alternative<ParseError>(req)) {
            std::cerr << "Err" << std::endl;
            return;
        }

        std::cerr << "error" << std::endl;
    }

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

    using OrderBimap = boost::bimap<
        boost::bimaps::unordered_set_of<OrderId>,
        boost::bimaps::unordered_set_of<Owner, OwnerHash>
    >;

    ITradeExecutor& trade_executor_;
    OrderFactory order_factory_;
    OrderBook order_book_;
    OrderBimap order_owners_;

    void add(ClientId client_id, const Order& request) {
        Order order = (request.type == OrderType::Market) // convert client to server order
            ? order_factory_.get_market_order(request.side, request.quantity, request.tif)
            : order_factory_.get_limit_order(request.side, request.quantity, request.price, request.tif);

        order_owners_.insert(OrderBimap::value_type(order.id, Owner{client_id, request.id}));

        auto trades = order_book_.add_order(std::make_shared<Order>(order));
        publish(trades);
        prune_owners(order.id, trades);

        std::cout << "Adding Order: " << order.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;
    }

    void modify(ClientId c, const OrderModify& request) {
        auto it = order_owners_.right.find(Owner{c, request.id});
        if (it == order_owners_.right.end()) return;

        OrderModify order_modify = {
            .id = it->second,
            .price = request.price,
            .quantity = request.quantity,
        };

        auto trades = order_book_.modify_order(order_modify);
        publish(trades);
        prune_owners(order_modify.id, trades);

        std::cout << "Modifying Order: " << order_modify.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;
    }

    void cancel(ClientId client_id, const OrderCancel& request) {
        auto it = order_owners_.right.find(Owner{client_id, static_cast<OrderId>(request)});
        if (it == order_owners_.right.end()) return;

        OrderCancel order_cancel_id = it->second;
        order_book_.cancel_order(order_cancel_id);
        order_owners_.right.erase(it);

        std::cout << "Cancelling Order: " << order_cancel_id << std::endl;
        std::cout << order_book_.to_string() << std::endl;
    }

    // Each trade has two sides, and they may belong to different clients
    void publish(std::vector<Trade>& trades) {
        if (trades.empty()) return;

        std::cout << "Trades Executed: " << std::endl;
        for (const Trade& t : trades) {
            std::cout << "\t" << t.to_string() << std::endl;
            auto sell_it = order_owners_.left.find(t.sell_id);
            auto buy_it = order_owners_.left.find(t.buy_id);

            if (sell_it == order_owners_.left.end() || buy_it == order_owners_.left.end()) {
                std::cerr << "Err" << std::endl;
                continue;
            }

            const Owner& sell_owner = sell_it->second;
            const Owner& buy_owner = buy_it->second;

            NetworkTradeInfo sell_trade_info = NetworkTradeInfo{
                .order_id = sell_owner.client_order_id,
                .other_client_id = buy_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };
            trade_executor_.on_trade(sell_owner.client_id, sell_trade_info);

            NetworkTradeInfo buy_trade_info = NetworkTradeInfo{
                .order_id = buy_owner.client_order_id,
                .other_client_id = sell_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };

            trade_executor_.on_trade(buy_owner.client_id, buy_trade_info);
        }
        std::cout << std::endl;
    }

    void prune_owners(OrderId order_id, const std::vector<Trade>& trades) {
        auto prune = [this](OrderId id) -> void {
            if (this->order_book_.get_order(id) == nullptr) {
                order_owners_.left.erase(id);
            }
        };

        prune(order_id);
        for (const Trade& trade : trades) {
            prune(trade.buy_id);
            prune(trade.sell_id);
        }
    }
};
