#include "order_book.h"

namespace atlas {

OrderBook::OrderBook() = default;

// The two sides match with mirror-image price logic, so the actual matching
// loop is a template and each side just passes in its own "does this cross?"
// test. This keeps the buy and sell paths from drifting out of sync.
template <typename BookSide, typename Crosses>
void OrderBook::match(Order& incoming, BookSide& opposite, Crosses crosses,
                      std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();          // best price on the far side
        Price level_price = level_it->first;

        // Market orders ignore price; limit orders stop once the book no
        // longer crosses their limit.
        if (incoming.type == Type::Limit && !crosses(incoming.price, level_price))
            break;

        PriceLevel& level = level_it->second;

        // Walk this price level in arrival order (front = oldest = first to fill).
        while (incoming.quantity > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();
            Quantity fill = std::min(incoming.quantity, resting.quantity);

            // Trades print at the resting (maker) order's price.
            trades.push_back(Trade{incoming.id, resting.id, level_price, fill});

            incoming.quantity -= fill;
            resting.quantity -= fill;
            level.total_quantity -= fill;

            if (resting.is_filled()) {
                locations_.erase(resting.id);
                level.orders.pop_front();
            }
        }

        if (level.orders.empty())
            opposite.erase(level_it);
    }
}

std::vector<Trade> OrderBook::add_order(Order order) {
    std::vector<Trade> trades;

    if (order.side == Side::Buy) {
        // A buy crosses when its limit price >= the best ask.
        match(order, asks_, [](Price bid, Price ask) { return bid >= ask; }, trades);
    } else {
        // A sell crosses when its limit price <= the best bid.
        match(order, bids_, [](Price ask, Price bid) { return ask <= bid; }, trades);
    }

    // An unfilled limit remainder rests in the book. Market orders never
    // rest; any unfilled remainder is dropped.
    if (order.quantity > 0 && order.type == Type::Limit)
        rest(order);

    return trades;
}

void OrderBook::rest(Order order) {
    if (order.side == Side::Buy) {
        PriceLevel& level = bids_[order.price];
        level.total_quantity += order.quantity;
        level.orders.push_back(order);
        locations_[order.id] = {Side::Buy, order.price, std::prev(level.orders.end())};
    } else {
        PriceLevel& level = asks_[order.price];
        level.total_quantity += order.quantity;
        level.orders.push_back(order);
        locations_[order.id] = {Side::Sell, order.price, std::prev(level.orders.end())};
    }
}

bool OrderBook::cancel_order(OrderId id) {
    auto loc = locations_.find(id);
    if (loc == locations_.end())
        return false; // already filled or never existed

    const Location& l = loc->second;
    if (l.side == Side::Buy) {
        auto level_it = bids_.find(l.price);
        level_it->second.total_quantity -= l.it->quantity;
        level_it->second.orders.erase(l.it);
        if (level_it->second.orders.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(l.price);
        level_it->second.total_quantity -= l.it->quantity;
        level_it->second.orders.erase(l.it);
        if (level_it->second.orders.empty()) asks_.erase(level_it);
    }

    locations_.erase(loc);
    return true;
}

bool OrderBook::best_bid(Price& out) const {
    if (bids_.empty()) return false;
    out = bids_.begin()->first;
    return true;
}

bool OrderBook::best_ask(Price& out) const {
    if (asks_.empty()) return false;
    out = asks_.begin()->first;
    return true;
}

} // namespace atlas
