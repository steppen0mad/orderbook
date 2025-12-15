#pragma once
#include "../book/order_book.h"
#include "../order/order.h"

class MatchingEngine {
public:
    MatchingEngine(OrderBook& book);
    void processOrder(const Order& order, Action action);

private:
    OrderBook& book;

    void matchBuy(const Order& aggressor, Quantity& remainingQty);
    void matchSell(const Order& aggressor, Quantity& remainingQty);
    
    void matchLevel(const Order& aggressor, Quantity& remainingQty, PriceLevel& level);
};

