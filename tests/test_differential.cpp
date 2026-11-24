#include "order_book.h"
#include "order_book_fast.h"

#include <iostream>
#include <random>
#include <vector>

using namespace atlas;

// Runs the same random order flow through OrderBook and OrderBookFast and
// fails on the first difference in trades, cancel results, or resting count.

static int g_fail = 0;

int main() {
    const size_t N = 200000;
    const Price MIN_TICK = 1;
    const uint32_t LEVELS = 40000; // price window [1, 40000]
    const uint32_t CAP = N + 16;

    std::mt19937_64 rng(7);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> noise(0.0, 30.0);
    std::uniform_int_distribution<int> qty(1, 50);
    std::uniform_real_distribution<double> action(0.0, 1.0);

    OrderBook baseline;
    OrderBookFast fast(MIN_TICK, LEVELS, CAP);

    std::vector<OrderId> live;
    Price mid = 20000;
    OrderId next_id = 1;
    uint64_t ts = 0;

    std::vector<Trade> buf_a;
    std::vector<Trade> buf_b(64);

    for (size_t i = 0; i < N; ++i) {
        mid += static_cast<Price>(noise(rng) * 0.05);
        if (mid < 100) mid = 100;
        if (mid > 39000) mid = 39000;

        if (!live.empty() && action(rng) < 0.15) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            OrderId id = live[k];
            live[k] = live.back();
            live.pop_back();

            bool a = baseline.cancel_order(id);
            bool b = fast.cancel_order(id);
            if (a != b) {
                std::cerr << "cancel mismatch on id " << id << " at op " << i << "\n";
                ++g_fail; break;
            }
        } else {
            Side s = side_dist(rng) ? Side::Buy : Side::Sell;
            Price p = mid + static_cast<Price>(noise(rng));
            if (p < MIN_TICK) p = MIN_TICK;
            if (p >= MIN_TICK + (Price)LEVELS) p = MIN_TICK + (Price)LEVELS - 1;
            Order o{next_id, s, Type::Limit, p, (Quantity)qty(rng), ts++};

            buf_a = baseline.add_order(o);
            uint32_t nb = fast.add_order(o, buf_b.data(), (uint32_t)buf_b.size());

            if (buf_a.size() != nb) {
                std::cerr << "trade count mismatch at op " << i
                          << " (baseline " << buf_a.size() << " vs fast " << nb << ")\n";
                ++g_fail; break;
            }
            for (size_t t = 0; t < buf_a.size(); ++t) {
                if (buf_a[t].price != buf_b[t].price ||
                    buf_a[t].quantity != buf_b[t].quantity ||
                    buf_a[t].maker_id != buf_b[t].maker_id) {
                    std::cerr << "trade detail mismatch at op " << i << "\n";
                    ++g_fail; break;
                }
            }
            if (g_fail) break;

            live.push_back(next_id);
            ++next_id;
        }

        if (baseline.resting_order_count() != fast.resting_order_count()) {
            std::cerr << "resting count mismatch at op " << i << " ("
                      << baseline.resting_order_count() << " vs "
                      << fast.resting_order_count() << ")\n";
            ++g_fail; break;
        }
    }

    if (g_fail == 0)
        std::cout << "differential test passed: fast book matches baseline over "
                  << N << " randomized operations\n";
    return g_fail == 0 ? 0 : 1;
}
