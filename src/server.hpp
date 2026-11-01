#pragma once

#include <iostream>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/bimap.hpp>
#include <utility>

#include "order.hpp"
#include "order_book.hpp"
#include "order_factory.hpp"

using boost::asio::ip::tcp;

constexpr size_t MAGIC_LEN = 4;
constexpr char MAGIC[MAGIC_LEN] = {'%', 'M' , 'O', 'B'};
constexpr int PORT = 8080;
constexpr size_t PACKET_LEN = 30; // bytes

using ParseError = std::string;
using NetworkRequest = std::variant<Order, OrderModify, OrderCancel, ParseError>;

class Server {
public:
    Server() {
        try {
            boost::asio::io_context io_context;
            tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), PORT));
            std::cout << "Server started. Waiting for client..." << std::endl;

            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "Client connected!" << std::endl;

            handle_client(std::move(socket));

        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }

    void handle_client(tcp::socket socket) {
        try {
            uint8_t buff[512];

            while (true) {
                std::memset(buff, 0, sizeof(buff));

                boost::system::error_code error;
                size_t length = socket.read_some(boost::asio::buffer(buff), error);

                if (error == boost::asio::error::eof) break; // Connection closed
                if (error) throw boost::system::system_error(error);
                if (length != PACKET_LEN) continue;

                auto request = bytes_to_request(buff);
                std::vector<Trade> trades = {};
                if (std::holds_alternative<Order>(request)) {
                    Order& order = std::get<Order>(request);
                    std::cout << "Received ADD order request: " << order.to_string() << std::endl;
                    trades = order_book.add_order(std::make_shared<Order>(order));
                }

                if (std::holds_alternative<OrderCancel>(request)) {
                    OrderCancel& order_cancel = std::get<OrderCancel>(request);
                    std::cout << "Received CANCEL order request: " << order_cancel<< std::endl;
                    order_book.cancel_order(order_cancel);
                }

                if (std::holds_alternative<OrderModify>(request)) {
                    OrderModify& order_modify = std::get<OrderModify>(request);
                    std::cout << "Received MODIFY order request: " << order_modify.to_string() << std::endl;
                    trades = order_book.modify_order(order_modify);
                }

                if (std::holds_alternative<ParseError>(request)) {
                    std::cerr << "Received error: " << std::get<ParseError>(request) << std::endl;
                }

                if (!trades.empty()) {
                    std::cout << "Trades executed:\n";
                    for (const auto& trade : trades) {
                        std::cout << "\t" << trade.to_string() << "\n";
                    }
                }
                std::cout << std::endl << order_book.to_string() << std::endl;

                std::vector<uint8_t> trade_byte_data = trades_to_bytes(trades);
                boost::asio::write(socket, boost::asio::buffer(trade_byte_data), error);
            }

        // Catch any exception thrown while communicating with the client.
        } catch (std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }

    NetworkRequest bytes_to_request(uint8_t* data) {
        if (std::memcmp(data, MAGIC, MAGIC_LEN) != 0) return ParseError("Invalid packet MAGIC");

        uint8_t operation_bits = 0;
        uint8_t side_bits = 0;
        uint8_t order_type_bits = 0;
        uint8_t tif_bits = 0;

        Price price = 0;
        Quantity quantity = 0;
        OrderId client_order_id = 0;

        std::memcpy(&operation_bits, data + 4, sizeof(operation_bits));
        operation_bits &= 0b00001111;
        auto operation = parse_enum<OrderOperation>(operation_bits);
        if (!operation) return ParseError("Invalid packet operation");

        std::memcpy(&side_bits, data + 4, sizeof(side_bits));
        side_bits >>= 4;
        auto side = parse_enum<Side>(side_bits);
        if (!side) return ParseError("Invalid packet side");

        std::memcpy(&order_type_bits, data + 5, sizeof(order_type_bits));
        order_type_bits &= 0b00001111;
        auto order_type = parse_enum<OrderType>(order_type_bits);
        if (!order_type) return ParseError("Invalid packet order type");

        std::memcpy(&tif_bits, data + 5, sizeof(tif_bits));
        tif_bits >>= 4;
        auto tif = parse_enum<TimeInForce>(tif_bits);
        if (!tif) return ParseError("Invalid packet time in force");

        std::memcpy(&client_order_id, data + 22, sizeof(client_order_id));
        std::memcpy(&price, data + 6, sizeof(price));
        std::memcpy(&quantity, data + 14, sizeof(quantity));

        price = boost::endian::big_to_native(price);
        quantity = boost::endian::big_to_native(quantity);
        client_order_id = boost::endian::big_to_native(client_order_id);

        switch (*operation) {
            case OrderOperation::Add: {
                Order order{};
                switch (*order_type) {
                    case OrderType::Market: order = order_factory.get_market_order(*side, quantity, *tif); break;
                    case OrderType::Limit:  order = order_factory.get_limit_order(*side, quantity, price, *tif); break;
                    default: return ParseError("Unsupported packet order type");
                }

                client_server_id_map.insert({client_order_id, order.id});
                return order;
            }
            case OrderOperation::Modify: return OrderModify{client_server_id_map.left.at(client_order_id), price, quantity};
            case OrderOperation::Cancel: return static_cast<OrderCancel>(client_server_id_map.left.at(client_order_id));
            default: ParseError("Unsupported packet operation");
        }

        // unreachable
        return ParseError("Malformed Packet");
    }

    std::vector<uint8_t> trades_to_bytes(const std::vector<Trade>& trades) {
        constexpr int RESPONSE_HEADER_SIZE = 6;
        constexpr int TRADE_PACKET_LEN = 32;
        std::vector<uint8_t> bytes(RESPONSE_HEADER_SIZE + trades.size() * TRADE_PACKET_LEN);

        uint16_t num_trades = static_cast<uint16_t>(trades.size());
        num_trades = boost::endian::native_to_big(num_trades);

        std::memcpy(bytes.data(), MAGIC, MAGIC_LEN);
        std::memcpy(bytes.data() + 4, &num_trades, sizeof(num_trades));

        uint8_t* pos = bytes.data() + RESPONSE_HEADER_SIZE;
        for (const Trade& trade : trades) {
            OrderId buy_id = boost::endian::native_to_big(client_server_id_map.right.at(trade.buy_id));
            OrderId sell_id = boost::endian::native_to_big(client_server_id_map.right.at(trade.sell_id));
            Quantity quantity = boost::endian::native_to_big(trade.quantity);
            Price price = boost::endian::native_to_big(trade.price);

            std::memcpy(pos, &buy_id, sizeof(OrderId));
            std::memcpy(pos + 8, &sell_id, sizeof(OrderId));
            std::memcpy(pos + 16, &quantity, sizeof(OrderId));
            std::memcpy(pos + 24, &price, sizeof(OrderId));

            pos += TRADE_PACKET_LEN;
        }
        return bytes;
    }

private:
    OrderFactory order_factory;
    OrderBook order_book;
    boost::bimap<OrderId, OrderId> client_server_id_map; // client <-> server order ids
};