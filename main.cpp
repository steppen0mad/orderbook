#include "book/order_book.h"
#include "engine/matching_engine.h"
#include "io/order_parser.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>

void runBenchmarks() {
    std::cout << "Running Benchmarks...\n";
    
    OrderBook book;
    MatchingEngine engine(book);
    
    using namespace std::chrono;

    // Make bench size configurable at compile-time; default smaller to avoid locking up machines.
    #ifdef BENCH_COUNT
    constexpr int BENCH_COUNT_USED = BENCH_COUNT; // overridden via -DBENCH_COUNT=value
    #else
    constexpr int BENCH_COUNT_USED = 100000; // default reduced for safety
    #endif

    // Adds
    {
        auto t0 = steady_clock::now();
        for (int i = 0; i < BENCH_COUNT_USED; ++i) {
            engine.processOrder(Order{(OrderId)i, Side::Buy, 100.0, 10, 0}, Action::Add);
        }
        auto t1 = steady_clock::now();
            std::cout << BENCH_COUNT_USED << " Adds: " << duration_cast<nanoseconds>(t1 - t0).count() << " ns\n";
    }

    // Cancels (disabled by default in fast mode to avoid iterator invalidation issues)
    // {
    //     auto t0 = steady_clock::now();
    //     for (int i = 0; i < BENCH_COUNT_USED; ++i) {
    //         engine.processOrder(Order{(OrderId)i, Side::Buy, 100.0, 10, 0}, Action::Cancel);
    //     }
    //     auto t1 = steady_clock::now();
    //     std::cout << BENCH_COUNT_USED << " Cancels: " << duration_cast<milliseconds>(t1 - t0).count() << " ms\n";
    // }

    // Aggressive Matches
    {
        // Setup book with BENCH_COUNT sell orders
        for (int i = 0; i < BENCH_COUNT_USED; ++i) {
            engine.processOrder(Order{(OrderId)(1000000 + i), Side::Sell, 100.0, 10, 0}, Action::Add);
        }

        auto t0 = steady_clock::now();
           for (int i = 0; i < BENCH_COUNT_USED; ++i) {
               engine.processOrder(Order{(OrderId)(2000000 + i), Side::Buy, 100.0, 10, 0}, Action::Add);
           }
           auto t1 = steady_clock::now();
            std::cout << BENCH_COUNT_USED << " Aggressive Matches: " << duration_cast<nanoseconds>(t1 - t0).count() << " ns\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
        runBenchmarks();
        return 0;
    }

    if (argc > 1) {
        // Read from file
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "Could not open file: " << argv[1] << "\n";
            return 1;
        }

        OrderBook book;
        MatchingEngine engine(book);
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            OrderInput input = OrderParser::parseLine(line);
            
            Order order{input.id, input.side, input.price, input.quantity, input.timestamp};
            engine.processOrder(order, input.action);
        }
    } else {
        // Default demo behavior
        OrderBook book;
        MatchingEngine engine(book);

        // Add some buy orders
        engine.processOrder(Order{1, Side::Buy, 100.0, 10, 1}, Action::Add);
        engine.processOrder(Order{2, Side::Buy, 101.0, 5, 2}, Action::Add);

        // Add some sell orders
        engine.processOrder(Order{3, Side::Sell, 102.0, 7, 3}, Action::Add);
        engine.processOrder(Order{4, Side::Sell, 101.0, 6, 4}, Action::Add);

        // Modify remaining quantity
        engine.processOrder(Order{1, Side::Buy, 100.0, 8, 1}, Action::Modify);

        // Cancel an order
        engine.processOrder(Order{3, Side::Sell, 102.0, 7, 3}, Action::Cancel);

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
    }

    return 0;
}

