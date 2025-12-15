#pragma once
#include "order_id.h"
#include <cstdint>

enum class Side { Buy, Sell };
enum class Action { Add, Cancel, Modify };

struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
    uint64_t timestamp;
    
    // For input parsing purposes, we might carry the action here or use a separate struct.
    // But the book stores 'Order'.
};
