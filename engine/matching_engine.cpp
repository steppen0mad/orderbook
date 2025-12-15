#include "matching_engine.h"
#include "../validation/invariants.h"
#include <iostream>
#include <algorithm>

MatchingEngine::MatchingEngine(OrderBook& book) : book(book) {}

void MatchingEngine::processOrder(const Order& order, Action action) {
    if (action == Action::Cancel) {
        book.cancelOrder(order.id);
#ifndef DISABLE_VALIDATION
        InvariantChecker::validate(book);
#endif
        return;
    }
    
    if (action == Action::Modify) {
        book.modifyOrder(order.id, order.quantity);
#ifndef DISABLE_VALIDATION
        InvariantChecker::validate(book);
#endif
        return;
    }

    if (action == Action::Add) {
        Quantity remainingQty = order.quantity;
        
        if (order.side == Side::Buy) {
            matchBuy(order, remainingQty);
        } else {
            matchSell(order, remainingQty);
        }

        if (remainingQty > 0) {
            Order restingOrder = order;
            restingOrder.quantity = remainingQty;
            book.addOrder(restingOrder);
        }
#ifndef DISABLE_VALIDATION
        InvariantChecker::validate(book);
#endif
    }
}

void MatchingEngine::matchBuy(const Order& aggressor, Quantity& remainingQty) {
    while (remainingQty > 0 && !book.asks.empty()) {
        auto bestAskIt = book.asks.begin();
        PriceLevel& level = bestAskIt->second;

        if (level.price > aggressor.price) break;

        matchLevel(aggressor, remainingQty, level);
        
        if (level.empty()) {
            book.asks.erase(bestAskIt);
        }
    }
}

void MatchingEngine::matchSell(const Order& aggressor, Quantity& remainingQty) {
    while (remainingQty > 0 && !book.bids.empty()) {
        auto bestBidIt = book.bids.begin();
        PriceLevel& level = bestBidIt->second;

        if (level.price < aggressor.price) break;

        matchLevel(aggressor, remainingQty, level);

        if (level.empty()) {
            book.bids.erase(bestBidIt);
        }
    }
}

void MatchingEngine::matchLevel(const Order& aggressor, Quantity& remainingQty, PriceLevel& level) {
    while (remainingQty > 0 && !level.orders.empty()) {
        Order& resting = level.orders.front();
        
        Quantity traded = std::min(remainingQty, resting.quantity);
        
            #ifndef QUIET_BENCH
            std::cout << "TRADE: " << aggressor.id << " (aggressor) matches " 
                      << resting.id << " (resting) " 
                      << traded << " @ " << resting.price << "\n";
            #endif

        remainingQty -= traded;
        resting.quantity -= traded;

        if (resting.quantity == 0) {
            book.index.erase(resting.id);
            level.pop();
        }
    }
}


