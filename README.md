# AtlasRisk

A limit order book matching engine in C++, plus a Python layer that computes
market metrics (spread, VWAP, price impact, order-flow imbalance, realized
volatility) from the engine's trade stream.

## What it does

- **Limit and market orders** on both sides of the book.
- **Price-time priority**: best price first, ties broken by arrival order (FIFO).
- **Partial fills and multi-level sweeps**: a large order walks price levels
  until its limit stops crossing, then rests the remainder.
- **O(1) cancels** in the optimized engine.

Prices are integer ticks, not floating point, so orders at the same price
always compare equal.

## Two engines

**Baseline (`OrderBook`)** uses `std::map<Price, std::list<Order>>` plus an
`unordered_map` for cancels. Simple and easy to check, and it is the reference
the optimized engine is tested against.

**Optimized (`OrderBookFast`)** changes three things:

| Cost in baseline                       | Change                                          |
|----------------------------------------|-------------------------------------------------|
| One heap allocation per resting order  | Preallocated node pool; orders are linked by index |
| Tree walk to find the best price       | Flat array of price levels plus an occupancy bitmap (`clz`/`ctz` find the next occupied level) |
| Cancel needs the order's neighbours    | Doubly linked list per level; unlink is O(1)    |

`tests/test_differential.cpp` runs 200k random orders and cancels through both
engines and fails on the first difference in trades, cancel results, or
resting order count.

## Benchmark

Mixed workload of limit orders and ~15% cancels, prices from a random walk so
the book stays a steady size. The same pre-generated flow is replayed through
both engines. Fixed RNG seed, -O2, single core.

| Metric (1M ops) | Baseline      | Optimized     |
|-----------------|---------------|---------------|
| Throughput      | ~4.8M ops/sec | ~7.7M ops/sec |
| Speedup         |               | ~1.5-2x       |
| Latency p50     |               | ~50 ns        |
| Latency p99     |               | ~270 ns       |

Throughput and latency are timed in separate passes. My first version timed
every operation with two clock reads, which cost more than the operation
itself and made the optimized engine look slower than the baseline.

Numbers depend on hardware, so run it on your own machine.

## Market analytics

`bench/market_replay` runs a synthetic order flow through the engine and writes
every trade and top-of-book change to `market_events.csv`.
`analytics/market_analytics.py` reads it and reports:

- **Liquidity**: bid-ask spread in ticks and basis points.
- **Execution**: VWAP and price impact in basis points.
- **Order flow**: buy vs sell volume and net imbalance.
- **Risk**: realized volatility from mid-price log returns.

The order flow is synthetic. The calculations are standard definitions.

## Build and run

```bash
# unit tests
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp tests/test_order_book.cpp -o atlas_tests && ./atlas_tests

# optimized vs baseline, 200k random ops
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp src/order_book_fast.cpp tests/test_differential.cpp -o atlas_diff && ./atlas_diff

# benchmark
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp src/order_book_fast.cpp bench/benchmark_compare.cpp -o atlas_compare && ./atlas_compare 1000000

# market analytics
g++ -std=c++17 -O2 -Iinclude src/order_book.cpp src/order_book_fast.cpp bench/market_replay.cpp -o atlas_replay && ./atlas_replay 200000
python3 analytics/market_analytics.py market_events.csv
```

Or with CMake: `cmake -B build && cmake --build build && ctest --test-dir build`.

## Demo

`demo/index.html` is a small page that reimplements the matching rules in
JavaScript: submit orders, watch them rest or cross, see the trade tape. Open
it in a browser, no build step.

## Tests

- `test_order_book.cpp`: 24 checks covering resting, crossing, price priority,
  time priority, partial fills, sweeps, market orders, and cancels.
- `test_differential.cpp`: optimized engine vs baseline over 200k random ops.

## Layout

```
include/    order types, baseline book, optimized book
src/        both engine implementations
tests/      unit tests and differential test
bench/      benchmarks and market replay
analytics/  latency summary and market metrics
demo/       standalone order book page (HTML)
```
