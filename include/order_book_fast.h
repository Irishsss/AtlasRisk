#pragma once

#include "order.h"

#include <vector>
#include <cstdint>
#include <limits>

namespace atlas {

// ------------------------------------------------------------------
// OrderBookFast: same matching rules as OrderBook (price-time priority),
// different data structures.
//
//   - Orders live in a preallocated pool. Resting, matching and
//     cancelling move indices and never allocate.
//   - Price levels are a flat array indexed by tick offset from min_tick.
//   - Each level is a doubly linked FIFO threaded through the pool, so
//     cancel unlinks in O(1).
//   - An occupancy bitmap (one bit per level) finds the next best price
//     with clz/ctz instead of scanning empty levels.
//
// Prices must fall inside [min_tick, min_tick + price_levels), fixed at
// construction.
// ------------------------------------------------------------------

constexpr uint32_t NIL = std::numeric_limits<uint32_t>::max();

class OrderBookFast {
public:
    // price_levels = number of distinct ticks the book can hold.
    // capacity     = max simultaneous resting orders (pool size).
    OrderBookFast(Price min_tick, uint32_t price_levels, uint32_t capacity);

    // Returns number of trades produced (trades themselves are reported via
    // the out-parameter to avoid allocating a vector on every call).
    uint32_t add_order(const Order& order, Trade* out, uint32_t out_cap);

    bool cancel_order(OrderId id);

    bool best_bid(Price& out) const;
    bool best_ask(Price& out) const;
    uint32_t resting_order_count() const { return resting_count_; }

private:
    // One pooled node = a resting order plus prev/next indices within its
    // price level.
    struct Node {
        Order order;
        uint32_t next;  // next node at this price level, or NIL
        uint32_t prev;  // previous node at this level, or NIL (doubly linked)
        bool live;      // false once filled or cancelled (slot reusable)
    };

    // One price level = head/tail indices into the pool + resting quantity.
    struct Level {
        uint32_t head = NIL;
        uint32_t tail = NIL;
        Quantity total = 0;
    };

    uint32_t tick_index(Price p) const {
        return static_cast<uint32_t>(p - min_tick_);
    }

    uint32_t alloc_node();      // grab a free pool slot
    void free_node(uint32_t i); // return a slot to the free list
    void push_level(Level& lvl, uint32_t node_idx);

    Price min_tick_;
    uint32_t levels_;

    std::vector<Node> pool_;
    std::vector<uint32_t> free_stack_;

    std::vector<Level> bid_levels_; // indexed by tick_index
    std::vector<Level> ask_levels_;

    // Occupancy bitmap: one bit per price level, set when the level has
    // resting orders. words_ = ceil(levels/64). find_highest / find_lowest
    // use __builtin_clzll / __builtin_ctzll, so the search is O(words)
    // instead of O(levels).
    std::vector<uint64_t> bid_bits_;
    std::vector<uint64_t> ask_bits_;
    uint32_t words_;

    void set_bit(std::vector<uint64_t>& bits, uint32_t idx) {
        bits[idx >> 6] |= (1ULL << (idx & 63));
    }
    void clear_bit(std::vector<uint64_t>& bits, uint32_t idx) {
        bits[idx >> 6] &= ~(1ULL << (idx & 63));
    }
    uint32_t find_highest(const std::vector<uint64_t>& bits) const;
    uint32_t find_lowest(const std::vector<uint64_t>& bits) const;

    // Index of the current best bid / best ask level, or NIL if that side
    // is empty.
    uint32_t best_bid_idx_;
    uint32_t best_ask_idx_;

    // id -> pool index, for O(1) cancel.
    std::vector<uint32_t> id_to_node_;

    uint32_t resting_count_ = 0;
};

} // namespace atlas
