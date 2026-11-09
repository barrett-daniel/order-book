#pragma once

#include <deque>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/bimap.hpp>
#include <boost/container_hash/hash.hpp>
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
using ClientId = uint64_t;

struct NetworkTradeInfo {
    OrderId order_id;
    ClientId other_client_id;
    Quantity quantity;
    Price price;
};

inline NetworkRequest parse_packet(uint8_t* data) {
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

        price    = boost::endian::big_to_native(price);
        quantity = boost::endian::big_to_native(quantity);
        order_id = boost::endian::big_to_native(order_id);

        switch (*operation) {
            case OrderOperation::Add:  return Order{*order_type, *side ,quantity, price, order_id, *tif};
            case OrderOperation::Modify: return OrderModify{order_id, price, quantity};
            case OrderOperation::Cancel: return static_cast<OrderCancel>(order_id);
            default: ParseError("Unsupported packet operation");
        }

        // unreachable
        return ParseError("Malformed Packet");
}

inline std::vector<uint8_t> encode(const NetworkTradeInfo& trade) {
    constexpr int RESPONSE_SIZE = 36;
    std::vector<uint8_t> bytes(RESPONSE_SIZE);

    std::memcpy(bytes.data(), MAGIC, MAGIC_LEN);

    OrderId order_id = boost::endian::native_to_big(trade.order_id);
    ClientId other_client_id = boost::endian::native_to_big(trade.other_client_id);
    Quantity quantity = boost::endian::native_to_big(trade.quantity);
    Price price = boost::endian::native_to_big(trade.price);

    std::memcpy(bytes.data() + 4, &order_id, sizeof(OrderId));
    std::memcpy(bytes.data() + 12, &other_client_id, sizeof(OrderId));
    std::memcpy(bytes.data() + 20, &quantity, sizeof(Quantity));
    std::memcpy(bytes.data() + 28, &price, sizeof(Price));

    return bytes;
}

class ITradeExecutor {
public:
    virtual ~ITradeExecutor() = default;
    virtual void on_trade(ClientId cid, const NetworkTradeInfo&) = 0;
};

class Exchange {
public:
    explicit Exchange(ITradeExecutor& sink) : trade_executor_(sink) {}

    void handle(ClientId client, const NetworkRequest& req) {
        if (std::holds_alternative<Order>(req))       { add(client, std::get<Order>(req)); return; }
        if (std::holds_alternative<OrderModify>(req)) { modify(client, std::get<OrderModify>(req)); return; }
        if (std::holds_alternative<OrderCancel>(req)) { cancel(client, std::get<OrderCancel>(req)); return; }
        if (std::holds_alternative<ParseError>(req))  { std::cerr << "Err" << std::endl; return; }

        std::cerr << "error" << std::endl;
    }

private:
    struct Owner {
        ClientId client_id;
        OrderId client_order_id;

        bool operator==(const Owner&) const = default;
    };

    struct OwnerHash {
        std::size_t operator()(const Owner& owner) const {
            std::size_t seed = 0;
            boost::hash_combine(seed, owner.client_id);
            boost::hash_combine(seed, owner.client_order_id);
            return seed;
        }
    };

    ITradeExecutor& trade_executor_;
    OrderFactory order_factory_;
    OrderBook order_book_;
    std::unordered_map<OrderId, Owner>            server_order_id_to_owner_map;
    std::unordered_map<Owner, OrderId, OwnerHash> owner_to_server_order_id_map;

    void add(ClientId c, const Order& request) {

        Order order = (request.type == OrderType::Market) // convert client to server order
            ? order_factory_.get_market_order(request.side, request.quantity, request.tif)
            : order_factory_.get_limit_order(request.side, request.quantity, request.price, request.tif);

        // store in maps
        server_order_id_to_owner_map[order.id] = {c, request.id};
        owner_to_server_order_id_map[{c, request.id}] = order.id;

        std::cout << "Adding Order: " << order.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        auto trades = order_book_.add_order(std::make_shared<Order>(order));
        publish(trades);
    }

    void modify(ClientId c, const OrderModify& request) {
        auto it = owner_to_server_order_id_map.find({c, request.id});
        if (it == owner_to_server_order_id_map.end()) return;

        std::cout << "Modifying Order: " << request.to_string() << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        OrderModify order_modify = {
            .id = it->second,
            .price = request.price,
            .quantity = request.quantity,
        };
        auto trades = order_book_.modify_order(order_modify);
        publish(trades);
    }

    void cancel(ClientId c, const OrderCancel& request) {
        auto it = owner_to_server_order_id_map.find({c, static_cast<OrderId>(request)});
        if (it == owner_to_server_order_id_map.end()) return;

        std::cout << "Cancelling Order: " << request << std::endl;
        std::cout << order_book_.to_string() << std::endl;

        OrderCancel order_cancel_id = it->second;
        order_book_.cancel_order(order_cancel_id);
        server_order_id_to_owner_map.erase(it->second);
        owner_to_server_order_id_map.erase(it);
    }

    // Each trade has two sides, and they may belong to different clients
    void publish(std::vector<Trade>& trades) {
        if (trades.empty()) return;

        std::cout << "Trades Executed: " << std::endl;
        for (const Trade& t : trades) {
            std::cout << "\t" << t.to_string() << std::endl;
            auto sell_it = server_order_id_to_owner_map.find(t.sell_id);
            auto buy_it = server_order_id_to_owner_map.find(t.buy_id);

            if (sell_it == server_order_id_to_owner_map.end() || buy_it == server_order_id_to_owner_map.end()) {
                std::cerr << "Err" << std::endl;
                continue;
            }

            const Owner& sell_owner = sell_it->second;
            const Owner& buy_owner = buy_it->second;

            NetworkTradeInfo sell_trade_info = NetworkTradeInfo {
                .order_id = sell_owner.client_order_id,
                .other_client_id = buy_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };
            trade_executor_.on_trade(sell_owner.client_id, sell_trade_info);

            NetworkTradeInfo buy_trade_info = NetworkTradeInfo {
                .order_id = buy_owner.client_order_id,
                .other_client_id = sell_owner.client_id,
                .quantity = t.quantity,
                .price = t.price,
            };

            trade_executor_.on_trade(buy_owner.client_id, buy_trade_info);
        }
        std::cout << std::endl;
    }
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket sock, ClientId id, Exchange& ex)
        : socket_(std::move(sock)), id_(id), exchange_(ex) {}

    void start() {
        std::vector<uint8_t> client_accept_msg(MAGIC_LEN + sizeof(ClientId));

        ClientId id = boost::endian::native_to_big(id_);

        std::memcpy(client_accept_msg.data(), &MAGIC, MAGIC_LEN);
        std::memcpy(client_accept_msg.data() + MAGIC_LEN, &id, sizeof(ClientId));
        deliver(client_accept_msg);

        do_read();
    }
    
    void deliver(std::vector<uint8_t> msg) {
        bool idle = out_.empty();
        out_.push_back(std::move(msg));
        if (idle) do_write();
    }

private:
    void do_read() {
        boost::asio::async_read(socket_, boost::asio::buffer(rx_), std::bind(
            &Session::handle_read,
            this->shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred
        ));
    }

    void handle_read(const boost::system::error_code& error_code, size_t len) {
        if (error_code) {
            close(error_code); 
            return;
        }
        
        NetworkRequest request = parse_packet(rx_.data());
        exchange_.handle(id_, request);
        do_read();
    }

    void do_write() {
        boost::asio::async_write(socket_, boost::asio::buffer(out_.front()), std::bind(
            &Session::handle_write,
            this->shared_from_this(),
            boost::asio::placeholders::error
        ));
    }
    
    void handle_write(const boost::system::error_code& error_code) {
        if (error_code) {
            close(error_code); 
            return;
        }
        out_.pop_front();
        if (!out_.empty()) do_write();
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

    tcp::socket socket_;
    ClientId id_;
    Exchange& exchange_;
    std::array<uint8_t, PACKET_LEN> rx_{};
    std::deque<std::vector<uint8_t>> out_;   // deque: front() stays valid while queued
};

class SessionRegistry : public ITradeExecutor {
public:
    void add(ClientId id, std::weak_ptr<Session> s) {
        sessions_[id] = std::move(s);
    }

    void remove(ClientId id) {
        sessions_.erase(id);
    }

    void on_trade(ClientId cid, const NetworkTradeInfo& t) override {
        if (auto sell_session = find(cid)) {
            sell_session->deliver(encode(t));
        }
    }

private:
    std::shared_ptr<Session> find(ClientId id) {
        auto it = sessions_.find(id);
        return it == sessions_.end() ? nullptr : it->second.lock();
    }
    std::unordered_map<ClientId, std::weak_ptr<Session>> sessions_;
};

class Server {
public:
    explicit Server(boost::asio::io_context& io)
        : io_(io), acceptor_(io, tcp::endpoint(tcp::v4(), PORT)), exchange_(registry_)
    {
        std::cout << "Server started" << std::endl;
        start_accept();
    }
private:
    void start_accept() {
        acceptor_.async_accept([this](auto error_code, tcp::socket sock) {
            if (!error_code) {
                auto session = std::make_shared<Session>(std::move(sock), next_client_, exchange_);
                registry_.add(next_client_, session);
                next_client_++;
                session->start();
                std::cout << "New Client Connected" << std::endl;
            }
            start_accept();
        });
    }

    boost::asio::io_context& io_;
    tcp::acceptor   acceptor_;
    SessionRegistry registry_;   // declared before exchange_, which holds a reference to it
    Exchange        exchange_;
    ClientId        next_client_ = 0;
};