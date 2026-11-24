#pragma once

#include "order.h"

#include <map>
#include <list>
#include <vector>
#include <unordered_map>
#include <functional>

namespace atlas {

// A single price level holds all resting orders at one price, in arrival order.
// std::list gives O(1) removal from the middle when an order is cancelled,
// and we keep an iterator to each order so cancel doesn't have to search.
struct PriceLevel {
    std::list<Order> orders;
    Quantity total_quantity = 0;
};

// Limit order book with strict price-time priority:
//   1. Best price matches first (highest bid / lowest ask).
//   2. Within a price, the order that arrived first matches first (FIFO).
//
// Bids are kept in a map sorted high->low, asks low->high, so the best price
// on each side is always map.begin(). std::map keeps levels sorted for us and
// gives O(log L) insert where L is the number of distinct price levels.
class OrderBook {
public:
    OrderBook();

    // Submit an order. Returns every trade the order generated. Any unfilled
    // remainder of a limit order is added to the book; a market order's
    // remainder is simply dropped (no price to rest at).
    std::vector<Trade> add_order(Order order);

    // Cancel a resting order by id. Returns true if it was found and removed.
    bool cancel_order(OrderId id);

    // Best bid / best ask, used by the analytics layer to reconstruct the spread.
    bool best_bid(Price& out) const;
    bool best_ask(Price& out) const;

    size_t resting_order_count() const { return locations_.size(); }

private:
    // Match an incoming order against the opposite side as far as price allows.
    template <typename BookSide, typename Crosses>
    void match(Order& incoming, BookSide& opposite, Crosses crosses,
               std::vector<Trade>& trades);

    void rest(Order order);

    // Bids: highest price first. Asks: lowest price first.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;

    // id -> (side, price, iterator into that level's list), so cancel
    // doesn't have to scan the book.
    struct Location {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };
    std::unordered_map<OrderId, Location> locations_;
};

} // namespace atlas
