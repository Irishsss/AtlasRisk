#pragma once

#include <cstdint>
#include <string>

namespace atlas {

// Buy = bid, Sell = ask.
enum class Side : uint8_t { Buy, Sell };

// Limit  = rest in the book until matched or cancelled.
// Market = take whatever liquidity is available right now, never rests.
enum class Type : uint8_t { Limit, Market };

// Prices are integer ticks (e.g. cents), not doubles, so two orders at the
// same price always compare equal.
using Price = int64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;

struct Order {
    OrderId id;
    Side side;
    Type type;
    Price price;        // ignored for market orders
    Quantity quantity;  // remaining quantity still to fill
    uint64_t timestamp; // sequence number, used to break price ties (FIFO)

    bool is_filled() const { return quantity == 0; }
};

// A Trade is produced every time an incoming order matches a resting order.
struct Trade {
    OrderId taker_id;   // the incoming order that crossed the spread
    OrderId maker_id;   // the resting order that provided liquidity
    Price price;        // execution price = the resting (maker) price
    Quantity quantity;  // how much was exchanged
};

} // namespace atlas
