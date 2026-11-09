#pragma once

#include <iostream>
#include <boost/container_hash/hash.hpp>
#include <utility>

#include "../order.hpp"
#include "../order_book.hpp"
#include "../order_factory.hpp"

#include "itrade_executor.hpp"
#include "server_types.hpp"

class Exchange {
public:
    explicit Exchange(ITradeExecutor& sink) : trade_executor_(sink) {}

    void handle(ClientId client, const NetworkRequest& req) {
        if (std::holds_alternative<Order>(req))       { add(client, std::get<Order>(req)); return; }
        if (std::holds_alternative<OrderModify>(req)) { modify(client, std::get<OrderModify>(req)); return; }
        if (std::holds_alternative<OrderCancel>(req)) { cancel(client, std::get<OrderCancel>(req)); return; }
        if (std::holds_alternative<ParseError>(req))  { std::cerr << "Err" << std::endl; return; }

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

    ITradeExecutor& trade_executor_;
    OrderFactory order_factory_;
    OrderBook order_book_;
    std::unordered_map<OrderId, Owner>            server_order_id_to_owner_map;
    std::unordered_map<Owner, OrderId, OwnerHash> owner_to_server_order_id_map;

    void add(ClientId client_id, const Order& request) {

        Order order = (request.type == OrderType::Market) // convert client to server order
            ? order_factory_.get_market_order(request.side, request.quantity, request.tif)
            : order_factory_.get_limit_order(request.side, request.quantity, request.price, request.tif);

        // store in maps
        server_order_id_to_owner_map[order.id] = {client_id, request.id};
        owner_to_server_order_id_map[{client_id, request.id}] = order.id;

        std::cout << "Adding Order: " << order.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        auto trades = order_book_.add_order(std::make_shared<Order>(order));
        publish(trades);
    }

    void modify(ClientId c, const OrderModify& request) {
        auto it = owner_to_server_order_id_map.find({c, request.id});
        if (it == owner_to_server_order_id_map.end()) return;

        std::cout << "Modifying Order: " << request.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        OrderModify order_modify = {
            .id = it->second,
            .price = request.price,
            .quantity = request.quantity,
        };
        auto trades = order_book_.modify_order(order_modify);
        publish(trades);
    }

    void cancel(ClientId client_id, const OrderCancel& request) {
        auto it = owner_to_server_order_id_map.find({client_id, static_cast<OrderId>(request)});
        if (it == owner_to_server_order_id_map.end()) return;

        std::cout << "Cancelling Order: " << request << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        OrderCancel order_cancel_id = it->second;
        order_book_.cancel_order(order_cancel_id);
        server_order_id_to_owner_map.erase(it->second);
        owner_to_server_order_id_map.erase(it);
    }

    // Each trade has two sides, and they may belong to different clients
    void publish(std::vector<Trade>& trades) {
        if (trades.empty()) return;

        std::cout << "Trades Executed: " << std::endl;
        for (const Trade& t : trades) {
            std::cout << "\t" << t.to_string() << std::endl;
            auto sell_it = server_order_id_to_owner_map.find(t.sell_id);
            auto buy_it = server_order_id_to_owner_map.find(t.buy_id);

            if (sell_it == server_order_id_to_owner_map.end() || buy_it == server_order_id_to_owner_map.end()) {
                std::cerr << "Err" << std::endl;
                continue;
            }

            const Owner& sell_owner = sell_it->second;
            const Owner& buy_owner = buy_it->second;

            NetworkTradeInfo sell_trade_info = NetworkTradeInfo {
                .order_id = sell_owner.client_order_id,
                .other_client_id = buy_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };
            trade_executor_.on_trade(sell_owner.client_id, sell_trade_info);

            NetworkTradeInfo buy_trade_info = NetworkTradeInfo {
                .order_id = buy_owner.client_order_id,
                .other_client_id = sell_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };

            trade_executor_.on_trade(buy_owner.client_id, buy_trade_info);
        }
        std::cout << std::endl;
    }
};