#pragma once

#include <iostream>
#include <boost/asio.hpp>
#include <utility>

#include "constants.hpp"
#include "exchange.hpp"
#include "session.hpp"
#include "session_registry.hpp"
#include "server_types.hpp"

using boost::asio::ip::tcp;

class Server {
public:
    explicit Server(boost::asio::io_context& io);

private:
    boost::asio::io_context& io_;
    tcp::acceptor acceptor_;
    SessionRegistry registry_;
    Exchange exchange_;
    ClientId next_client_id = 0;

    void start_accept();

    void handle_accept(std::shared_ptr<tcp::socket> sock, const boost::system::error_code& error_code);
};
