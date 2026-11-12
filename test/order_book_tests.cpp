#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <vector>

#include "order.hpp"
#include "order_book.hpp"
#include "order_factory.hpp"
#include "trade.hpp"
#include "types.hpp"

OrderFactory factory;

OrderPointer make_limit(Side side, Quantity qty, Price price, TimeInForce tif) {
    return std::make_shared<Order>(factory.get_limit_order(side, qty, price, tif));
}

OrderPointer make_market(Side side, Quantity qty, TimeInForce tif) {
    return std::make_shared<Order>(factory.get_market_order(side, qty, tif));
}

OrderPointer make_resting_order(OrderBook* book, Side side, Quantity qty, Price price) {
    auto order = make_limit(side, qty, price, TimeInForce::GTC);
    REQUIRE(book->add_order(order).empty());      // check no trades were executed
    REQUIRE(book->get_order(order->id) == order); // check the order was added to the book
    return order;
}

Quantity total_quantity(const std::vector<Trade>& trades) {
    Quantity total = 0;
    for (const auto& t : trades) total += t.quantity;
    return total;
}

OrderId resting_id(const OrderPointer& aggressor, const Trade& t) {
    return aggressor->side == Side::Buy ? t.sell_id : t.buy_id;
};


TEST_SUITE("order book - basics") {
    TEST_CASE("empty book") {
        OrderBook book;
        CHECK(book.get_order(0) == nullptr);
    }

    TEST_CASE("non-crossing orders rest") {
        OrderBook book;
        auto bid = make_resting_order(&book, Side::Buy, 10, 99);
        auto ask = make_resting_order(&book, Side::Sell, 10, 100);

        // verify orders are not filled
        CHECK(bid->quantity == 10);
        CHECK(ask->quantity == 10);
        CHECK(book.get_orders().size() == 2);
    }
}

TEST_SUITE("order book - matching") {
    TEST_CASE("orders at the same price fully match") {
        OrderBook book;
        std::shared_ptr<Order> bid;
        std::shared_ptr<Order> ask;
        std::vector<Trade> trades;

        SUBCASE("bid rests, ask arrives") {
            bid = make_resting_order(&book, Side::Buy, 10, 100);
            ask = make_limit(Side::Sell, 10, 100, TimeInForce::GTC);
            trades = book.add_order(ask);
        }

        SUBCASE("ask rests, bid arrives") {
            ask = make_resting_order(&book, Side::Sell, 10, 100);
            bid = make_limit(Side::Buy, 10, 100, TimeInForce::GTC);
            trades = book.add_order(bid);
        }

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == bid->id);
        CHECK(trades[0].sell_id == ask->id);
        CHECK(trades[0].quantity == 10);
        CHECK(trades[0].price == 100);
        CHECK(bid->quantity == 0);
        CHECK(ask->quantity == 0);
        CHECK(!book.get_order(bid->id));
        CHECK(!book.get_order(ask->id));
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("Trades execute at the resting price") {
        OrderBook book;
        std::shared_ptr<Order> bid;
        std::shared_ptr<Order> ask;
        std::vector<Trade> trades;

        SUBCASE("bid rests, ask arrives") {
            bid = make_resting_order(&book, Side::Buy, 10, 100);
            ask = make_limit(Side::Sell, 10, 95, TimeInForce::GTC);
            trades = book.add_order(ask);
        }

        SUBCASE("ask rests, bid arrives") {
            ask = make_resting_order(&book, Side::Sell, 5, 100);
            bid = make_limit(Side::Buy, 5, 105, TimeInForce::GTC);
            trades = book.add_order(bid);
        }

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].price == 100);
    }

    TEST_CASE("partial fill leaves the remainder of the resting order") {
        OrderBook book;
        auto ask = make_resting_order(&book, Side::Sell, 10, 100);
        auto bid = make_limit(Side::Buy, 1, 100, TimeInForce::GTC);
        auto trades = book.add_order(bid);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].quantity == 1);
        CHECK(bid->quantity == 0);
        CHECK(ask->quantity == 9);
        CHECK(book.get_order(ask->id) == ask);
        CHECK(book.get_order(bid->id) == nullptr);
        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("earlier resting orders fill first") {
        OrderBook book;
        auto first_ask = make_resting_order(&book, Side::Sell, 5, 100);
        auto second_ask = make_resting_order(&book, Side::Sell, 5, 100);
        auto bid = make_limit(Side::Buy, 5, 100, TimeInForce::GTC);
        auto trades = book.add_order(bid);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].sell_id == first_ask->id);
        CHECK(first_ask->quantity == 0);
        CHECK(second_ask->quantity == 5);
    }

    TEST_CASE("multiple resting orders can be filled by one aggressor at one price level") {
        OrderBook book;
        auto first_ask = make_resting_order(&book, Side::Sell, 5, 100);
        auto second_ask = make_resting_order(&book, Side::Sell, 5, 100);
        auto bid = make_limit(Side::Buy, 7, 100, TimeInForce::GTC);
        auto trades = book.add_order(bid);

        REQUIRE(trades.size() == 2);
        CHECK(trades[0].sell_id == first_ask->id);
        CHECK(trades[0].quantity == 5);
        CHECK(trades[1].sell_id == second_ask->id);
        CHECK(trades[1].quantity == 2);
        CHECK(second_ask->quantity == 3);
        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("best price fills first") {
        OrderBook book;
        OrderPointer worse;
        OrderPointer better;
        OrderPointer aggressor;

        SUBCASE("asks rest, bid arrives") {
            worse = make_resting_order(&book, Side::Sell, 5, 101);
            better = make_resting_order(&book, Side::Sell, 5, 100);
            aggressor = make_limit(Side::Buy, 5, 101, TimeInForce::GTC);
        }

        SUBCASE("bids rest, ask arrives") {
            worse = make_resting_order(&book, Side::Buy, 5, 99);
            better = make_resting_order(&book, Side::Buy, 5, 100);
            aggressor = make_limit(Side::Sell, 5, 99, TimeInForce::GTC);
        }

        auto trades = book.add_order(aggressor);
        REQUIRE(trades.size() == 1);
        CHECK(resting_id(aggressor, trades[0]) == better->id);
        CHECK(trades[0].price == 100);
        CHECK(better->quantity == 0);
        CHECK(worse->quantity == 5);
    }

    TEST_CASE(
        "multiple resting orders can be filled by one aggressor at multiple price levels (best price fills first)") {
        OrderBook book;
        OrderPointer worse;
        OrderPointer better;
        OrderPointer aggressor;

        SUBCASE("asks rest, bid arrives") {
            worse = make_resting_order(&book, Side::Sell, 5, 101);
            better = make_resting_order(&book, Side::Sell, 5, 100);
            aggressor = make_limit(Side::Buy, 7, 101, TimeInForce::GTC);
        }

        SUBCASE("bids rest, ask arrives") {
            worse = make_resting_order(&book, Side::Buy, 5, 100);
            better = make_resting_order(&book, Side::Buy, 5, 101);
            aggressor = make_limit(Side::Sell, 7, 100, TimeInForce::GTC);
        }

        auto trades = book.add_order(aggressor);
        REQUIRE(trades.size() == 2);

        CHECK(resting_id(aggressor, trades[0]) == better->id);
        CHECK(trades[0].price == better->price);
        CHECK(better->quantity == 0);

        CHECK(resting_id(aggressor, trades[1]) == worse->id);
        CHECK(trades[1].price == worse->price);
        CHECK(worse->quantity == 3);

        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("aggressor never trades through its limit price") {
        OrderBook book;
        auto near = make_resting_order(&book, Side::Sell, 5, 100);
        auto far = make_resting_order(&book, Side::Sell, 5, 102);

        auto bid = make_limit(Side::Buy, 10, 101, TimeInForce::GTC);
        auto trades = book.add_order(bid);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].sell_id == near->id);
        CHECK(near->quantity == 0);
        CHECK(far->quantity == 5);
        CHECK(bid->quantity == 5);
        CHECK(book.get_order(bid->id) == bid); // check bid is still in book
        CHECK(book.get_orders().size() == 2); // 2 resting orders, the unfilled 'far' order and the remainder of the bit
    }
}

TEST_SUITE("order book - cancel") {
    TEST_CASE("cancel removes order") {
        OrderBook book;
        OrderPointer order;

        SUBCASE("bid") {
            order = make_resting_order(&book, Side::Buy, 10, 100);
        }

        SUBCASE("ask") {
            order = make_resting_order(&book, Side::Sell, 10, 100);
        }

        book.cancel_order(order->id);
        CHECK(book.get_order(order->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("a cancelled order can no longer be matched") {
        OrderBook book;
        auto ask = make_resting_order(&book, Side::Sell, 10, 100);
        auto bid = make_limit(Side::Buy, 10, 100, TimeInForce::GTC);

        book.cancel_order(ask->id);
        auto trades = book.add_order(bid);

        CHECK(trades.empty());                 // check no trades were executed
        CHECK(book.get_order(bid->id) == bid); // check bid still in book
    }

    TEST_CASE("cancelling order preserves FIFO price level") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Sell, 5, 100);
        auto b = make_resting_order(&book, Side::Sell, 5, 100);
        auto c = make_resting_order(&book, Side::Sell, 5, 100);

        book.cancel_order(b->id);
        auto aggressor = make_limit(Side::Buy, 10, 100, TimeInForce::GTC);
        auto trades = book.add_order(aggressor);

        REQUIRE(trades.size() == 2);
        CHECK(trades[0].sell_id == a->id);
        CHECK(trades[1].sell_id == c->id);
        CHECK(b->quantity == 5);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("cancelling head of queue pushes 2nd order to head") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Sell, 5, 100);
        auto b = make_resting_order(&book, Side::Sell, 5, 100);
        auto c = make_resting_order(&book, Side::Sell, 5, 100);

        book.cancel_order(a->id);
        auto aggressor = make_limit(Side::Buy, 5, 100, TimeInForce::GTC);
        auto trades = book.add_order(aggressor);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == aggressor->id);
        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("cancelling the best level exposes the next level") {
        OrderBook book;
        auto best = make_resting_order(&book, Side::Sell, 5, 100);
        auto next = make_resting_order(&book, Side::Sell, 5, 101);

        book.cancel_order(best->id);
        auto aggressor = make_limit(Side::Buy, 5, 101, TimeInForce::GTC);
        auto trades = book.add_order(aggressor);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].sell_id == next->id);
        CHECK(trades[0].price == 101);
    }

    TEST_CASE("a price level can be re-created") {
        OrderBook book;
        auto first = make_resting_order(&book, Side::Sell, 5, 100);
        book.cancel_order(first->id);

        auto second = make_resting_order(&book, Side::Sell, 5, 100);
        auto aggressor = make_limit(Side::Buy, 5, 100, TimeInForce::GTC);
        auto trades = book.add_order(aggressor);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].sell_id == second->id);
    }

    TEST_CASE("cancelling a partially filled order removes the remainder") {
        OrderBook book;
        auto ask = make_resting_order(&book, Side::Sell, 10, 100);
        auto bid = make_limit(Side::Buy, 4, 100, TimeInForce::GTC);

        book.add_order(bid);
        REQUIRE(ask->quantity == 6);

        book.cancel_order(ask->id);
        CHECK(book.get_order(ask->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }
}

TEST_SUITE("order book - FOK") {
    TEST_CASE("FOK fills with matching resting quantity") {
        OrderBook book;
        OrderPointer fok;

        SUBCASE("ask rests, bid arrives") {
            make_resting_order(&book, Side::Sell, 10, 100);
            fok = make_limit(Side::Buy, 10, 100, TimeInForce::FOK);
        }

        SUBCASE("bid rests, ask arrives") {
            make_resting_order(&book, Side::Buy, 10, 100);
            fok = make_limit(Side::Sell, 10, 100, TimeInForce::FOK);
        }

        auto trades = book.add_order(fok);
        CHECK(total_quantity(trades) == 10);
        CHECK(fok->quantity == 0);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("FOK fills across several levels") {
        OrderBook book;
        OrderPointer fok;

        SUBCASE("asks rest, bid arrives") {
            make_resting_order(&book, Side::Sell, 5, 100);
            make_resting_order(&book, Side::Sell, 5, 101);
            fok = make_limit(Side::Buy, 10, 101, TimeInForce::FOK);
        }

        SUBCASE("bids rest, ask arrives") {
            make_resting_order(&book, Side::Buy, 5, 101);
            make_resting_order(&book, Side::Buy, 5, 100);
            fok = make_limit(Side::Sell, 10, 100, TimeInForce::FOK);
        }

        auto trades = book.add_order(fok);
        CHECK(total_quantity(trades) == 10);
        CHECK(fok->quantity == 0);
        CHECK(book.get_order(fok->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("FOK kills on an empty book") {
        OrderBook book;
        OrderPointer fok;

        SUBCASE("buy") {
            fok = make_limit(Side::Buy, 10, 100, TimeInForce::FOK);
        }

        SUBCASE("sell") {
            fok = make_limit(Side::Sell, 10, 100, TimeInForce::FOK);
        }

        CHECK(book.add_order(fok).empty());
        CHECK_FALSE(book.get_order(fok->id));
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("FOK kills with non-matching resting quantity") {
        OrderBook book;
        OrderPointer resting;
        OrderPointer fok;

        SUBCASE("asks rest, bid arrives") {
            resting = make_resting_order(&book, Side::Sell, 5, 100);
            fok = make_limit(Side::Buy, 10, 100, TimeInForce::FOK);
        }

        SUBCASE("bids rest, ask arrives") {
            resting = make_resting_order(&book, Side::Buy, 5, 100);
            fok = make_limit(Side::Sell, 10, 100, TimeInForce::FOK);
        }

        auto trades = book.add_order(fok);
        CHECK(trades.empty());
        CHECK(fok->quantity == 10);
        CHECK(book.get_order(fok->id) == nullptr);
        CHECK(resting->quantity == 5);
        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("FOK ignores orders beyond its price level") {
        OrderBook book;
        OrderPointer near;
        OrderPointer far;
        OrderPointer fok;

        SUBCASE("buy ignores asks above its limit") {
            near = make_resting_order(&book, Side::Sell, 5, 100);
            far = make_resting_order(&book, Side::Sell, 5, 102);
            fok = make_limit(Side::Buy, 10, 101, TimeInForce::FOK);
        }

        SUBCASE("sell ignores bids below its limit") {
            near = make_resting_order(&book, Side::Buy, 5, 101);
            far = make_resting_order(&book, Side::Buy, 5, 99);
            fok = make_limit(Side::Sell, 10, 100, TimeInForce::FOK);
        }

        auto trades = book.add_order(fok);
        CHECK(trades.empty());
        CHECK(near->quantity == 5);
        CHECK(far->quantity == 5);
        CHECK_FALSE(book.get_order(fok->id));
        CHECK(book.get_orders().size() == 2);
    }
}

TEST_SUITE("order book - IOC") {
    TEST_CASE("IOC fills") {
        OrderBook book;
        make_resting_order(&book, Side::Sell, 10, 100);
        auto trades = book.add_order(make_limit(Side::Buy, 10, 100, TimeInForce::IOC));
        CHECK(total_quantity(trades) == 10);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("IOC partially fills and the rest is cancelled") {
        OrderBook book;
        make_resting_order(&book, Side::Sell, 4, 100);
        auto ioc = make_limit(Side::Buy, 10, 100, TimeInForce::IOC);
        auto trades = book.add_order(ioc);

        CHECK(total_quantity(trades) == 4);
        CHECK(ioc->quantity == 6);
        CHECK(book.get_order(ioc->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("IOC on an empty book cancels") {
        OrderBook book;
        auto ioc = make_limit(Side::Sell, 10, 100, TimeInForce::IOC);
        auto trades = book.add_order(ioc);
        CHECK(trades.empty());
        CHECK(book.get_order(ioc->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("IOC which cannot be filled at all leaves book unchanged") {
        OrderBook book;
        auto ask = make_resting_order(&book, Side::Sell, 5, 100);
        auto ioc = make_limit(Side::Buy, 5, 99, TimeInForce::IOC);
        auto trades = book.add_order(ioc);

        CHECK(trades.empty());
        CHECK(book.get_order(ioc->id) == nullptr);
        CHECK(ask->quantity == 5);
        CHECK(book.get_orders().size() == 1);
    }
}

TEST_SUITE("order book - market orders") {
    TEST_CASE("market order which cannot be matched does nothing") {
        OrderBook book;
        OrderPointer market;

        SUBCASE("buy with no asks") {
            market = make_market(Side::Buy, 10, TimeInForce::IOC);
        }

        SUBCASE("sell with no bids") {
            market = make_market(Side::Sell, 10, TimeInForce::IOC);
        }

        auto trades = book.add_order(market);
        CHECK(trades.empty());
        CHECK_FALSE(book.get_order(market->id));
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("market order fills against a resting order") {
        OrderBook book;
        OrderPointer resting;
        OrderPointer market;

        SUBCASE("buy against an ask") {
            resting = make_resting_order(&book, Side::Sell, 10, 100);
            market = make_market(Side::Buy, 10, TimeInForce::IOC);
        }

        SUBCASE("sell against a bid") {
            resting = make_resting_order(&book, Side::Buy, 10, 100);
            market = make_market(Side::Sell, 10, TimeInForce::IOC);
        }

        auto trades = book.add_order(market);
        REQUIRE(trades.size() == 1);
        CHECK(resting_id(market, trades[0]) == resting->id);
        CHECK(trades[0].quantity == 10);
        CHECK(trades[0].price == 100);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("market order sweeps levels best-first at each resting price and leaves the rest") {
        OrderBook book;
        OrderPointer best, next, worst;
        OrderPointer market;

        SUBCASE("buy sweeps asks") {
            best = make_resting_order(&book, Side::Sell, 5, 100);
            next = make_resting_order(&book, Side::Sell, 5, 101);
            worst = make_resting_order(&book, Side::Sell, 5, 102);
            market = make_market(Side::Buy, 8, TimeInForce::IOC);
        }

        SUBCASE("sell sweeps bids") {
            best = make_resting_order(&book, Side::Buy, 5, 102);
            next = make_resting_order(&book, Side::Buy, 5, 101);
            worst = make_resting_order(&book, Side::Buy, 5, 100);
            market = make_market(Side::Sell, 8, TimeInForce::IOC);
        }

        auto trades = book.add_order(market);

        REQUIRE(trades.size() == 2);
        CHECK(resting_id(market, trades[0]) == best->id);
        CHECK(trades[0].quantity == 5);
        CHECK(trades[0].price == best->price);
        CHECK(resting_id(market, trades[1]) == next->id);
        CHECK(trades[1].quantity == 3);
        CHECK(trades[1].price == next->price);

        CHECK(best->quantity == 0);
        CHECK(next->quantity == 2);
        CHECK(worst->quantity == 5);
        CHECK(book.get_order(market->id) == nullptr);
        CHECK(book.get_orders().size() == 2);
    }

    TEST_CASE("Unfilled remainder of market order is cancelled") {
        OrderBook book;
        OrderPointer market;

        SUBCASE("buy") {
            make_resting_order(&book, Side::Sell, 10, 100);
            market = make_market(Side::Buy, 25, TimeInForce::IOC);
        }

        SUBCASE("sell") {
            make_resting_order(&book, Side::Buy, 10, 100);
            market = make_market(Side::Sell, 25, TimeInForce::IOC);
        }

        auto trades = book.add_order(market);

        CHECK(total_quantity(trades) == 10);
        CHECK(market->quantity == 15);
        CHECK(book.get_order(market->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("market FOK is still FOK") {
        OrderBook book;
        OrderPointer a, b;
        Side side;

        SUBCASE("buy") {
            side = Side::Buy;
            a = make_resting_order(&book, Side::Sell, 5, 100);
            b = make_resting_order(&book, Side::Sell, 3, 101);
        }

        SUBCASE("sell") {
            side = Side::Sell;
            a = make_resting_order(&book, Side::Buy, 5, 101);
            b = make_resting_order(&book, Side::Buy, 3, 100);
        }

        // insufficient liquidity: rejected, book untouched
        auto fok_fail = make_market(side, 10, TimeInForce::FOK);
        auto fok_fail_trades = book.add_order(fok_fail);
        CHECK(fok_fail_trades.empty());
        CHECK(a->quantity == 5);
        CHECK(b->quantity == 3);
        CHECK(book.get_orders().size() == 2);

        auto fok_success = make_market(side, 8, TimeInForce::FOK);
        auto trades = book.add_order(fok_success);
        CHECK(total_quantity(trades) == 8);
        CHECK(book.get_orders().size() == 0);
    }
}

TEST_SUITE("order book - modify") {
    TEST_CASE("modifying an unknown order does nothing") {
        OrderBook book;
        make_resting_order(&book, Side::Buy, 10, 100);

        auto modify = OrderModify{9999, 1, 1};
        auto trades = book.modify_order(modify);
        CHECK(trades.empty());
        CHECK(book.get_orders().size() == 1);
    }

    TEST_CASE("reducing quantity updates keeps queue priority") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Buy, 10, 100);
        auto b = make_resting_order(&book, Side::Buy, 10, 100);
        auto modify = OrderModify{a->id, 100, 5};
        auto trades = book.modify_order(modify);

        CHECK(trades.empty());
        CHECK(a->quantity == 5);

        trades = book.add_order(make_limit(Side::Sell, 5, 100, TimeInForce::GTC));
        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == a->id);
        CHECK(b->quantity == 10);
    }

    TEST_CASE("increasing quantity pushes order to back of queue") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Buy, 10, 100);
        auto b = make_resting_order(&book, Side::Buy, 10, 100);
        auto modify = OrderModify{a->id, 100, 15};
        auto trades = book.modify_order(modify);


        CHECK(trades.empty());
        CHECK(a->quantity == 15);

        trades = book.add_order(make_limit(Side::Sell, 5, 100, TimeInForce::GTC));
        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == b->id);
    }

    TEST_CASE("unchanged modify operation does nothing") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Buy, 10, 100);
        auto _ = make_resting_order(&book, Side::Buy, 10, 100);
        auto modify = OrderModify{a->id, 100, 10};
        auto trades = book.modify_order(modify);

        CHECK(trades.empty());

        trades = book.add_order(make_limit(Side::Sell, 5, 100, TimeInForce::GTC));
        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == a->id);
    }

    TEST_CASE("modify price changes price level") {
        OrderBook book;
        auto bid = make_resting_order(&book, Side::Buy, 10, 100);
        auto modify = OrderModify{bid->id, 98, 10};
        auto trades = book.modify_order(modify);

        CHECK(trades.empty());
        CHECK(bid->price == 98);
        CHECK(book.get_order(bid->id) == bid);
        CHECK(book.get_orders().size() == 1);

        // now adding an order which previously matched, but no longer does should not do anything to the book
        auto ask_fail = make_limit(Side::Sell, 10, 99, TimeInForce::GTC);
        trades = book.add_order(ask_fail);
        CHECK(trades.empty());

        // now adding an order which does match the new price should be filled
        auto ask_success = make_limit(Side::Sell, 10, 98, TimeInForce::GTC);
        trades = book.add_order(ask_success);
        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == bid->id);
    }

    TEST_CASE("double modification looses priority") {
        OrderBook book;
        auto a = make_resting_order(&book, Side::Buy, 5, 100);
        auto b = make_resting_order(&book, Side::Buy, 5, 100);

        book.modify_order(OrderModify{a->id, 99, 5});
        book.modify_order(OrderModify{a->id, 100, 5});

        auto aggressor = make_limit(Side::Sell, 5, 100, TimeInForce::GTC);
        auto trades = book.add_order(aggressor);
        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == b->id);
    }

    TEST_CASE("modifying a price through the spread trades") {
        OrderBook book;
        OrderPointer bid;
        OrderPointer ask;
        OrderPointer aggressor_modified;

        SUBCASE("bid price up") {
            ask = make_resting_order(&book, Side::Sell, 5, 100);
            bid = make_resting_order(&book, Side::Buy, 5, 99);
            aggressor_modified = bid;
        }

        SUBCASE("ask price down") {
            bid = make_resting_order(&book, Side::Buy, 5, 100);
            ask = make_resting_order(&book, Side::Sell, 5, 101);
            aggressor_modified = ask;
        }

        auto modify = OrderModify{aggressor_modified->id, 100, 5};
        auto trades = book.modify_order(modify);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].buy_id == bid->id);
        CHECK(trades[0].sell_id == ask->id);
        CHECK(trades[0].quantity == 5);
        CHECK(trades[0].price == 100);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("quantity and price can be modified together") {
        OrderBook book;
        auto ask = make_resting_order(&book, Side::Sell, 4, 100);
        auto bid = make_resting_order(&book, Side::Buy, 10, 99);
        auto modify = OrderModify{bid->id, 101, 4};
        auto trades = book.modify_order(modify);

        REQUIRE(trades.size() == 1);
        CHECK(trades[0].quantity == 4);
        CHECK(trades[0].buy_id == bid->id);
        CHECK(trades[0].sell_id == ask->id);
        CHECK(book.get_orders().size() == 0);
    }

    TEST_CASE("modifying quantity to zero cancels the order") {
        OrderBook book;
        auto bid = make_resting_order(&book, Side::Buy, 10, 100);
        auto modify = OrderModify{bid->id, 100, 0};
        auto _ = book.modify_order(modify);

        CHECK(book.get_order(bid->id) == nullptr);
        CHECK(book.get_orders().size() == 0);
    }
}
