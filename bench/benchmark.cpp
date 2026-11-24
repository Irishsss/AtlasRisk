#include "order_book.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using namespace atlas;
using Clock = std::chrono::steady_clock;

// Order flow: prices around a random-walk mid, balanced sides, ~15% cancels.
// Keeps the book at a steady size instead of growing or draining.
struct GeneratedOp {
    enum Kind { Add, Cancel } kind;
    Order order;
    OrderId cancel_id;
};

int main(int argc, char** argv) {
    const size_t N = (argc > 1) ? std::stoul(argv[1]) : 1'000'000;

    std::mt19937_64 rng(42); // fixed seed => reproducible numbers
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> price_noise(0.0, 15.0);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_real_distribution<double> action(0.0, 1.0);

    std::vector<GeneratedOp> ops;
    ops.reserve(N);
    std::vector<OrderId> live_ids; // ids currently resting, candidates to cancel

    Price mid = 10'000; // start at $100.00 in cents
    OrderId next_id = 1;
    uint64_t ts = 0;

    for (size_t i = 0; i < N; ++i) {
        mid += static_cast<Price>(price_noise(rng) * 0.05); // slow random walk
        if (mid < 1000) mid = 1000;

        // ~15% cancels when we have something to cancel, else always add.
        if (!live_ids.empty() && action(rng) < 0.15) {
            std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
            size_t idx = pick(rng);
            ops.push_back({GeneratedOp::Cancel, {}, live_ids[idx]});
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();
        } else {
            Side s = side_dist(rng) ? Side::Buy : Side::Sell;
            Price p = mid + static_cast<Price>(price_noise(rng));
            if (p < 1) p = 1;
            Order o{next_id, s, Type::Limit, p,
                    static_cast<Quantity>(qty_dist(rng)), ts++};
            ops.push_back({GeneratedOp::Add, o, 0});
            live_ids.push_back(next_id);
            ++next_id;
        }
    }

    // Per-operation latency samples, in nanoseconds. Reserved up front so the
    // measurement loop never allocates.
    std::vector<uint64_t> latencies;
    latencies.reserve(N);

    OrderBook book;
    auto wall_start = Clock::now();

    for (const auto& op : ops) {
        auto t0 = Clock::now();
        if (op.kind == GeneratedOp::Add) {
            auto trades = book.add_order(op.order);
            asm volatile("" ::"r"(trades.size()) : "memory"); // keep result live
        } else {
            bool ok = book.cancel_order(op.cancel_id);
            asm volatile("" ::"r"(ok) : "memory");
        }
        auto t1 = Clock::now();
        latencies.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    auto wall_end = Clock::now();
    double seconds =
        std::chrono::duration<double>(wall_end - wall_start).count();

    std::sort(latencies.begin(), latencies.end());
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p * (latencies.size() - 1));
        return latencies[idx];
    };

    double throughput = N / seconds;

    std::cout << "AtlasRisk matching engine benchmark\n";
    std::cout << "-----------------------------------\n";
    std::cout << "operations         : " << N << "\n";
    std::cout << "wall time          : " << seconds << " s\n";
    std::cout << "throughput         : " << static_cast<uint64_t>(throughput)
              << " ops/sec\n";
    std::cout << "resting orders left: " << book.resting_order_count() << "\n";
    std::cout << "latency p50        : " << pct(0.50) << " ns\n";
    std::cout << "latency p95        : " << pct(0.95) << " ns\n";
    std::cout << "latency p99        : " << pct(0.99) << " ns\n";
    std::cout << "latency max        : " << latencies.back() << " ns\n";

    // Raw latencies for analytics/analyze.py.
    std::ofstream out("latencies.csv");
    out << "latency_ns\n";
    for (uint64_t l : latencies) out << l << "\n";
    out.close();

    return 0;
}
