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
    void run();

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

    void handle_add(const std::vector<std::string>& words);

    void handle_modify(const std::vector<std::string>& words);

    void handle_cancel(const std::vector<std::string>& words);

    // ==================================================
    // util
    // ==================================================

    void print_result(const std::vector<Trade>& trades) const;

    static void print_error(std::string_view message);

    /**
     * Parses a given input string into the templated integral type
     *
     * @tparam T type to parse to
     * @param text to parse
     * @return the integral value
     */
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
