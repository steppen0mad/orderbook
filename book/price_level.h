#pragma once
#include "../order/order.h"
#include <deque>

struct PriceLevel {
    Price price;
    std::deque<Order> orders; // FIFO

    explicit PriceLevel(Price p) : price(p) {}

    void addOrder(const Order& o) {
        orders.push_back(o);
    }

    bool empty() const { return orders.empty(); }

    Order& front() { return orders.front(); }
    const Order& front() const { return orders.front(); }
    
    void pop() { orders.pop_front(); }
};
