#pragma once
//
// order_book.hpp -- a single-symbol limit order book with price-time priority.
//
// Data-structure choices (the part an interviewer will push on):
//
//   1. PRICE LADDER AS A FLAT ARRAY.  Prices are discrete ticks in a bounded
//      band [min,max] per symbol, so a level is addressed by direct index
//      (price - min): O(1), contiguous, cache-friendly. This replaces the
//      std::map<double,...> red-black tree of v1 (O(log n), pointer-chasing,
//      one cache miss per node). Trade-off: memory is O(band width), not
//      O(occupied levels). For a real instrument the band is a few thousand
//      ticks, which is cheap; for a pathologically wide band a hybrid
//      (array near the touch, map in the tails) would be better.
//
//   2. OCCUPANCY BITSET + INTEGER CURSORS for best bid/ask. One bit per level
//      marks "non-empty". best bid / best ask are integer cursors; when the
//      top level clears, we find the next occupied level with a hardware
//      find-set-bit (__builtin_clzll/ctzll) over 64 levels at a time. Best
//      bid/ask read is O(1); re-seating after a clear is effectively O(1)
//      because the next level is almost always within the same word.
//
//   3. INTRUSIVE DOUBLY-LINKED FIFO per level, nodes owned by an arena pool.
//      FIFO gives price-time priority; intrusive links make a cancel from the
//      middle O(1) with no search; pooled slots are stable handles, so the
//      use-after-free that v1 had (dangling deque iterators) cannot occur.
//
// Hot path performs ZERO heap allocation and ZERO I/O.
//
#include "types.hpp"
#include "order_pool.hpp"
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <cassert>

namespace ob {

class OrderBook {
public:
    // band: inclusive [min_price, max_price] in ticks. capacity: max live orders.
    OrderBook(Price min_price, Price max_price, std::size_t capacity)
        : min_price_(min_price),
          num_levels_(static_cast<std::size_t>(max_price - min_price + 1)),
          pool_(capacity),
          levels_(num_levels_),
          bits_((num_levels_ + 63) / 64, 0ull),
          best_bid_idx_(-1),
          best_ask_idx_(static_cast<long>(num_levels_)) {
        id_index_.reserve(capacity * 2);
    }

    // Submit an incoming order. Trades produced are appended to `trades`.
    // Returns the quantity that filled immediately.
    Quantity submit(OrderId id, Side side, OrderType type, Price price,
                    Quantity qty, std::vector<Trade>& trades) {
        assert(qty > 0);

        // Market orders cross the whole band; limit/IOC/FOK cross up to `price`.
        const Price limit = (type == OrderType::Market)
                                ? (side == Side::Buy ? maxPrice() : min_price_)
                                : price;

        // Limit/resting orders must fall inside the band.
        if (type != OrderType::Market && (price < min_price_ || price > maxPrice()))
            return 0;

        // Fill-Or-Kill: verify the whole size can fill before touching anything.
        if (type == OrderType::FOK && !canFill(side, limit, qty))
            return 0;

        const Quantity incoming = qty;
        Quantity remaining = (side == Side::Buy)
                                 ? matchBuy(limit, qty, id, trades)
                                 : matchSell(limit, qty, id, trades);

        // Only a genuine limit order rests its unfilled remainder.
        if (remaining > 0 && type == OrderType::Limit)
            rest(id, side, price, remaining);

        return incoming - remaining;
    }

    // O(1) cancel of a resting order. Returns false if the id is unknown.
    bool cancel(OrderId id) {
        auto it = id_index_.find(id);
        if (it == id_index_.end()) return false;
        removeSlot(it->second);
        id_index_.erase(it);
        return true;
    }

    // Modify with correct priority semantics:
    //   * pure quantity DECREASE at the same price keeps time priority (in place);
    //   * a price change or quantity INCREASE loses priority -> cancel + re-add
    //     (which may now cross and execute, exactly as a real exchange would).
    bool modify(OrderId id, Price new_price, Quantity new_qty,
                std::vector<Trade>& trades) {
        auto it = id_index_.find(id);
        if (it == id_index_.end()) return false;
        if (new_qty <= 0) return cancel(id);

        OrderNode& n = pool_[it->second];
        if (new_price == n.price && new_qty <= n.qty) {
            levels_[levelIndex(n.price)].total_qty -= (n.qty - new_qty);
            n.qty = new_qty;
            return true;
        }
        const Side side = n.side;
        cancel(id);
        submit(id, side, OrderType::Limit, new_price, new_qty, trades);
        return true;
    }

    // ---- queries -------------------------------------------------------
    Price bestBid() const noexcept {
        return best_bid_idx_ < 0 ? NO_PRICE : min_price_ + best_bid_idx_;
    }
    Price bestAsk() const noexcept {
        return best_ask_idx_ >= static_cast<long>(num_levels_) ? NO_PRICE
                                                               : min_price_ + best_ask_idx_;
    }
    Quantity qtyAtPrice(Price p) const noexcept {
        if (p < min_price_ || p > maxPrice()) return 0;
        return levels_[levelIndex(p)].total_qty;
    }
    bool contains(OrderId id) const { return id_index_.count(id) != 0; }
    std::size_t restingCount() const noexcept { return pool_.live(); }

    // ---- accessors for market-data reconstruction ---------------------
    // Read a resting order's current side/price/qty by id.
    bool getOrder(OrderId id, Side& side, Price& price, Quantity& qty) const {
        auto it = id_index_.find(id);
        if (it == id_index_.end()) return false;
        const OrderNode& n = pool_[it->second];
        side = n.side; price = n.price; qty = n.qty;
        return true;
    }
    // Insert a resting order WITHOUT matching. Order-by-order feeds (e.g. ITCH)
    // describe the book as it already is, so adds never cross and must not be
    // run through the matcher.
    void addResting(OrderId id, Side side, Price price, Quantity qty) {
        if (price < min_price_ || price > maxPrice()) return;
        rest(id, side, price, qty);
    }

    // exposed for the invariant checker / tests
    const OrderPool& pool() const noexcept { return pool_; }
    Price minPrice() const noexcept { return min_price_; }
    Price maxPrice() const noexcept { return min_price_ + static_cast<Price>(num_levels_) - 1; }
    std::size_t numLevels() const noexcept { return num_levels_; }
    Quantity levelQty(std::size_t idx) const noexcept { return levels_[idx].total_qty; }

private:
    struct Level {
        Slot     head      = NULL_SLOT;   // oldest order (front of FIFO)
        Slot     tail      = NULL_SLOT;   // newest order
        Quantity total_qty = 0;
        std::uint32_t count = 0;
    };

    std::size_t levelIndex(Price p) const noexcept {
        return static_cast<std::size_t>(p - min_price_);
    }

    // ---- bitset over occupied levels ----------------------------------
    void setBit(std::size_t i)   noexcept { bits_[i >> 6] |=  (1ull << (i & 63)); }
    void clearBit(std::size_t i) noexcept { bits_[i >> 6] &= ~(1ull << (i & 63)); }

    // Highest occupied level index <= i, or -1 if none.
    long highestSetLE(long i) const noexcept {
        if (i < 0) return -1;
        long w = i >> 6;
        std::uint64_t word = bits_[w] & maskLowInclusive(i & 63);
        if (word) return (w << 6) + 63 - __builtin_clzll(word);
        for (--w; w >= 0; --w)
            if (bits_[w]) return (w << 6) + 63 - __builtin_clzll(bits_[w]);
        return -1;
    }
    // Lowest occupied level index >= i, or num_levels_ if none.
    long lowestSetGE(long i) const noexcept {
        const long n = static_cast<long>(num_levels_);
        if (i >= n) return n;
        std::size_t w = static_cast<std::size_t>(i) >> 6;
        std::uint64_t word = bits_[w] & maskHighInclusive(i & 63);
        if (word) return static_cast<long>((w << 6) + __builtin_ctzll(word));
        for (++w; w < bits_.size(); ++w)
            if (bits_[w]) return static_cast<long>((w << 6) + __builtin_ctzll(bits_[w]));
        return n;
    }
    static std::uint64_t maskLowInclusive(long b) noexcept {  // bits [0..b]
        return b >= 63 ? ~0ull : ((1ull << (b + 1)) - 1);
    }
    static std::uint64_t maskHighInclusive(long b) noexcept { // bits [b..63]
        return ~((1ull << b) - 1);
    }

    // ---- resting & removal --------------------------------------------
    void rest(OrderId id, Side side, Price price, Quantity qty) {
        Slot s = pool_.allocate();
        assert(s != NULL_SLOT && "order pool exhausted");
        OrderNode& n = pool_[s];
        n.id = id; n.price = price; n.qty = qty; n.side = side;
        n.prev = NULL_SLOT; n.next = NULL_SLOT;

        const std::size_t idx = levelIndex(price);
        Level& lvl = levels_[idx];
        if (lvl.tail == NULL_SLOT) {            // first order at this level
            lvl.head = lvl.tail = s;
            setBit(idx);
            if (side == Side::Buy) { if (static_cast<long>(idx) > best_bid_idx_) best_bid_idx_ = static_cast<long>(idx); }
            else                   { if (static_cast<long>(idx) < best_ask_idx_) best_ask_idx_ = static_cast<long>(idx); }
        } else {                                 // append to back (youngest)
            n.prev = lvl.tail;
            pool_[lvl.tail].next = s;
            lvl.tail = s;
        }
        lvl.total_qty += qty;
        ++lvl.count;
        id_index_.emplace(id, s);
    }

    // Unlink an arbitrary slot from its level and free it (used by cancel).
    void removeSlot(Slot s) {
        OrderNode& n = pool_[s];
        const std::size_t idx = levelIndex(n.price);
        Level& lvl = levels_[idx];

        if (n.prev != NULL_SLOT) pool_[n.prev].next = n.next; else lvl.head = n.next;
        if (n.next != NULL_SLOT) pool_[n.next].prev = n.prev; else lvl.tail = n.prev;

        lvl.total_qty -= n.qty;
        --lvl.count;
        pool_.deallocate(s);

        if (lvl.count == 0) onLevelEmptied(idx, n.side);
    }

    void onLevelEmptied(std::size_t idx, Side side) noexcept {
        clearBit(idx);
        if (side == Side::Buy) {
            if (static_cast<long>(idx) == best_bid_idx_)
                best_bid_idx_ = highestSetLE(best_bid_idx_ - 1);
        } else {
            if (static_cast<long>(idx) == best_ask_idx_)
                best_ask_idx_ = lowestSetGE(best_ask_idx_ + 1);
        }
    }

    // ---- matching ------------------------------------------------------
    // Consume resting asks from the top up while they cross `limit`.
    Quantity matchBuy(Price limit, Quantity qty, OrderId aggr, std::vector<Trade>& trades) {
        while (qty > 0 && best_ask_idx_ < static_cast<long>(num_levels_)) {
            const Price lvl_px = min_price_ + best_ask_idx_;
            if (lvl_px > limit) break;                       // best ask no longer crosses
            qty = drainLevel(static_cast<std::size_t>(best_ask_idx_), lvl_px, qty, aggr, Side::Sell, trades);
        }
        return qty;
    }
    // Consume resting bids from the top down while they cross `limit`.
    Quantity matchSell(Price limit, Quantity qty, OrderId aggr, std::vector<Trade>& trades) {
        while (qty > 0 && best_bid_idx_ >= 0) {
            const Price lvl_px = min_price_ + best_bid_idx_;
            if (lvl_px < limit) break;
            qty = drainLevel(static_cast<std::size_t>(best_bid_idx_), lvl_px, qty, aggr, Side::Buy, trades);
        }
        return qty;
    }

    // Match `qty` against the FIFO at one level; remove fully-filled orders.
    // resting_side is the side of the orders being consumed.
    Quantity drainLevel(std::size_t idx, Price lvl_px, Quantity qty,
                        OrderId aggr, Side resting_side, std::vector<Trade>& trades) {
        Level& lvl = levels_[idx];
        while (qty > 0 && lvl.head != NULL_SLOT) {
            Slot s = lvl.head;
            OrderNode& r = pool_[s];
            const Quantity traded = qty < r.qty ? qty : r.qty;

            trades.push_back(Trade{aggr, r.id, lvl_px, traded});
            qty        -= traded;
            r.qty      -= traded;
            lvl.total_qty -= traded;

            if (r.qty == 0) {                    // resting order fully filled -> pop front
                lvl.head = r.next;
                if (lvl.head != NULL_SLOT) pool_[lvl.head].prev = NULL_SLOT;
                else                       lvl.tail = NULL_SLOT;
                --lvl.count;
                id_index_.erase(r.id);
                pool_.deallocate(s);
            }
        }
        if (lvl.count == 0) onLevelEmptied(idx, resting_side);
        return qty;
    }

    // Liquidity available to an incoming `side` order up to `limit` (for FOK).
    bool canFill(Side side, Price limit, Quantity need) const noexcept {
        Quantity avail = 0;
        if (side == Side::Buy) {
            for (long i = best_ask_idx_; i < static_cast<long>(num_levels_); i = lowestSetGE(i + 1)) {
                if (min_price_ + i > limit) break;
                avail += levels_[static_cast<std::size_t>(i)].total_qty;
                if (avail >= need) return true;
            }
        } else {
            for (long i = best_bid_idx_; i >= 0; i = highestSetLE(i - 1)) {
                if (min_price_ + i < limit) break;
                avail += levels_[static_cast<std::size_t>(i)].total_qty;
                if (avail >= need) return true;
            }
        }
        return avail >= need;
    }

    // ---- state ---------------------------------------------------------
    Price       min_price_;
    std::size_t num_levels_;
    OrderPool   pool_;
    std::vector<Level>         levels_;     // indexed by (price - min_price)
    std::vector<std::uint64_t> bits_;       // occupancy bitset over levels
    long        best_bid_idx_;              // -1 when no bids
    long        best_ask_idx_;              // num_levels_ when no asks
    std::unordered_map<OrderId, Slot> id_index_;   // id -> pool slot, for O(1) cancel/modify
};

} // namespace ob
