#include "OrderBook.hpp"
#include <iostream>

bool OrderBook::addOrder(const Order& o) {
    std::map<double, PriceLevel, std::greater<double>>* bidBook = &bids;
    std::map<double, PriceLevel>* askBook = &asks;

    if (o.side == Side::Buy) {
        auto it = bidBook->try_emplace(o.price, PriceLevel{o.price}).first;
        it->second.addOrder(o);
        index[o.id] = OrderHandle{o.price, std::prev(it->second.orders.end())};
    } else {
        auto it = askBook->try_emplace(o.price, PriceLevel{o.price}).first;
        it->second.addOrder(o);
        index[o.id] = OrderHandle{o.price, std::prev(it->second.orders.end())};
    }

    match();

    return true;
    }

bool OrderBook::cancelOrder(uint64_t id) {
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


bool OrderBook::modifyOrder(uint64_t id, int newQty) {
    auto it = index.find(id);
    if (it == index.end()) return false;

    it->second.it->quantity = newQty;
    return true;
}

void OrderBook::match() {
    while (!bids.empty() && !asks.empty()) {
        auto& bidLevel = bids.begin()->second;
        auto& askLevel = asks.begin()->second;

        if (bidLevel.price < askLevel.price) break;

        Order& buy = bidLevel.front();
        Order& sell = askLevel.front();

        int traded = std::min(buy.quantity, sell.quantity);

        buy.quantity -= traded;
        sell.quantity -= traded;

        std::cout << "TRADE: " << traded << " @ "
                  << sell.price << "\n";

        if (buy.quantity == 0) {
            index.erase(buy.id);
            bidLevel.pop();
            if (bidLevel.empty()) bids.erase(bids.begin());
        }

        if (sell.quantity == 0) {
            index.erase(sell.id);
            askLevel.pop();
            if (askLevel.empty()) asks.erase(asks.begin());
        }
    }
}

std::optional<PriceLevel> OrderBook::bestBid() const {
    if (bids.empty()) return std::nullopt;
    return bids.begin()->second;
}

std::optional<PriceLevel> OrderBook::bestAsk() const {
    if (asks.empty()) return std::nullopt;
    return asks.begin()->second;
}

