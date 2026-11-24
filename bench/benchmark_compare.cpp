#include "order_book.h"
#include "order_book_fast.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using namespace atlas;
using Clock = std::chrono::steady_clock;

// Generates one order flow up front and replays it through both engines, so
// both see identical input.

struct Op {
    bool is_cancel;
    Order order;
    OrderId cancel_id;
};

static std::vector<Op> generate(size_t N, Price min_tick, uint32_t levels) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> noise(0.0, 15.0);
    std::uniform_int_distribution<int> qty(1, 100);
    std::uniform_real_distribution<double> action(0.0, 1.0);

    std::vector<Op> ops;
    ops.reserve(N);
    std::vector<OrderId> live;
    Price mid = min_tick + static_cast<Price>(levels) / 2;
    OrderId next_id = 1;
    uint64_t ts = 0;

    for (size_t i = 0; i < N; ++i) {
        mid += static_cast<Price>(noise(rng) * 0.05);
        Price lo = min_tick, hi = min_tick + static_cast<Price>(levels) - 1;
        if (mid < lo + 50) mid = lo + 50;
        if (mid > hi - 50) mid = hi - 50;

        if (!live.empty() && action(rng) < 0.15) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            ops.push_back({true, {}, live[k]});
            live[k] = live.back();
            live.pop_back();
        } else {
            Side s = side_dist(rng) ? Side::Buy : Side::Sell;
            Price p = mid + static_cast<Price>(noise(rng));
            if (p < lo) p = lo;
            if (p > hi) p = hi;
            Order o{next_id, s, Type::Limit, p, (Quantity)qty(rng), ts++};
            ops.push_back({false, o, 0});
            live.push_back(next_id);
            ++next_id;
        }
    }
    return ops;
}

template <typename F>
static double time_run(F&& fn) {
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

int main(int argc, char** argv) {
    const size_t N = (argc > 1) ? std::stoul(argv[1]) : 1'000'000;
    // 2,000-tick price window. The generator starts the mid in the centre and
    // clamps it so every price stays inside.
    const Price MIN_TICK = 1;
    const uint32_t LEVELS = 2000;

    auto ops = generate(N, MIN_TICK, LEVELS);

    // ---- Baseline: std::map + std::list ----
    double base_s = time_run([&] {
        OrderBook book;
        for (const auto& op : ops) {
            if (op.is_cancel) {
                bool ok = book.cancel_order(op.cancel_id);
                asm volatile("" ::"r"(ok) : "memory");
            } else {
                auto tr = book.add_order(op.order);
                asm volatile("" ::"r"(tr.size()) : "memory");
            }
        }
    });

    // ---- Optimized: pooled nodes + array-indexed levels ----
    // No per-op clock reads in this pass: two Clock::now() calls cost more
    // than one operation. Latency gets its own pass below.
    Trade buf[64];

    double fast_s = time_run([&] {
        OrderBookFast book(MIN_TICK, LEVELS, (uint32_t)N + 16);
        for (const auto& op : ops) {
            if (op.is_cancel) {
                bool ok = book.cancel_order(op.cancel_id);
                asm volatile("" ::"r"(ok) : "memory");
            } else {
                uint32_t n = book.add_order(op.order, buf, 64);
                asm volatile("" ::"r"(n) : "memory");
            }
        }
    });

    // Latency pass. Clock overhead stays out of the throughput number.
    std::vector<uint64_t> latencies;
    latencies.reserve(N);
    {
        OrderBookFast book(MIN_TICK, LEVELS, (uint32_t)N + 16);
        for (const auto& op : ops) {
            auto t0 = Clock::now();
            if (op.is_cancel) {
                bool ok = book.cancel_order(op.cancel_id);
                asm volatile("" ::"r"(ok) : "memory");
            } else {
                uint32_t n = book.add_order(op.order, buf, 64);
                asm volatile("" ::"r"(n) : "memory");
            }
            auto t1 = Clock::now();
            latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
    }

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        return latencies[static_cast<size_t>(p * (latencies.size() - 1))];
    };

    double base_tput = N / base_s;
    double fast_tput = N / fast_s;

    std::cout << "AtlasRisk: baseline vs optimized (" << N << " ops)\n";
    std::cout << "=================================================\n";
    std::cout << "baseline (map+list)  : " << (uint64_t)base_tput
              << " ops/sec  (" << base_s << " s)\n";
    std::cout << "optimized (pool+array): " << (uint64_t)fast_tput
              << " ops/sec  (" << fast_s << " s)\n";
    std::cout << "speedup              : " << (fast_tput / base_tput) << "x\n";
    std::cout << "-------------------------------------------------\n";
    std::cout << "optimized latency p50: " << pct(0.50) << " ns\n";
    std::cout << "optimized latency p95: " << pct(0.95) << " ns\n";
    std::cout << "optimized latency p99: " << pct(0.99) << " ns\n";

    std::ofstream out("latencies.csv");
    out << "latency_ns\n";
    for (uint64_t l : latencies) out << l << "\n";
    return 0;
}
