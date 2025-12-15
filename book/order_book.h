#pragma once
#include "price_level.h"
#include "../order/order.h"
#include <map>
#include <unordered_map>
#include <optional>
#include <functional>

struct OrderHandle {
    Price price;
    std::deque<Order>::iterator it;
};

class OrderBook {
public:
    // Core operations
    bool addOrder(const Order& o);
    bool cancelOrder(OrderId id);
    bool modifyOrder(OrderId id, Quantity newQty);

    // Query
    std::optional<PriceLevel> bestBid() const;
    std::optional<PriceLevel> bestAsk() const;
    
    // Accessors for Engine/Validation
    const std::map<Price, PriceLevel, std::greater<Price>>& getBids() const { return bids; }
    const std::map<Price, PriceLevel>& getAsks() const { return asks; }
    const std::unordered_map<OrderId, OrderHandle>& getIndex() const { return index; }

    // Friend engine to allow modification during matching
    friend class MatchingEngine;

private:
    std::map<Price, PriceLevel, std::greater<Price>> bids;
    std::map<Price, PriceLevel> asks;
    std::unordered_map<OrderId, OrderHandle> index;
};
