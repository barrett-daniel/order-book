#include "server.hpp"

Server::Server(boost::asio::io_context& io)
    : io_(io)
    , acceptor_(io, tcp::endpoint(tcp::v4(), PORT))
    , exchange_(registry_)
{
    std::cout << "Server started" << std::endl;
    start_accept();
}

void Server::start_accept() {
    auto sock = std::make_shared<tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock,
       std::bind(
           &Server::handle_accept,
           this,
           sock,
           boost::asio::placeholders::error
       )
    );
}

void Server::handle_accept(std::shared_ptr<tcp::socket> sock, const boost::system::error_code& error_code) {
    if (!error_code) {
        auto session = std::make_shared<Session>(std::move(*sock), next_client_id, exchange_);
        registry_.add(next_client_id, session);
        next_client_id++;
        session->start();
        std::cout << "New Client Connected" << std::endl;
    }
    start_accept(); // recursively accept clients
}
