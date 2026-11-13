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
    std::vector<Trade> add_order(OrderPointer order);

    void cancel_order(OrderCancel order_cancel);

    std::vector<Trade> modify_order(const OrderModify& order_modify);

    std::string to_string() const;

    OrderPointer get_order(OrderId order_id);

    std::vector<OrderPointer> get_orders();

private:
    std::unordered_map<OrderId, OrderEntry> orders_;
    std::map<Price, std::list<OrderPointer>, std::greater<> > bids_;
    std::map<Price, std::list<OrderPointer>, std::less<> > asks_;

    std::vector<Trade> match_orders(const OrderPointer& aggressor);

    std::vector<Trade> modify_order_price(OrderId order_id, Price new_price);

    void modify_order_quantity(OrderId order_id, Quantity new_quantity);

    bool can_fill_order(OrderPointer order);
};
