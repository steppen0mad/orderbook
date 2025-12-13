# Limit Order Book Simulator

A C++ implementation of a **simple limit order book** for educational and prototyping purposes.  
Supports **buy/sell orders**, **price-time priority matching**, and basic order management (add, cancel, modify).

---

## Features

- **Add, cancel, and modify orders**  
- **Automatic matching** based on **price-time priority**  
- **Bid/ask price levels** with FIFO ordering per price level  
- **Trade reporting** in console output  
- **Query best bid and best ask** at any time  

---

## Technical Details

- Implemented in **C++20**  
- Uses:
  - `std::map` for bid/ask order books  
  - `std::deque` for FIFO queues at each price level  
  - `std::unordered_map` for fast order ID indexing  
- Matching logic handles **partial fills** and updates orders in-place  
- Deterministic and repeatable for consistent results  

---

## Usage

1. **Clone the repository**
```bash
git clone https://github.com/<your-username>/orderbook.git
cd orderbook
