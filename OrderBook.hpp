#pragma once
#include <map>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <optional>

enum class Side { Buy, Sell };

struct Order {
    uint64_t id;
    Side side;
    double price;
    int quantity;
    uint64_t timestamp;
};

struct OrderHandle {
    double price;
    std::deque<Order>::iterator it;
};

struct PriceLevel {
    double price;
    std::deque<Order> orders; // FIFO

    void addOrder(const Order& o) {
        orders.push_back(o);
    }

    bool empty() const { return orders.empty(); }

    Order& front() { return orders.front(); }
    void pop() { orders.pop_front(); }
};

class OrderBook {
public:
    bool addOrder(const Order& o);
    bool cancelOrder(uint64_t id);
    bool modifyOrder(uint64_t id, int newQty);
    void match();

    std::optional<PriceLevel> bestBid() const;
    std::optional<PriceLevel> bestAsk() const;

private:
    std::map<double, PriceLevel, std::greater<double>> bids;
    std::map<double, PriceLevel> asks;

    std::unordered_map<uint64_t, OrderHandle> index;
};

