#pragma once

#include <deque>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <utility>

#include "../order.hpp"

#include "constants.hpp"
#include "exchange.hpp"
#include "server_types.hpp"

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket sock, ClientId client_id, Exchange& ex)
        : socket_(std::move(sock))
        , id_(client_id)
        , exchange_(ex) {}

    void start() {
        std::vector<uint8_t> client_accept_msg(MAGIC_LEN + sizeof(ClientId));

        ClientId id = boost::endian::native_to_big(id_);

        std::memcpy(client_accept_msg.data(), &MAGIC, MAGIC_LEN);
        std::memcpy(client_accept_msg.data() + MAGIC_LEN, &id, sizeof(ClientId));
        deliver(client_accept_msg);

        do_read();
    }

    void deliver(std::vector<uint8_t> msg) {
        bool idle = write_buff_.empty();
        write_buff_.push_back(std::move(msg));
        if (idle) do_write();
    }

private:
    tcp::socket socket_;
    ClientId id_;
    Exchange& exchange_;
    std::array<uint8_t, PACKET_LEN> read_buff_{};
    std::deque<std::vector<uint8_t> > write_buff_;

    void do_read() {
        boost::asio::async_read(socket_,boost::asio::buffer(read_buff_),
            std::bind(
                &Session::handle_read,
                this->shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
    }

    void handle_read(const boost::system::error_code& error_code, size_t len) {
        if (error_code) {
            close(error_code);
            return;
        }

        NetworkRequest request = parse_packet(read_buff_.data());
        exchange_.handle(id_, request);
        do_read();
    }

    void do_write() {
        boost::asio::async_write(socket_, boost::asio::buffer(write_buff_.front()),
            std::bind(
                &Session::handle_write,
                this->shared_from_this(),
                boost::asio::placeholders::error
            )
        );
    }

    void handle_write(const boost::system::error_code& error_code) {
        if (error_code) {
            close(error_code);
            return;
        }
        write_buff_.pop_front();
        if (!write_buff_.empty()) do_write();
    }

    void close(const boost::system::error_code& error_code) {
        if (error_code != boost::asio::error::eof) {
            std::cerr << "Session " << id_ << ": " << error_code.message() << '\n';
        }

        boost::system::error_code close_ec;
        socket_.close(close_ec);
        if (close_ec) {
            std::cerr << "Session " << id_ << ": " << close_ec.message() << '\n';
        }
    }

    static NetworkRequest parse_packet(uint8_t* data) {
        if (std::memcmp(data, MAGIC, MAGIC_LEN) != 0) return ParseError("Invalid packet MAGIC");

        uint8_t operation_bits = 0;
        uint8_t side_bits = 0;
        uint8_t order_type_bits = 0;
        uint8_t tif_bits = 0;

        Price price = 0;
        Quantity quantity = 0;
        OrderId order_id = 0;

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

        std::memcpy(&order_id, data + 22, sizeof(order_id));
        std::memcpy(&price, data + 6, sizeof(price));
        std::memcpy(&quantity, data + 14, sizeof(quantity));

        price = boost::endian::big_to_native(price);
        quantity = boost::endian::big_to_native(quantity);
        order_id = boost::endian::big_to_native(order_id);

        switch (*operation) {
            case OrderOperation::Add: return Order{*order_type, *side, quantity, price, order_id, *tif};
            case OrderOperation::Modify: return OrderModify{order_id, price, quantity};
            case OrderOperation::Cancel: return static_cast<OrderCancel>(order_id);
            default: ParseError("Unsupported packet operation");
        }

        // unreachable
        return ParseError("Malformed Packet");
    }
};
