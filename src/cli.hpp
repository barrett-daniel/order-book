#pragma once
#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

#include "order_book.hpp"
#include "order_factory.hpp"

class CLI {
public:
    void run() {
        std::string help_msg =
            "========== ORDER BOOK SIMULATOR ==========\n"
            "A CLI interface is provided to simulate a basic order support LIMIT and MARKET orders.\n"
            "You can: ADD, CANCEL, or MODIFY buy or sell orders. Accepts user input of the following forms\n"
            "\t - \"ADD [BUY or SELL] [quantity] [IOC or FOK or GTC]\"                          : adds a MARKET order to the order book (and executes it)\n"
            "\t - \"ADD [BUY or SELL] [quantity] [IOC or FOK or GTC] @ [price] \"               : adds a LIMIT order to the order book\n"
            "\t - \"CANCEL [order id]\"                                                         : cancel an order\n"
            "\t - \"MODIFY [order id] [PRICE or QUANTITY] [changed value]\"                     : modifies the given order_id price or quantity to the changed value in the order book\n";

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

            if (words[0] == "ADD") handle_add_operation(words);
            else if (words[0] == "MODIFY") handle_modify_operation(words);
            else if (words[0] == "CANCEL") handle_cancel_operation(words);
            else std::cerr << "invalid input:\n" << help_msg;
        }
    }

    void handle_add_operation(const std::vector<std::string>& words) {
        if (words.size() < 4) {
            std::cerr << "invalid input: ADD operation with too few arguments" << std::endl;
            return;
        }

        // parse side
        const std::string& side_str = words[1];
        Side side;
        if (side_str == "BUY") side = Side::Buy;
        else if (side_str == "SELL") side = Side::Sell;
        else {
            std::cerr << "invalid input: ADD operation with unknown side" << std::endl;
            return;
        }

        // quantity
        const std::string& quantity_str = words[2];
        if (!std::all_of(quantity_str.begin(), quantity_str.end(), ::isdigit)) {
            std::cerr << "invalid input: ADD operation with non-numeric quantity" << std::endl;
            return;
        }
        Quantity quantity = std::stol(quantity_str);

        const std::string& tif_str= words[3];
        TimeInForce tif;
        if (tif_str == "IOC") tif = TimeInForce::ImmediateOrCancel;
        else if (tif_str == "FOK") tif = TimeInForce::FillOrKill;
        else if (tif_str == "GTC") tif = TimeInForce::GoodTillCancel;
        else {
            std::cerr << "invalid input: ADD operation with unknown time-in-force" << std::endl;
            return;
        }

        // if market order (inferred based on user input)
        if (words.size() == 4) {
            Order order = order_factory.get_market_order(side, quantity, tif);
            std::vector<Trade> trades = order_book.add_order(std::make_shared<Order>(order));
            std::cout << "Added " << order.to_string() << "\n\n";
            std::cout << "Trades executed:\n";
            for (const auto& trade : trades) {
                std::cout << "\t" << trade.to_string() << "\n";
            }
            std::cout << "\n";
            std::cout << order_book.to_string() << std::endl;
            return;
        }

        // otherwise must be limit order

        // @
        const std::string& at_str = words[4];
        if (at_str != "@") {
            std::cerr << "invalid input: ADD operation of LIMIT order with missing @ symbol" << std::endl;
            return;
        }

        // price
        const std::string& bid_str = words[5];
        if (!std::all_of(bid_str.begin(), bid_str.end(), ::isdigit)) {
            std::cerr << "invalid input: ADD operation of LIMIT order with non-numeric bid" << std::endl;
            return;
        }
        Price bid = std::stol(bid_str);

        // add limit order
        Order order = order_factory.get_limit_order(side, quantity, bid, tif);

        std::vector<Trade> trades = order_book.add_order(std::make_shared<Order>(order));
        std::cout << "Added " << order.to_string() << "\n\n";
        std::cout << "Trades executed:\n";
        for (const auto& trade : trades) {
            std::cout << "\t" << trade.to_string() << "\n";
        }
        std::cout << "\n";
        std::cout << order_book.to_string() << std::endl;
    }

    void handle_modify_operation(const std::vector<std::string>& words) {
        if (words.size() !=  4) {
            std::cerr << "invalid input: MODIFY operation requires 3 additional arguments" << std::endl;
            return;
        }

        // parse id
        const std::string& order_id_str = words[1];
        if (!std::all_of(order_id_str.begin(), order_id_str.end(), ::isdigit)) {
            std::cerr << "invalid input: MODIFY with non-numeric order id" << std::endl;
            return;
        }
        OrderId order_id = std::stol(order_id_str);

        const std::string& modification_type_string = words[2];
        if (modification_type_string != "PRICE" && modification_type_string != "QUANTITY") {
            std::cerr << "invalid input: MODIFY operation with undefined modification type, enter: PRICE or QUANTITY" << std::endl;
            return;
        }

        const std::string& changed_value_str = words[3];
        if (!std::all_of(changed_value_str.begin(), changed_value_str.end(), ::isdigit)) {
            std::cerr << "invalid input: MODIFY operation with non-numeric changed parameter (either the price or quantity)" << std::endl;
            return;
        }

        std::vector<Trade> trades = {};
        OrderModify order_modify{};
        order_modify.id = order_id;

        if (modification_type_string == "PRICE") {
            Price new_price = std::stol(changed_value_str);
            order_modify.price = new_price;

            trades = order_book.modify_order(order_modify);

        } else if (modification_type_string == "QUANTITY") {
            Quantity new_quantity = std::stol(changed_value_str);
            order_modify.quantity = new_quantity;
            trades = order_book.modify_order(order_modify);
        }

        std::cout << "Modified " << order_book.get_order(order_id)->to_string() << "\n\n";
        std::cout << "Trades executed:\n";
        for (const auto& trade : trades) {
            std::cout << "\t" << trade.to_string() << "\n";
        }
        std::cout << "\n";
        std::cout << order_book.to_string() << std::endl;
    }

    void handle_cancel_operation(const std::vector<std::string>& words) {
        if (words.size() != 2) {
            std::cerr << "invalid input: CANCEL operation with too many arguments" << std::endl;
            return;
        }

        // parse id
        const std::string& order_id_str = words[1];
        if (!std::all_of(order_id_str.begin(), order_id_str.end(), ::isdigit)) {
            std::cerr << "invalid input: CANCEL operation with non-numeric order id" << std::endl;
            return;
        }
        OrderId order_id = std::stol(order_id_str);

        order_book.cancel_order(order_id);
        std::cout << "Order cancelled: " << order_id << "\n\n";
        std::cout << order_book.to_string() << std::endl;
    }

private:
    OrderBook order_book;
    OrderFactory order_factory;
};
