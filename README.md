# Limit Order Book (C++17)

A single-symbol limit order book and matching engine built for low and
predictable latency, plus a NASDAQ TotalView-ITCH 5.0 decoder that reconstructs
the book from a real exchange feed.

The design choices below are the ones that matter for matching-engine
performance, and each is here for a reason I can defend, not because a library
offered it.

## Design

**Integer tick prices, never floating point.** Prices are `int64` ticks. A
`double` cannot represent most decimal prices exactly, so using one as a map key
makes equality unreliable and comparisons slower. The tick/price conversion
lives only at the I/O boundary; the engine never sees a float.

**Price levels as a flat array, not a tree.** Prices are discrete and bounded
per symbol, so a level is addressed by direct index `price - min_price`. That is
O(1) and cache-friendly: walking adjacent levels during a sweep is a linear scan
of contiguous memory. The textbook `std::map<price, level>` is a red-black tree:
O(log n) with a pointer chase and a likely cache miss per node. The trade-off is
memory proportional to the price band rather than to occupied levels, which is
cheap for a real instrument (a few thousand ticks) and is the standard
matching-engine layout. A pathologically wide band would want a hybrid (array
near the touch, map in the tails).

**Occupancy bitset + integer cursors for best bid/ask.** One bit per level marks
"non-empty". Best bid and best ask are integer cursors, so reading the top of
book is O(1). When the top level clears, the next occupied level is found with a
hardware find-set-bit (`__builtin_clzll`/`ctzll`) scanning 64 levels per word,
so re-seating is effectively O(1) because the next level is almost always in the
same word.

**Arena-pooled, intrusive FIFO queues.** Each level is an intrusive
doubly-linked list of order nodes owned by a pre-allocated pool. FIFO gives
price-time priority. Intrusive links make a cancel from the middle of the queue
O(1) with no search. Pool slots are stable indices, so handles never dangle (see
"Correctness" below). The hot path performs **zero heap allocation and zero
I/O**.

**Order types.** Limit, Market, IOC (immediate-or-cancel), and FOK
(fill-or-kill, pre-checked against available liquidity before any state change).

**Modify with correct priority semantics.** A pure quantity decrease at the same
price keeps time priority and is done in place. A price change or a quantity
increase loses priority and is implemented as cancel plus re-add, which can then
cross and execute, exactly as a real exchange behaves.

## Correctness

This was rebuilt from an earlier version that had a heap-use-after-free: it
stored `std::deque<Order>::iterator` handles in the order index, and a deque
invalidates all iterators when it grows, so a cancel after enough adds
dereferenced a dangling iterator. The arena design removes that class of bug
structurally (handles are indices, not iterators).

Correctness is checked three ways:

- **Unit tests** (`tests/test_matching.cpp`) for the semantics that are easy to
  get wrong: FIFO priority, partial fills, multi-level sweeps, modify keeping vs
  losing priority, a repriced order that crosses, and IOC/FOK/market behaviour.
- **Differential fuzzing** (`tests/test_differential.cpp`) runs 3,000,000 random
  operations through both this book and a deliberately naive `std::map`-based
  reference, asserting identical trades after every operation and identical
  resting state periodically. The two agree across more than a million trades.
- **Sanitizers**: both test suites run clean under AddressSanitizer and
  UndefinedBehaviorSanitizer (`cmake -DSANITIZE=ON`).

## Benchmark

`bench/bench_latency.cpp` is deliberately honest. The earlier version added
100,000 orders at a single price, so its "per-add" number measured one growing
queue and the price map never held more than one level. This benchmark seeds a
deep book across hundreds of levels around a moving mid, drives a realistic mix
of passive adds, marketable orders, and cancels, warms up to fault in memory and
reach steady state, then times every operation individually with `rdtsc` and
reports percentiles.

Representative run, 11M timed operations, single thread, `-O3 -march=native`, on
a shared ~2.8 GHz cloud VM:

| operation                | p50    | p99     | p99.9   |
|--------------------------|--------|---------|---------|
| submit, rests passively  | ~51 ns | ~190 ns | ~950 ns |
| submit, crosses & trades | ~75 ns | ~1.0 us | ~3.5 us |
| cancel                   | ~58 ns | ~110 ns | ~300 ns |

Throughput is roughly 7M ops/s. A crossing order is more expensive because it
can sweep several levels and erase each filled order from the id index; that hash
map is the dominant remaining cost and the obvious next optimization (a flat or
open-addressed index keyed on a dense id). Absolute `max` samples reach the
millisecond range: those are the process being descheduled by the shared VM, not
a stall in the book, which is why p50 through p99.9 are the figures that matter.
Numbers are hardware dependent; run it yourself.

## Market data: ITCH 5.0 reconstruction

`include/ob/itch.hpp` decodes NASDAQ TotalView-ITCH 5.0 (big-endian,
length-prefixed) and reconstructs the book from the order-by-order feed: Add,
Order Executed, Order Executed With Price, Order Cancel, Order Delete, and
Replace. Unknown message types are skipped via the length prefix, so it consumes
the full stream. ITCH prices are already integers (1/10000 dollar), so they map
straight onto tick prices.

Real NASDAQ files are multi-gigabyte and not redistributable, so `gen_itch`
writes a synthetic but spec-accurate stream for one symbol (bids below the mid,
asks above, so the reconstructed book never crosses). `replay_itch` reads it
back and reports decode plus reconstruct throughput. A representative run decodes
5,000,000 messages (167 MB) at roughly 3.5M msg/s (~120 MB/s) on the same VM,
ending with a sane, non-crossed book.

To run against a real feed: download a single-symbol NASDAQ ITCH 5.0 sample,
widen the price band in `replay_itch.cpp` to cover the symbol's range (or filter
by stock locate), and point the tool at the file. The parser is unchanged.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/test_matching          # unit tests
./build/test_differential      # 3M-op differential fuzz vs reference
./build/bench_latency 12000000 # latency percentiles

./build/gen_itch    feed.itch 5000000   # synthesise an ITCH 5.0 feed
./build/replay_itch feed.itch           # reconstruct the book from it
```

Tests under sanitizers:

```bash
cmake -B build_san -DSANITIZE=ON && cmake --build build_san -j
./build_san/test_matching
```

## Layout

```
include/ob/
  types.hpp          value types: integer-tick Price, Side, OrderType, Trade
  order_pool.hpp     fixed-capacity arena, O(1) alloc/free, stable handles
  order_book.hpp     flat price ladder + occupancy bitset + intrusive FIFO + matching
  reference_book.hpp naive std::map book used only to validate the fast one
  itch.hpp           NASDAQ TotalView-ITCH 5.0 decoder
tests/   unit tests, differential fuzz
bench/   latency benchmark
tools/   ITCH feed generator and replay/reconstruction
```

## What I would do next

- Replace the `std::unordered_map` id index with an open-addressed or flat index;
  it is the main cost left in the crossing path.
- Add an L2/L3 snapshot view and a top-of-book change feed for downstream
  consumers.
- Multi-symbol support with per-symbol bands.
- A persistent op log for deterministic replay and crash recovery.
