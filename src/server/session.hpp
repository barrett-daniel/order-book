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
    Session(tcp::socket sock, ClientId client_id, Exchange& ex);

    void start();

    void deliver(std::vector<uint8_t> msg);

private:
    tcp::socket socket_;
    ClientId id_;
    Exchange& exchange_;
    std::array<uint8_t, PACKET_LEN> read_buff_{};
    std::deque<std::vector<uint8_t> > write_buff_;

    void do_read();

    void handle_read(const boost::system::error_code& error_code, size_t len);

    void do_write();

    void handle_write(const boost::system::error_code& error_code);

    void close(const boost::system::error_code& error_code);

    /**
     * Converts some network byte data into an encoded request for the exchange to handle
     *
     * @param data bytes of network request data
     * @return the encoded request
     */
    static NetworkRequest parse_packet(uint8_t* data);
};
