#include "order_book.h"

#include <iostream>
#include <cassert>

using namespace atlas;

// Minimal check macro: prints the failing condition and line, then keeps going.
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            std::cerr << "FAILED: " << #cond << "  (line " << __LINE__     \
                      << ")\n";                                            \
        }                                                                  \
    } while (0)

static Order limit(OrderId id, Side s, Price p, Quantity q, uint64_t ts) {
    return Order{id, s, Type::Limit, p, q, ts};
}

// An order that doesn't cross rests in the book with no trades.
static void test_no_cross_rests() {
    OrderBook book;
    auto t = book.add_order(limit(1, Side::Buy, 100, 10, 1));
    CHECK(t.empty());
    CHECK(book.resting_order_count() == 1);
    Price bid;
    CHECK(book.best_bid(bid) && bid == 100);
}

// A sell that crosses a resting buy should fill at the resting (maker) price.
static void test_simple_cross() {
    OrderBook book;
    book.add_order(limit(1, Side::Buy, 100, 10, 1));
    auto t = book.add_order(limit(2, Side::Sell, 100, 10, 2));
    CHECK(t.size() == 1);
    CHECK(t[0].price == 100);
    CHECK(t[0].quantity == 10);
    CHECK(t[0].maker_id == 1);
    CHECK(t[0].taker_id == 2);
    CHECK(book.resting_order_count() == 0); // both fully filled
}

// Price priority: an incoming buy should hit the cheapest ask first.
static void test_price_priority() {
    OrderBook book;
    book.add_order(limit(1, Side::Sell, 102, 5, 1));
    book.add_order(limit(2, Side::Sell, 101, 5, 2)); // cheaper, should fill first
    auto t = book.add_order(limit(3, Side::Buy, 102, 5, 3));
    CHECK(t.size() == 1);
    CHECK(t[0].price == 101);   // matched the cheaper ask
    CHECK(t[0].maker_id == 2);
}

// Time priority: two orders at the same price fill oldest-first (FIFO).
static void test_time_priority() {
    OrderBook book;
    book.add_order(limit(1, Side::Buy, 100, 5, 1)); // older
    book.add_order(limit(2, Side::Buy, 100, 5, 2)); // newer
    auto t = book.add_order(limit(3, Side::Sell, 100, 5, 3));
    CHECK(t.size() == 1);
    CHECK(t[0].maker_id == 1); // the older order filled first
}

// A large order sweeps two levels, then rests the remainder.
static void test_partial_and_sweep() {
    OrderBook book;
    book.add_order(limit(1, Side::Sell, 100, 3, 1));
    book.add_order(limit(2, Side::Sell, 101, 3, 2));
    auto t = book.add_order(limit(3, Side::Buy, 101, 10, 3));
    CHECK(t.size() == 2);          // hit both ask levels
    CHECK(t[0].price == 100);
    CHECK(t[1].price == 101);
    Price bid;                     // 4 units left should rest as a bid at 101
    CHECK(book.best_bid(bid) && bid == 101);
}

// Market order takes liquidity regardless of price and never rests.
static void test_market_order() {
    OrderBook book;
    book.add_order(limit(1, Side::Sell, 105, 5, 1));
    Order mkt{2, Side::Buy, Type::Market, 0, 5, 2};
    auto t = book.add_order(mkt);
    CHECK(t.size() == 1);
    CHECK(t[0].price == 105);
    CHECK(book.resting_order_count() == 0);
}

// Cancel should remove a resting order so it can't match later.
static void test_cancel() {
    OrderBook book;
    book.add_order(limit(1, Side::Buy, 100, 5, 1));
    CHECK(book.cancel_order(1));
    CHECK(!book.cancel_order(1));           // second cancel finds nothing
    auto t = book.add_order(limit(2, Side::Sell, 100, 5, 2));
    CHECK(t.empty());                       // nothing left to match
}

int main() {
    test_no_cross_rests();
    test_simple_cross();
    test_price_priority();
    test_time_priority();
    test_partial_and_sweep();
    test_market_order();
    test_cancel();

    std::cout << (g_checks - g_failures) << "/" << g_checks
              << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
