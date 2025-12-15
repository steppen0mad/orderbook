#include "order_book.h"

bool OrderBook::addOrder(const Order& o) {
    // Just insert into the book. Matching is handled by the Engine.
    // Wait, the prompt implies 'addOrder' does the matching in the original code.
    // But with 'MatchingEngine', usually the engine coordinates.
    // However, to keep 'OrderBook' self-contained as a data structure:
    
    if (o.side == Side::Buy) {
        auto it = bids.try_emplace(o.price, PriceLevel{o.price}).first;
        it->second.addOrder(o);
        index[o.id] = OrderHandle{o.price, std::prev(it->second.orders.end())};
    } else {
        auto it = asks.try_emplace(o.price, PriceLevel{o.price}).first;
        it->second.addOrder(o);
        index[o.id] = OrderHandle{o.price, std::prev(it->second.orders.end())};
    }
    return true;
}

bool OrderBook::cancelOrder(OrderId id) {
    auto it = index.find(id);
    if (it == index.end()) return false;

    auto& handle = it->second;

    if (handle.it->side == Side::Buy) {
        auto priceIt = bids.find(handle.price);
        if (priceIt == bids.end()) return false;

        priceIt->second.orders.erase(handle.it);
        if (priceIt->second.empty()) bids.erase(priceIt);
    } else {
        auto priceIt = asks.find(handle.price);
        if (priceIt == asks.end()) return false;

        priceIt->second.orders.erase(handle.it);
        if (priceIt->second.empty()) asks.erase(priceIt);
    }

    index.erase(id);
    return true;
}

bool OrderBook::modifyOrder(OrderId id, Quantity newQty) {
    auto it = index.find(id);
    if (it == index.end()) return false;

    it->second.it->quantity = newQty;
    return true;
}

std::optional<PriceLevel> OrderBook::bestBid() const {
    if (bids.empty()) return std::nullopt;
    return bids.begin()->second;
}

std::optional<PriceLevel> OrderBook::bestAsk() const {
    if (asks.empty()) return std::nullopt;
    return asks.begin()->second;
}
