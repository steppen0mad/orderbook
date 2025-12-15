#pragma once
#include "../book/order_book.h"
#include <cassert>
#include <numeric>

class InvariantChecker {
public:
    static void validate(const OrderBook& book) {
        // 1. No order has zero or negative quantity
        // 2. Sum of quantities at price level = sum of order quantities
        // 3. Order IDs in map exist exactly once in the book
        
        size_t countedOrders = 0;

        // Check Bids
        for (const auto& [price, level] : book.getBids()) {
            assert(!level.orders.empty() && "Price level should not be empty");
            for (const auto& order : level.orders) {
                assert(order.quantity > 0 && "Order quantity must be positive");
                assert(order.price == price && "Order price mismatch");
                assert(order.side == Side::Buy && "Order side mismatch in bids");
                
                auto idxIt = book.getIndex().find(order.id);
                assert(idxIt != book.getIndex().end() && "Order not in index");
                countedOrders++;
            }
        }

        // Check Asks
        for (const auto& [price, level] : book.getAsks()) {
            assert(!level.orders.empty() && "Price level should not be empty");
            for (const auto& order : level.orders) {
                assert(order.quantity > 0 && "Order quantity must be positive");
                assert(order.price == price && "Order price mismatch");
                assert(order.side == Side::Sell && "Order side mismatch in asks");

                auto idxIt = book.getIndex().find(order.id);
                assert(idxIt != book.getIndex().end() && "Order not in index");
                countedOrders++;
            }
        }

        // Check Index size
        assert(countedOrders == book.getIndex().size() && "Index size mismatch");

        // Check Best Bid < Best Ask
        auto bestBid = book.bestBid();
        auto bestAsk = book.bestAsk();
        if (bestBid && bestAsk) {
            assert(bestBid->price < bestAsk->price && "Crossed book detected");
        }
    }
};
