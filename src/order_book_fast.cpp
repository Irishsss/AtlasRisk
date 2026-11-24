#include "order_book_fast.h"

#include <algorithm>

namespace atlas {

OrderBookFast::OrderBookFast(Price min_tick, uint32_t price_levels,
                             uint32_t capacity)
    : min_tick_(min_tick),
      levels_(price_levels),
      bid_levels_(price_levels),
      ask_levels_(price_levels),
      best_bid_idx_(NIL),
      best_ask_idx_(NIL) {
    words_ = (price_levels + 63) / 64;
    bid_bits_.assign(words_, 0);
    ask_bits_.assign(words_, 0);

    pool_.resize(capacity);
    free_stack_.reserve(capacity);
    // Build the free list back-to-front so slot 0 is handed out first.
    for (uint32_t i = 0; i < capacity; ++i)
        free_stack_.push_back(capacity - 1 - i);

    // Grows on demand in add_order if an id goes past the end.
    id_to_node_.assign(capacity * 2, NIL);
}

// Highest set bit across the bitmap = highest occupied price = best bid.
// Walk words from the top; within the first non-zero word, 63 - clz gives the
// highest set bit's position. Returns NIL if the book side is empty.
uint32_t OrderBookFast::find_highest(const std::vector<uint64_t>& bits) const {
    for (uint32_t w = words_; w-- > 0;) {
        if (bits[w]) {
            uint32_t bit = 63u - static_cast<uint32_t>(__builtin_clzll(bits[w]));
            return (w << 6) + bit;
        }
    }
    return NIL;
}

// Lowest set bit = lowest occupied price = best ask. ctz gives the lowest set
// bit within the first non-zero word.
uint32_t OrderBookFast::find_lowest(const std::vector<uint64_t>& bits) const {
    for (uint32_t w = 0; w < words_; ++w) {
        if (bits[w]) {
            uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(bits[w]));
            return (w << 6) + bit;
        }
    }
    return NIL;
}

uint32_t OrderBookFast::alloc_node() {
    // No exhaustion check: callers size capacity for the workload.
    uint32_t idx = free_stack_.back();
    free_stack_.pop_back();
    return idx;
}

void OrderBookFast::free_node(uint32_t i) {
    pool_[i].live = false;
    free_stack_.push_back(i);
}

void OrderBookFast::push_level(Level& lvl, uint32_t node_idx) {
    pool_[node_idx].next = NIL;
    if (lvl.head == NIL) {
        pool_[node_idx].prev = NIL;
        lvl.head = lvl.tail = node_idx;
    } else {
        pool_[node_idx].prev = lvl.tail;
        pool_[lvl.tail].next = node_idx; // append at tail = newest = last to fill
        lvl.tail = node_idx;
    }
    lvl.total += pool_[node_idx].order.quantity;
}

uint32_t OrderBookFast::add_order(const Order& order, Trade* out,
                                  uint32_t out_cap) {
    uint32_t ntrades = 0;
    Quantity remaining = order.quantity;

    if (order.side == Side::Buy) {
        // Match against asks from the lowest ask upward while the buy crosses.
        while (remaining > 0 && best_ask_idx_ != NIL) {
            Price ask_price = min_tick_ + static_cast<Price>(best_ask_idx_);
            if (order.type == Type::Limit && order.price < ask_price) break;

            Level& lvl = ask_levels_[best_ask_idx_];
            while (remaining > 0 && lvl.head != NIL) {
                Node& maker = pool_[lvl.head];
                Quantity fill = std::min(remaining, maker.order.quantity);
                if (ntrades < out_cap)
                    out[ntrades] = Trade{order.id, maker.order.id, ask_price, fill};
                ++ntrades;

                remaining -= fill;
                maker.order.quantity -= fill;
                lvl.total -= fill;

                if (maker.order.quantity == 0) {
                    uint32_t done = lvl.head;
                    lvl.head = maker.next;
                    if (lvl.head == NIL) lvl.tail = NIL;
                    else pool_[lvl.head].prev = NIL;
                    id_to_node_[maker.order.id] = NIL;
                    free_node(done);
                    --resting_count_;
                }
            }
            // Level emptied: clear its bit and move to the next occupied ask.
            if (lvl.head == NIL) {
                clear_bit(ask_bits_, best_ask_idx_);
                best_ask_idx_ = find_lowest(ask_bits_);
            }
        }
    } else {
        // Match against bids from the highest bid downward while the sell crosses.
        while (remaining > 0 && best_bid_idx_ != NIL) {
            Price bid_price = min_tick_ + static_cast<Price>(best_bid_idx_);
            if (order.type == Type::Limit && order.price > bid_price) break;

            Level& lvl = bid_levels_[best_bid_idx_];
            while (remaining > 0 && lvl.head != NIL) {
                Node& maker = pool_[lvl.head];
                Quantity fill = std::min(remaining, maker.order.quantity);
                if (ntrades < out_cap)
                    out[ntrades] = Trade{order.id, maker.order.id, bid_price, fill};
                ++ntrades;

                remaining -= fill;
                maker.order.quantity -= fill;
                lvl.total -= fill;

                if (maker.order.quantity == 0) {
                    uint32_t done = lvl.head;
                    lvl.head = maker.next;
                    if (lvl.head == NIL) lvl.tail = NIL;
                    else pool_[lvl.head].prev = NIL;
                    id_to_node_[maker.order.id] = NIL;
                    free_node(done);
                    --resting_count_;
                }
            }
            // Level emptied: clear its bit and move to the next occupied bid.
            if (lvl.head == NIL) {
                clear_bit(bid_bits_, best_bid_idx_);
                best_bid_idx_ = find_highest(bid_bits_);
            }
        }
    }

    // Rest any unfilled remainder of a limit order as new liquidity.
    if (remaining > 0 && order.type == Type::Limit) {
        uint32_t idx = tick_index(order.price);
        uint32_t node_idx = alloc_node();
        pool_[node_idx].order = order;
        pool_[node_idx].order.quantity = remaining;
        pool_[node_idx].live = true;

        if (order.id >= id_to_node_.size())
            id_to_node_.resize(order.id + 1, NIL);
        id_to_node_[order.id] = node_idx;

        if (order.side == Side::Buy) {
            push_level(bid_levels_[idx], node_idx);
            set_bit(bid_bits_, idx);
            if (best_bid_idx_ == NIL || idx > best_bid_idx_) best_bid_idx_ = idx;
        } else {
            push_level(ask_levels_[idx], node_idx);
            set_bit(ask_bits_, idx);
            if (best_ask_idx_ == NIL || idx < best_ask_idx_) best_ask_idx_ = idx;
        }
        ++resting_count_;
    }

    return ntrades;
}

bool OrderBookFast::cancel_order(OrderId id) {
    if (id >= id_to_node_.size()) return false;
    uint32_t node_idx = id_to_node_[id];
    if (node_idx == NIL) return false;

    Node& n = pool_[node_idx];
    uint32_t idx = tick_index(n.order.price);
    Level& lvl = (n.order.side == Side::Buy) ? bid_levels_[idx] : ask_levels_[idx];

    // O(1) unlink from the level's doubly linked list.
    uint32_t p = n.prev, nx = n.next;
    if (p != NIL) pool_[p].next = nx; else lvl.head = nx;
    if (nx != NIL) pool_[nx].prev = p; else lvl.tail = p;

    lvl.total -= n.order.quantity;
    Side side = n.order.side; // read before freeing the node
    id_to_node_[id] = NIL;
    free_node(node_idx);
    --resting_count_;

    // If the level emptied, clear its bit. The best-price cursor only moves
    // when the emptied level was the best one.
    if (lvl.head == NIL) {
        if (side == Side::Buy) {
            clear_bit(bid_bits_, idx);
            if (idx == best_bid_idx_) best_bid_idx_ = find_highest(bid_bits_);
        } else {
            clear_bit(ask_bits_, idx);
            if (idx == best_ask_idx_) best_ask_idx_ = find_lowest(ask_bits_);
        }
    }
    return true;
}

bool OrderBookFast::best_bid(Price& out) const {
    if (best_bid_idx_ == NIL) return false;
    out = min_tick_ + static_cast<Price>(best_bid_idx_);
    return true;
}

bool OrderBookFast::best_ask(Price& out) const {
    if (best_ask_idx_ == NIL) return false;
    out = min_tick_ + static_cast<Price>(best_ask_idx_);
    return true;
}

} // namespace atlas
