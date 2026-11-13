#pragma once
#include <list>
#include <map>
#include <unordered_map>
#include <vector>
#include <sstream>

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
        // sanity checks
        if (order->quantity == 0) return {};
        if (get_order(order->id) != nullptr) return {};

        // market orders can be collapsed to be limit orders at the current
        // worst price on the other side.
        if (order->type == OrderType::Market) {
            if (order->side == Side::Buy && !asks_.empty()) {
                const auto& [worst_ask, _] = *asks_.rbegin();
                order->price = worst_ask;
                order->type = OrderType::Limit;
                if (order->tif != TimeInForce::FOK) order->tif = TimeInForce::IOC;
            }
            else if (order->side == Side::Sell && !bids_.empty()) {
                const auto& [worst_bid, _] = *bids_.rbegin();
                order->price = worst_bid;
                order->type = OrderType::Limit;
                if (order->tif != TimeInForce::FOK) order->tif = TimeInForce::IOC;
            }
            else return {};
        }

        // if the FOK order cannot be executed -> do nothing
        if (order->tif == TimeInForce::FOK && !can_fill_order(order)) {
            return {};
        }

        // insert the order into the book
        auto& price_level = order->side == Side::Buy
            ? bids_[order->price]
            : asks_[order->price];

        price_level.push_back(order);
        auto it = std::prev(price_level.end());
        orders_.emplace(order->id, OrderEntry{order, it});

        // try and match the newly added order and process it accordingly
        auto trades = match_orders(order);
        if (order->tif == TimeInForce::IOC && orders_.contains(order->id)) {
            cancel_order(order->id);
        }
        return trades;
    }

    void cancel_order(OrderCancel order_cancel) {
        if (!orders_.contains(order_cancel)) return;

        auto& [order, it] = orders_.at(order_cancel);

        // remove the given order from its associated price level
        if (order->side == Side::Buy) {
            auto& price_level = bids_.at(order->price);
            price_level.erase(it);
            if (price_level.empty()) bids_.erase(order->price);
        }
        else {
            auto& price_level = asks_.at(order->price);
            price_level.erase(it);
            if (price_level.empty()) asks_.erase(order->price);
        }
        orders_.erase(order_cancel);
    }

    std::vector<Trade> modify_order(const OrderModify& order_modify) {
        modify_order_quantity(order_modify.id, order_modify.quantity);
        return modify_order_price(order_modify.id, order_modify.price);
    }

    std::string to_string() const {
        std::stringstream ss;
        ss << "===== ORDER BOOK =====\n";

        ss << "=====    BIDS    =====\n";
        for (const auto& [price_level, bids] : bids_) {
            for (OrderPointer bid : bids) {
                ss << bid->to_string() << "\n";
            }
        }

        ss << "=====    ASKS    =====\n";
        for (const auto& [price_level, asks] : asks_) {
            for (OrderPointer ask : asks) {
                ss << ask->to_string() << "\n";
            }
        }

        ss << std::endl;
        return ss.str();
    }

    OrderPointer get_order(OrderId order_id) {
        if (!orders_.contains(order_id)) return nullptr;

        auto& [order, _] = orders_.at(order_id);
        return order;
    }

    std::vector<OrderPointer> get_orders() {
        std::vector<OrderPointer> orders;
        orders.reserve(asks_.size() + bids_.size());

        for (const auto& [_, bids] : bids_) {
            for (OrderPointer bid : bids) {
                orders.push_back(bid);
            }
        }

        for (const auto& [_, asks] : asks_) {
            for (OrderPointer ask : asks) {
                orders.push_back(ask);
            }
        }

        return orders;
    }

private:
    std::unordered_map<OrderId, OrderEntry> orders_;
    std::map<Price, std::list<OrderPointer>, std::greater<> > bids_;
    std::map<Price, std::list<OrderPointer>, std::less<> > asks_;

    std::vector<Trade> match_orders(const OrderPointer& aggressor) {
        std::vector<Trade> trades;

        // realistically this should be the number of bids, but computing this
        // efficiently based on the current data structures may be too much work
        // compared to just reserving for the total number of orders.
        trades.reserve(orders_.size());
        bool aggressor_is_buy = aggressor->side == Side::Buy;

        while (!bids_.empty() && !asks_.empty()) {
            auto& [bid_price, bids] = *bids_.begin();
            auto& [ask_price, asks] = *asks_.begin();

            // if the spread is positive, we can't match any trades
            if (ask_price - bid_price > 0) break;

            // at the given price level, iterate until one of the bids or asks
            // is empty
            while (!bids.empty() && !asks.empty()) {
                // get the first FIFO bid/ask at the level
                OrderPointer bid = bids.front();
                OrderPointer ask = asks.front();

                // trades execute at the resting order's price
                Price trade_price = aggressor_is_buy ? ask_price : bid->price;

                // get the minimum quantity of the two and fill the orders with
                // those quantities
                Quantity quantity = std::min(bid->quantity, ask->quantity);
                bid->fill(quantity);
                ask->fill(quantity);

                // if either of the orders are now filled -> remove them
                if (bid->quantity == 0) {
                    bids.pop_front();
                    orders_.erase(bid->id);
                }
                if (ask->quantity == 0) {
                    asks.pop_front();
                    orders_.erase(ask->id);
                }

                trades.emplace_back(bid->id, ask->id, quantity, trade_price);
            }

            // remove price levels is empty
            if (bids.empty()) bids_.erase(bid_price);
            if (asks.empty()) asks_.erase(ask_price);
        }

        return trades;
    }

    std::vector<Trade> modify_order_price(OrderId order_id, Price new_price) {
        if (!orders_.contains(order_id)) return {};

        auto [order, _] = orders_.at(order_id);
        if (new_price == order->price) return {};

        // modifying an orders price is just cancelling the order and re-adding
        // it with the new price

        cancel_order(order_id);
        order->price = new_price;
        return add_order(order);
    }

    void modify_order_quantity(OrderId order_id, Quantity new_quantity) {
        if (!orders_.contains(order_id)) return;
        if (new_quantity == 0) {
            cancel_order(order_id);
            return;
        }

        auto& [order, it] = orders_.at(order_id);
        if (new_quantity == order->quantity) return;

        // if the order modification is a reduction in quantity, it should
        // maintain its position in the list, otherwise it should be put to back

        if (new_quantity > order->quantity) {
            // move to back of price level
            auto& price_level = order->side == Side::Buy
                ? bids_[order->price]
                : asks_[order->price];
            price_level.splice(price_level.end(), price_level, it);
        }
        order->quantity = new_quantity;
    }

    bool can_fill_order(OrderPointer order) {
        // could exit early if theres no spread?

        if (order->side == Side::Buy) {
            Quantity fillable_quantity = 0;

            // can probably abstract this logic to maintain another map of:
            // price -> quantity for each level
            for (auto& [price, bids] : asks_) {
                if (price > order->price) break;

                for (OrderPointer bid : bids) {
                    fillable_quantity += bid->quantity;
                }

                if (fillable_quantity >= order->quantity) return true;
            }

            return false;
        }

        // same pattern as above but inverted
        if (order->side == Side::Sell) {
            Quantity fillable_quantity = 0;
            for (auto& [price, bids] : bids_) {
                if (price < order->price) break;

                for (OrderPointer bid : bids) {
                    fillable_quantity += bid->quantity;
                }

                if (fillable_quantity >= order->quantity) return true;
            }

            return false;
        }

        return false;
    }
};
