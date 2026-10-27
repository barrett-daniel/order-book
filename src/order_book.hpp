#pragma once
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "order.hpp"
#include "trade.hpp"
#include "types.hpp"


// use list of order pointers because list gives an iterator that cannot be invalidated despite the fact the list can grow very large
struct OrderEntry {
    OrderPointer order;
    std::list<OrderPointer>::iterator it;
};

class OrderBook {
public:
    std::vector<Trade> add_order(OrderPointer order) {
        // market orders can be collapsed to be limit orders at the current
        // worst price on the other side.
        if (order->type == OrderType::Market) {
            if (order->side == Side::Buy && !m_asks.empty()) {
                const auto& [worst_ask, _] = *m_asks.rbegin();
                order->price = worst_ask;
                order->type = OrderType::Limit;
                if (order->tif != TimeInForce::FillOrKill) order->tif = TimeInForce::ImmediateOrCancel;
            }
            else if (order->side == Side::Sell && !m_bids.empty()) {
                const auto& [worst_bid, _] = *m_bids.rbegin();
                order->price = worst_bid;
                order->type = OrderType::Limit;
                if (order->tif != TimeInForce::FillOrKill) order->tif = TimeInForce::ImmediateOrCancel;
            }
            else return {};
        }

        if (order->tif == TimeInForce::FillOrKill && !can_fill_order(order)) {
            return {};
        }

        auto& price_level = order->side == Side::Buy
            ? m_bids[order->price]
            : m_asks[order->price];

        price_level.push_back(order);
        auto it = std::prev(price_level.end());
        m_orders.emplace(order->id, OrderEntry{order, it});

        auto trades = match_orders();
        if (order->tif == TimeInForce::ImmediateOrCancel && m_orders.contains(order->id)) {
            cancel_order(order->id);
        }
        return trades;
    }

    bool can_fill_order(OrderPointer order) {

        // could exit early if theres no spread?

        if (order->side == Side::Buy) {
            Quantity fillable_quantity = 0;

            // can probably abstract this logic to maintain another map of:
            // price -> quantity for each level
            for (auto& [price, bids] : m_asks) {
                if (price > order->price) break;

                for (OrderPointer bid : bids ) {
                    fillable_quantity += bid->quantity;
                }

                if (fillable_quantity >= order->quantity) return true;
            }

            return false;
        }

        // same pattern as above but inverted
        if (order->side == Side::Sell) {
            Quantity fillable_quantity = 0;
            for (auto& [price, bids] : m_bids) {
                if (price < order->price) break;

                for (OrderPointer bid : bids ) {
                    fillable_quantity += bid->quantity;
                }

                if (fillable_quantity >= order->quantity) return true;
            }

            return false;
        }

        return false;
    }

    void cancel_order(OrderCancel order_cancel) {
        if (!m_orders.contains(order_cancel)) return;

        auto& [order, it] = m_orders.at(order_cancel);

        if (order->side == Side::Buy) {
            auto& price_level = m_bids.at(order->price);
            price_level.erase(it);
            if (price_level.empty()) m_bids.erase(order->price);
        }
        else {
            auto& price_level = m_asks.at(order->price);
            price_level.erase(it);
            if (price_level.empty()) m_asks.erase(order->price);
        }
        m_orders.erase(order_cancel);
    }

    std::vector<Trade> modify_order(OrderModify order_modify) {
        modify_order_quantity(order_modify.id, order_modify.quantity);
        return modify_order_price(order_modify.id, order_modify.price);
    }

    std::string to_string() const {
        std::stringstream ss;

        ss << "===== ORDER BOOK =====\n";

        ss << "=====    BIDS    =====\n";
        for (const auto& [price_level, bids] : m_bids) {
            for (OrderPointer bid : bids) {
                ss << bid->to_string() << "\n";
            }
        }

        ss << "=====    ASKS    =====\n";
        for (const auto& [price_level, asks] : m_asks) {
            for (OrderPointer ask : asks) {
                ss << ask->to_string() << "\n";
            }
        }

        ss << std::endl;
        return ss.str();
    }

    OrderPointer get_order(OrderId order_id) {
        if (!m_orders.contains(order_id)) return {};

        auto& [order, _] = m_orders.at(order_id);
        return order;
    }

private:
    std::unordered_map<OrderId, OrderEntry> m_orders;
    std::map<Price, std::list<OrderPointer>, std::greater<>> m_bids;
    std::map<Price, std::list<OrderPointer>, std::less<>> m_asks;

    std::vector<Trade> match_orders() {

        std::vector<Trade> trades;

        // realistically this should be the number of bids, but computing this
        // efficiently based on the current data structures may be too much work
        // compared to just reserving for the total number of orders.
        trades.reserve(m_orders.size());

        while (!m_bids.empty() && !m_asks.empty()) {
            auto& [bid_price, bids] = *m_bids.begin();
            auto& [ask_price, asks] = *m_asks.begin();

            // if the spread is positive, we can't match any trades
            if (ask_price - bid_price > 0) break;

            // at the given price level, iterate until one of the bids or asks
            // is empty
            while (!bids.empty() && !asks.empty()) {
                // get the first FIFO bid/ask at the level
                OrderPointer bid = bids.front();
                OrderPointer ask = asks.front();

                // get the minimum quantity of the two and fill the orders with
                // those quantities
                Quantity quantity = std::min(bid->quantity, ask->quantity);
                bid->fill(quantity);
                ask->fill(quantity);

                // if either of the orders are now filled -> remove them
                if (bid->quantity == 0) {
                    bids.pop_front();
                    m_orders.erase(bid->id);
                }
                if (ask->quantity == 0) {
                    asks.pop_front();
                    m_orders.erase(ask->id);
                }

                trades.emplace_back(bid->id, ask->id, quantity, ask->price);
            }

            // remove price levels is empty
            if (bids.empty()) m_bids.erase(bid_price);
            if (asks.empty()) m_asks.erase(ask_price);
        }

        return trades;
    }

    std::vector<Trade> modify_order_price(OrderId order_id, Price new_price) {
        if (!m_orders.contains(order_id)) return {};

        auto [order, _] = m_orders.at(order_id);
        if (new_price == order->price) return {};

        // modifying an orders price is just cancelling the order and re-adding
        // it with the new price

        cancel_order(order_id);
        order->price = new_price;
        return add_order(order);
    }

    void modify_order_quantity(OrderId order_id, Quantity new_quantity) {
        if (!m_orders.contains(order_id)) return;
        if (new_quantity == 0) cancel_order(order_id);

        auto& [order, it] = m_orders.at(order_id);
        if (new_quantity == order->quantity) return;

        // if the order modification is a reduction in quantity, it should
        // maintain its position in the list, otherwise it should be put to back

        if (new_quantity > order->quantity) {
            // move to back of price level
            auto& price_level = order->side == Side::Buy
                ? m_bids[order->price]
                : m_asks[order->price];
            price_level.splice(price_level.end(), price_level, it);
        }
        order->quantity = new_quantity;
    }
};
