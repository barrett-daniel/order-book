#pragma once

#include <charconv>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "order_book.hpp"
#include "order_factory.hpp"

class CLI {
public:
    void run() {
        std::cout << kHelpMessage << std::endl;

        std::string input;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, input)) break;
            std::cout << "\n";

            if (input == "quit" || input == "QUIT" || input == "q") break;

            // tokenise the input
            std::istringstream stream(input);
            std::vector<std::string> words;
            for (std::string word; stream >> word;) words.push_back(std::move(word));
            if (words.empty()) continue;

            // delegate requested operation to appropriate handler
            const auto operation = parse_enum<OrderOperation>(words[0]);
            if (!operation) {
                print_error("unknown operation '" + words[0] + "'");
                continue;
            }

            switch (*operation) {
                case OrderOperation::Add:    handle_add(words);    break;
                case OrderOperation::Modify: handle_modify(words); break;
                case OrderOperation::Cancel: handle_cancel(words); break;
            }
        }
    }

private:
    enum class ModifyField { Price, Quantity };

    static constexpr std::string_view kHelpMessage =
        "========== ORDER BOOK SIMULATOR ==========\n"
        "A CLI for simulating a basic order book supporting LIMIT and MARKET orders.\n"
        "You can ADD, CANCEL, or MODIFY buy or sell orders:\n"
        "\t - \"ADD [BUY|SELL] [quantity] [IOC|FOK|GTC]\"               : add (and execute) a MARKET order\n"
        "\t - \"ADD [BUY|SELL] [quantity] @ [price] [IOC|FOK|GTC]\"     : add a LIMIT order\n"
        "\t - \"CANCEL [order id]\"                                     : cancel an order\n"
        "\t - \"MODIFY [order id] [PRICE|QUANTITY] [new value]\"        : change an order's price or quantity\n"
        "\t - \"quit\"                                                  : exit\n";

    // ==================================================
    // operation handlers
    // ==================================================

    // MARKET: ADD <side> <qty> <tif>               (4 words)
    // LIMIT:  ADD <side> <qty> @ <price> <tif>     (6 words)
    void handle_add(const std::vector<std::string>& words) {
        bool is_market = words.size() == 4;
        bool is_limit = words.size() == 6;
        if (!is_market && !is_limit) {
            print_error("ADD expects 3 arguments (MARKET) or 5 arguments (LIMIT)");
            return;
        }

        auto add_order = [this](const Order& order) {
            const std::vector<Trade> trades = order_book.add_order(std::make_shared<Order>(order));
            std::cout << "Added Order: " << order.to_string() << "\n\n";
            print_result(trades);
        };

        const auto side = parse_enum<Side>(words[1]);
        if (!side) {
            print_error("ADD with unknown side");
            return;
        }

        const auto quantity = parse_number<Quantity>(words[2]);
        if (!quantity) {
            print_error("ADD with invalid quantity");
            return;
        }

        if (is_market) {
            const auto tif = parse_enum<TimeInForce>(words[3]);
            if (!tif) {
                print_error("ADD with unknown time-in-force");
                return;
            }

            add_order(order_factory.get_market_order(*side, *quantity, *tif));
            return;
        }

        // otherwise limit orders

        if (words[3] != "@") {
            print_error("LIMIT order is missing the '@' symbol");
            return;
        }

        const auto price = parse_number<Price>(words[4]);
        if (!price) {
            print_error("ADD with invalid price");
            return;
        }

        const auto tif = parse_enum<TimeInForce>(words[5]);
        if (!tif) {
            print_error("ADD with unknown time-in-force");
            return;
        }

        add_order(order_factory.get_limit_order(*side, *quantity, *price, *tif));
    }

    // MODIFY <order id> <PRICE|QUANTITY> <new value>
    void handle_modify(const std::vector<std::string>& words) {
        if (words.size() != 4) {
            print_error("MODIFY expects 3 arguments: [order id] [PRICE|QUANTITY] [new value]");
            return;
        }

        const auto order_id = parse_number<OrderId>(words[1]);
        if (!order_id) {
            print_error("MODIFY with invalid order id");
            return;
        }

        const auto field = parse_enum<ModifyField>(words[2]);
        if (!field) {
            print_error("MODIFY field must be PRICE or QUANTITY");
            return;
        }

        OrderModify modification{};
        modification.id = *order_id;

        switch (*field) {
            case ModifyField::Price: {
                const auto price = parse_number<Price>(words[3]);
                if (!price) {
                    print_error("MODIFY with invalid price");
                    return;
                }
                modification.price = *price;
                break;
            }
            case ModifyField::Quantity: {
                const auto quantity = parse_number<Quantity>(words[3]);
                if (!quantity) {
                    print_error("MODIFY with invalid quantity");
                    return;
                }
                modification.quantity = *quantity;
                break;
            }
        }

        const std::vector<Trade> trades = order_book.modify_order(modification);
        print_result(trades);
    }

    // CANCEL <order id>
    void handle_cancel(const std::vector<std::string>& words) {
        if (words.size() != 2) {
            print_error("CANCEL expects exactly 1 argument: [order id]");
            return;
        }

        const auto order_id = parse_number<OrderId>(words[1]);
        if (!order_id) {
            print_error("CANCEL with invalid order id");
            return;
        }

        order_book.cancel_order(*order_id);
        std::cout << "Order cancelled: " << *order_id << "\n\n";
        std::cout << order_book.to_string() << std::endl;
    }

    // ==================================================
    // util
    // ==================================================

    void print_result(const std::vector<Trade>& trades) const {
        std::cout << "Trades executed:\n";
        for (const auto& trade : trades) {
            std::cout << "\t" << trade.to_string() << "\n";
        }
        std::cout << "\n" << order_book.to_string() << std::endl;
    }

    static void print_error(std::string_view message) {
        std::cerr << "invalid input: " << message << std::endl;
    }

    template <typename T>
    static std::optional<T> parse_number(std::string_view text) {
        if (text.empty() || text.front() == '-') return std::nullopt;

        T value{};
        const char* start = text.data();
        const char* end = start + text.size();
        const auto result = std::from_chars(start, end, value);

        return result.ec == std::errc() ? std::optional<T>(value) : std::nullopt;
    }

    OrderBook order_book;
    OrderFactory order_factory;
};
