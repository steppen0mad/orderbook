# Order Book

My order book implementation

## Features

- **Determinism**: Replayable from CSV input for backtesting and debugging.
- **Validation**: Strict invariant checking (e.g., crossed book, negative quantities) after every state change (optional for benchmarks).
- **Partial Fills**: Correctly handles multiple matches per aggressor, maintaining FIFO priority for resting orders.
- **Architecture**: Clean separation of concerns:
  - **Book**: Pure data structure managing price levels and order storage.
  - **Engine**: Business logic for matching and order lifecycle.
  - **IO**: Parsing and input handling.
  - **Validation**: Debugging and correctness checks.

## Build

### Standard Build (Debug/Demo)
Includes full invariant checking and trade logging.
```bash
g++ -std=c++17 main.cpp book/order_book.cpp engine/matching_engine.cpp -o orderbook
```

### High-Performance Benchmark Build
Disables validation and logging for accurate latency measurement.
```bash
g++ -std=c++17 -O3 -DNDEBUG -DDISABLE_VALIDATION -DQUIET_BENCH -DBENCH_COUNT=100000 main.cpp book/order_book.cpp engine/matching_engine.cpp -o orderbook_fast
```

## Usage

### Demo Mode
Runs a hardcoded scenario to demonstrate matching logic.
```bash
./orderbook
```

### File Input Mode (Replay)
Process orders from a CSV file.
```bash
./orderbook orders.csv
```
**Input format:** `timestamp,order_id,side,price,quantity,action`
**Example:** `1000,1,Buy,100.0,10,Add`

### Benchmark Mode
Runs performance tests for Adds and Aggressive Matches.
```bash
./orderbook_fast --benchmark
```

## Benchmark Results

Current snapshot performance (100,000 operations, single-threaded, -O3):

| Operation | Count | Total Time (ns) | Avg Latency (ns/op) |
|-----------|-------|-----------------|---------------------|
| **Adds** (Passive) | 100,000 | ~14,276,350 ns | ~142 ns |
| **Aggressive Matches** | 100,000 | ~7,070,956 ns | ~70 ns |

*Note: "Adds" involves map insertion and index updates. "Aggressive Matches" involves order lookup, matching logic, and deque/map cleanup.*

## Architecture & Design Choices

- **`std::map` for Price Levels**: 
  - *Why:* Keeps price levels ordered (Best Bid / Best Ask) which is critical for matching.
  - *Trade-off:* O(log N) insertion/lookup vs O(1) for `unordered_map`.
  
- **`std::deque` for Orders**: 
  - *Why:* Efficient O(1) insertion at back and removal from front (FIFO), unlike `std::vector` which shifts elements.
  
- **`std::unordered_map` for Order Index**:
  - *Why:* O(1) lookup for cancellations and modifications by Order ID.

- **Separation of Engine vs Book**:
  - The `OrderBook` class is a dumb container. The `MatchingEngine` contains the logic. This allows easier testing and swapping of matching algorithms.

