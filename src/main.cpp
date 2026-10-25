#include <algorithm>
#include <cstdint>
#include <format>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

// use list of order pointers because list gives an iterator that cannot be invalidated despite the fact the list can grow very large

enum class OrderType {
    Market,
    Limit
};

enum class Side {
    Buy,
    Sell
};

using Price = int64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;

struct Order {
    OrderType type;
    Side side;
    Quantity quantity;
    Price price;
    OrderId id;

    Order(OrderType type_, Side side_, Quantity quantity_, Price price_, OrderId id_)
        : type{type_}
        , side{side_}
        , quantity{quantity_}
        , price{price_}
        , id{id_}
    {}

    void fill(Quantity quantity_to_fill) {
        if (quantity_to_fill > quantity) {
            throw std::logic_error(std::format(
                "Order: {} cannot be filled with quantity {} because the remaining quantity is only {}",
                id,
                quantity_to_fill,
                quantity
            ));
        }

        quantity -= quantity_to_fill;
    }

    std::string to_string() const {
        if (type == OrderType::Market) {
            return std::format("[Order {}: {} {}]",
                id,
                side == Side::Buy ? "BUY" : "SELL",
                quantity
            );
        }

        // limit
        return std::format("[Order {} : {} {} @ {}]",
            id,
            side == Side::Buy ? "BUY" : "SELL",
            quantity,
            price
        );
    }
};

class OrderFactory {
public:
    Order get_limit_order(Side side_, Quantity quantity_, Price price_) {
        return {OrderType::Limit, side_, quantity_, price_, m_next_id++};
    }

    Order get_market_order(Side side_, Quantity quantity_) {
        return {OrderType::Market, side_, quantity_, std::numeric_limits<Price>::quiet_NaN(), m_next_id++};
    }

private:
    OrderId m_next_id = 0;
};

using OrderPointer = std::shared_ptr<Order>;

struct Trade {
    OrderId buy_id;
    OrderId sell_id;
    Quantity quantity;
    Price price;

    std::string to_string() const {
        return std::format("[Trade : buy_id={} sell_id={}, quantity={}, price={}]",
            buy_id,
            sell_id,
            quantity,
            price
        );
    }
};

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
                const auto [worst_ask, _] = *m_asks.rbegin();
                order->price = worst_ask;
                order->type = OrderType::Limit;
            }
            else if (order->side == Side::Sell && !m_bids.empty()) {
                const auto [worst_bid, _] = *m_bids.rbegin();
                order->price = worst_bid;
                order->type = OrderType::Limit;
            }
            else return {};
        }

        auto& price_level = order->side == Side::Buy
            ? m_bids[order->price]
            : m_asks[order->price];

        price_level.push_back(order);
        auto it = std::prev(price_level.end());
        m_orders.emplace(order->id, OrderEntry{order, it});

        return match_orders();
    }

    void cancel_order(OrderId order_id) {
        if (!m_orders.contains(order_id)) return;

        auto& [order, it] = m_orders.at(order_id);

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
        m_orders.erase(order_id);
    }

    std::vector<Trade> modify_order_price(OrderId order_id, Price new_price) {
        if (!m_orders.contains(order_id)) return {};

        auto [order, _] = m_orders.at(order_id);
        if (new_price == order->price) return {};

        // modifying an orders price is just cancelling the order and re-adding
        // it with the new price

        cancel_order(order_id);
        order->price = new_price;
        add_order(order);
        return match_orders();
    }

    void modify_order_quantity(OrderId order_id, Quantity new_quantity) {
        if (!m_orders.contains(order_id)) return;

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
                    m_orders.erase(bid->id);
                }

                trades.emplace_back(bid->id, ask->id, quantity, ask->price);
            }

            // remove price levels is empty
            if (bids.empty()) m_bids.erase(bid_price);
            if (asks.empty()) m_asks.erase(ask_price);
        }

        return trades;
    }
};

int main() {
    OrderBook order_book;
    OrderFactory order_factory;

    std::string help_msg =
        "========== ORDER BOOK SIMULATOR ==========\n"
        "A CLI interface is provided to simulate a basic order support LIMIT and MARKET orders.\n"
        "You can: ADD, CANCEL, or MODIFY buy or sell orders. Accepts user input of the following forms\n"
        "\t - \"ADD [BUY or SELL] [quantity]\"                          :  adds a MARKET order to the order book (and executes it)\n"
        "\t - \"ADD [BUY or SELL] [quantity] @ [price] \"               : adds a LIMIT order to the order book\n"
        "\t - \"CANCEL [order id]\"                                     : cancel an order\n"
        "\t - \"MODIFY [order id] [PRICE or QUANTITY] [changed value]\" : modifies the given order_id price or quantity to the changed value in the order book\n";

    std::cout << help_msg << std::endl;

    while (true) {
        std::string input;
        std::cout << "> ";
        std::getline(std::cin, input);
        std::cout << "\n";
        std::stringstream ss(input);

        if (input == "quit" || input == "q" || input == "QUIT") break;

        // encode input into space separated words
        std::vector<std::string> words;
        std::string word;
        while (ss >> word) words.push_back(std::move(word));
        if (words.empty()) continue;

        if (words[0] == "ADD") {
            if (words.size() < 3) {
                std::cerr << "invalid input: ADD operation with too few arguments" << std::endl;
                continue;
            }

            // parse side
            const std::string& side_str = words[1];
            Side side;
            if (side_str == "BUY") side = Side::Buy;
            else if (side_str == "SELL") side = Side::Sell;
            else {
                std::cerr << "invalid input: ADD operation with unknown side" << std::endl;
                continue;
            }

            // quantity
            const std::string& quantity_str = words[2];
            if (!std::all_of(quantity_str.begin(), quantity_str.end(), ::isdigit)) {
                std::cerr << "invalid input: ADD operation with non-numeric quantity" << std::endl;
                continue;
            }
            Quantity quantity = std::stol(quantity_str);

            // if market order (inferred based on user input)
            if (words.size() == 3) {
                Order order = order_factory.get_market_order(side, quantity);
                std::vector<Trade> trades = order_book.add_order(std::make_shared<Order>(order));
                std::cout << "Added " << order.to_string() << "\n\n";
                std::cout << "Trades executed:\n";
                for (const auto& trade : trades) {
                    std::cout << "\t" << trade.to_string() << "\n";
                }
                std::cout << "\n";
                std::cout << order_book.to_string() << std::endl;
                continue;
            }

            // otherwise must be limit order

            // @
            const std::string& at_str = words[3];
            if (at_str != "@") {
                std::cerr << "invalid input: ADD operation of LIMIT order with missing @ symbol" << std::endl;
                continue;
            }

            // price
            const std::string& bid_str = words[4];
            if (!std::all_of(bid_str.begin(), bid_str.end(), ::isdigit)) {
                std::cerr << "invalid input: ADD operation of LIMIT order with non-numeric bid" << std::endl;
                continue;
            }
            Price bid = std::stol(bid_str);

            // add limit order
            Order order = order_factory.get_limit_order(side, quantity, bid);

            std::vector<Trade> trades = order_book.add_order(std::make_shared<Order>(order));
            std::cout << "Added " << order.to_string() << "\n\n";
            std::cout << "Trades executed:\n";
            for (const auto& trade : trades) {
                std::cout << "\t" << trade.to_string() << "\n";
            }
            std::cout << "\n";
            std::cout << order_book.to_string() << std::endl;
        }
        else if (words[0] == "MODIFY") {
            if (words.size() !=  4) {
                std::cerr << "invalid input: MODIFY operation requires 3 additional arguments" << std::endl;
                continue;
            }

            // parse id
            const std::string& order_id_str = words[1];
            if (!std::all_of(order_id_str.begin(), order_id_str.end(), ::isdigit)) {
                std::cerr << "invalid input: MODIFY with non-numeric order id" << std::endl;
                continue;
            }
            OrderId order_id = std::stol(order_id_str);

            const std::string& modification_type_string = words[2];
            if (modification_type_string != "PRICE" && modification_type_string != "QUANTITY") {
                std::cerr << "invalid input: MODIFY operation with undefined modification type, enter: PRICE or QUANTITY" << std::endl;
                continue;
            }

            const std::string& changed_value_str = words[3];
            if (!std::all_of(changed_value_str.begin(), changed_value_str.end(), ::isdigit)) {
                std::cerr << "invalid input: MODIFY operation with non-numeric changed parameter (either the price or quantity)" << std::endl;
                continue;
            }

            std::vector<Trade> trades = {};
            if (modification_type_string == "PRICE") {
                Price new_price = std::stol(changed_value_str);
                trades = order_book.modify_order_price(order_id, new_price);

            } else if (modification_type_string == "QUANTITY") {
                Quantity new_quantity = std::stol(changed_value_str);
                order_book.modify_order_quantity(order_id, new_quantity);
            }

            std::cout << "Modified " << order_book.get_order(order_id)->to_string() << "\n\n";
            std::cout << "Trades executed:\n";
            for (const auto& trade : trades) {
                std::cout << "\t" << trade.to_string() << "\n";
            }
            std::cout << "\n";
            std::cout << order_book.to_string() << std::endl;
        }
        else if (words[0] == "CANCEL") {
            if (words.size() != 2) {
                std::cerr << "invalid input: CANCEL operation with too many arguments" << std::endl;
                continue;
            }

            // parse id
            const std::string& order_id_str = words[1];
            if (!std::all_of(order_id_str.begin(), order_id_str.end(), ::isdigit)) {
                std::cerr << "invalid input: CANCEL operation with non-numeric order id" << std::endl;
                continue;
            }
            OrderId order_id = std::stol(order_id_str);

            order_book.cancel_order(order_id);
            std::cout << "Order cancelled: " << order_id << "\n\n";
            std::cout << order_book.to_string() << std::endl;
        }
        else {
            std::cerr << "invalid input:\n" << help_msg;
        }
    }

    return 0;
}
