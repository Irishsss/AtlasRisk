#include "order_book_fast.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>

using namespace atlas;

// Replays a synthetic order flow through OrderBookFast and writes every trade
// and top-of-book change to market_events.csv, which
// analytics/market_analytics.py reads.

int main(int argc, char** argv) {
    const size_t N = (argc > 1) ? std::stoul(argv[1]) : 200000;
    const Price MIN_TICK = 1;
    const uint32_t LEVELS = 2000;

    std::mt19937_64 rng(2025);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> noise(0.0, 8.0);
    std::uniform_int_distribution<int> qty(1, 50);
    std::uniform_real_distribution<double> action(0.0, 1.0);
    // Small upward drift in the mid.
    std::normal_distribution<double> drift(0.02, 1.0);

    OrderBookFast book(MIN_TICK, LEVELS, (uint32_t)N + 16);
    std::ofstream out("market_events.csv");
    out << "ts,event,side,price,quantity,best_bid,best_ask\n";

    Trade trades[64];
    Price mid = 1000;
    OrderId next_id = 1;
    uint64_t ts = 0;

    Price last_bid = 0, last_ask = 0;

    for (size_t i = 0; i < N; ++i) {
        mid += static_cast<Price>(drift(rng) * 0.05 + noise(rng) * 0.02);
        if (mid < 60) mid = 60;
        if (mid > 1940) mid = 1940;

        // Buys are priced 2 ticks above the mid and sells 2 below, so more
        // orders cross the spread.
        Side s = side_dist(rng) ? Side::Buy : Side::Sell;
        double skew = (s == Side::Buy) ? 2.0 : -2.0;
        Price p = mid + static_cast<Price>(noise(rng) + skew);
        if (p < MIN_TICK) p = MIN_TICK;
        if (p >= MIN_TICK + (Price)LEVELS) p = MIN_TICK + (Price)LEVELS - 1;

        Order o{next_id++, s, Type::Limit, p, (Quantity)qty(rng), ts++};

        // Mid before this order arrives; used as each trade's reference mid.
        Price pbb = 0, pba = 0;
        bool phb = book.best_bid(pbb), pha = book.best_ask(pba);
        Price pre_mid = (phb && pha) ? (pbb + pba) / 2 : mid;

        uint32_t nt = book.add_order(o, trades, 64);

        // Trade rows put the pre-trade mid in both bid/ask columns.
        // side = taker side.
        for (uint32_t k = 0; k < nt; ++k) {
            out << ts << ",trade," << (s == Side::Buy ? "Buy" : "Sell") << ","
                << trades[k].price << "," << trades[k].quantity << ","
                << pre_mid << "," << pre_mid << "\n";
        }

        // Record a quote line whenever the top of book moved.
        Price bb = 0, ba = 0;
        bool hb = book.best_bid(bb), ha = book.best_ask(ba);
        if ((hb && bb != last_bid) || (ha && ba != last_ask)) {
            out << ts << ",quote,,,," << (hb ? bb : 0) << "," << (ha ? ba : 0) << "\n";
            last_bid = hb ? bb : last_bid;
            last_ask = ha ? ba : last_ask;
        }
    }

    out.close();
    std::cout << "wrote market_events.csv (" << N << " input orders)\n";
    std::cout << "run: python3 analytics/market_analytics.py market_events.csv\n";
    return 0;
}
