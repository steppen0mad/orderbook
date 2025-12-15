#pragma once
#include "../order/order.h"
#include <string>
#include <sstream>
#include <vector>
#include <iostream>

struct OrderInput {
    uint64_t timestamp;
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;
    Action action;
};

class OrderParser {
public:
    static OrderInput parseLine(const std::string& line) {
        std::stringstream ss(line);
        std::string segment;
        std::vector<std::string> parts;

        while (std::getline(ss, segment, ',')) {
            parts.push_back(segment);
        }

        // Expected: timestamp, order_id, side, price, quantity, action
        // Example: 1000,1,Buy,100.0,10,Add
        
        OrderInput input;
        if (parts.size() < 6) return input; // Or throw

        input.timestamp = std::stoull(parts[0]);
        input.id = std::stoull(parts[1]);
        
        std::string sideStr = parts[2];
        if (sideStr == "Buy") input.side = Side::Buy;
        else input.side = Side::Sell;

        input.price = std::stod(parts[3]);
        input.quantity = std::stoi(parts[4]);

        std::string actionStr = parts[5];
        // Trim whitespace if needed
        if (actionStr.find("Add") != std::string::npos) input.action = Action::Add;
        else if (actionStr.find("Cancel") != std::string::npos) input.action = Action::Cancel;
        else if (actionStr.find("Modify") != std::string::npos) input.action = Action::Modify;
        
        return input;
    }
};
