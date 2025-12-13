#include "OrderBook.hpp"
#include <iostream>

int main() {
    OrderBook book;

    // Add some buy orders
    book.addOrder(Order{1, Side::Buy, 100.0, 10, 1});
    book.addOrder(Order{2, Side::Buy, 101.0, 5, 2});

    // Add some sell orders
    book.addOrder(Order{3, Side::Sell, 102.0, 7, 3});
    book.addOrder(Order{4, Side::Sell, 101.0, 6, 4});

    // At this point, order 2 (buy @101) should match with order 4 (sell @101)
    // Matching happens automatically inside addOrder()

    // Modify remaining quantity
    book.modifyOrder(1, 8);  // change buy order 1 qty to 8

    // Cancel an order
    book.cancelOrder(3);     // cancel sell @102

    // Query best bid / ask
    auto bestBid = book.bestBid();
    auto bestAsk = book.bestAsk();

    if (bestBid) {
        std::cout << "Best Bid: "
                  << bestBid->price
                  << " | Qty: "
                  << bestBid->orders.front().quantity
                  << "\n";
    } else {
        std::cout << "No bids\n";
    }

    if (bestAsk) {
        std::cout << "Best Ask: "
                  << bestAsk->price
                  << " | Qty: "
                  << bestAsk->orders.front().quantity
                  << "\n";
    } else {
        std::cout << "No asks\n";
    }

    return 0;
}

