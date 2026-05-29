//
// bench_latency.cpp -- honest latency benchmark.
//
// What v1 did wrong: it added 100k orders all at the SAME price, so the price
// map never held more than one level and the "per-add" number measured one
// growing queue, not the book. It also reported only an average, which hides
// the tail that actually matters in trading.
//
// This benchmark instead:
//   * seeds a deep book spread across many levels around a moving mid,
//   * drives a realistic mix of passive adds, marketable (crossing) orders and
//     cancels so every code path (new level, existing level, multi-level
//     sweep, level-empty re-seat, O(1) cancel) is exercised,
//   * times EACH operation individually with rdtsc,
//   * classifies each sample by what actually happened, and
//   * reports p50 / p90 / p99 / p99.9 / max per class, plus throughput.
//
#include "ob/order_book.hpp"
#include <x86intrin.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace ob;

static inline std::uint64_t tsc() {
    _mm_lfence();
    std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

// Calibrate TSC ticks per nanosecond against steady_clock.
static double calibrate_tsc_per_ns() {
    using namespace std::chrono;
    auto c0 = tsc();
    auto w0 = steady_clock::now();
    // busy spin ~100ms
    while (duration_cast<milliseconds>(steady_clock::now() - w0).count() < 100) { }
    auto c1 = tsc();
    auto w1 = steady_clock::now();
    double ns = (double)duration_cast<nanoseconds>(w1 - w0).count();
    return (double)(c1 - c0) / ns;
}

struct Stats {
    std::vector<std::uint32_t> cyc;     // per-op cycle counts
    void reserve(std::size_t n) { cyc.reserve(n); }
    void add(std::uint64_t c) { cyc.push_back((std::uint32_t)c); }
    void report(const char* name, double tsc_per_ns) {
        if (cyc.empty()) { std::printf("  %-22s (no samples)\n", name); return; }
        std::sort(cyc.begin(), cyc.end());
        auto ns = [&](double frac) {
            std::size_t i = (std::size_t)(frac * (cyc.size() - 1));
            return cyc[i] / tsc_per_ns;
        };
        double sum = 0; for (auto v : cyc) sum += v;
        std::printf("  %-22s n=%-9zu mean=%6.1f  p50=%6.1f  p90=%6.1f  p99=%6.1f  p99.9=%7.1f  max=%8.1f  (ns)\n",
                    name, cyc.size(), (sum / cyc.size()) / tsc_per_ns,
                    ns(0.50), ns(0.90), ns(0.99), ns(0.999), cyc.back() / tsc_per_ns);
    }
};

int main(int argc, char** argv) {
    const int OPS = (argc > 1) ? std::atoi(argv[1]) : 10'000'000;

    const Price MINP = 1, MAXP = 200'000;
    const std::size_t CAP = 3'000'000;
    OrderBook book(MINP, MAXP, CAP);

    std::printf("Calibrating TSC...\n");
    const double tsc_per_ns = calibrate_tsc_per_ns();
    std::printf("TSC: %.3f GHz\n\n", tsc_per_ns);

    std::mt19937_64 rng(12345);
    auto U = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // Pre-generate randomness so RNG cost is OUTSIDE the timed region.
    struct Gen { std::uint8_t kind; std::uint8_t side; std::int32_t off; std::int32_t qty; };
    std::vector<Gen> script(OPS);
    for (int i = 0; i < OPS; ++i) {
        int r = U(0, 99);
        std::uint8_t kind = r < 45 ? 0 /*passive*/ : r < 75 ? 1 /*marketable*/ : 2 /*cancel*/;
        script[i] = Gen{ kind, (std::uint8_t)U(0,1), (std::int32_t)U(1,40), (std::int32_t)U(1,100) };
    }

    // Seed a deep book: 200 levels each side around the mid.
    Price mid = 100'000;
    OrderId next_id = 1;
    std::vector<OrderId> live;  live.reserve(CAP);
    std::vector<Trade> trades;  trades.reserve(256);
    for (int k = 1; k <= 200; ++k) {
        book.submit(next_id, Side::Buy,  OrderType::Limit, mid - k, 50, trades); live.push_back(next_id++);
        book.submit(next_id, Side::Sell, OrderType::Limit, mid + k, 50, trades); live.push_back(next_id++);
    }

    Stats passive, matching, cancels;
    passive.reserve(OPS); matching.reserve(OPS); cancels.reserve(OPS);
    long miss = 0;

    // Warmup: run a slice of the workload UNTIMED to fault in the arena pages
    // (the pool is ~100MB) and reach a steady book depth, so the timed loop
    // measures the data structure rather than first-touch page faults.
    const int WARMUP = OPS < 1'000'000 ? OPS / 4 : 1'000'000;
    for (int i = 0; i < WARMUP; ++i) {
        const Gen g = script[i];
        const Side side = g.side ? Side::Sell : Side::Buy;
        if (g.kind == 2) {
            if (live.empty()) continue;
            std::size_t k = (std::size_t)g.off % live.size();
            OrderId id = live[k]; live[k] = live.back(); live.pop_back();
            book.cancel(id);
        } else {
            Price px = (g.kind == 0) ? (side == Side::Buy ? mid - g.off : mid + g.off)
                                     : (side == Side::Buy ? mid + g.off : mid - g.off);
            if (px < MINP) px = MINP;
            if (px > MAXP) px = MAXP;
            OrderId id = next_id++;
            trades.clear();
            book.submit(id, side, OrderType::Limit, px, g.qty, trades);
            if (trades.empty()) live.push_back(id);
        }
        if ((i & 0x3FF) == 0) mid += (U(0,1) ? 1 : -1);
    }

    auto wall0 = std::chrono::steady_clock::now();

    for (int i = WARMUP; i < OPS; ++i) {
        const Gen g = script[i];
        const Side side = g.side ? Side::Sell : Side::Buy;

        if (g.kind == 2) {                       // cancel a live order
            if (live.empty()) continue;
            std::size_t k = (std::size_t)g.off % live.size();
            OrderId id = live[k];
            live[k] = live.back(); live.pop_back();

            std::uint64_t t0 = tsc();
            bool ok = book.cancel(id);
            std::uint64_t dt = tsc() - t0;
            if (ok) cancels.add(dt); else ++miss;
        } else {
            // passive rests away from the touch; marketable crosses it.
            Price px = (g.kind == 0)
                ? (side == Side::Buy ? mid - g.off : mid + g.off)
                : (side == Side::Buy ? mid + g.off : mid - g.off);
            if (px < MINP) px = MINP;
            if (px > MAXP) px = MAXP;
            OrderId id = next_id++;
            trades.clear();

            std::uint64_t t0 = tsc();
            book.submit(id, side, OrderType::Limit, px, g.qty, trades);
            std::uint64_t dt = tsc() - t0;

            if (!trades.empty()) matching.add(dt);
            else { passive.add(dt); live.push_back(id); }
        }

        if ((i & 0x3FF) == 0) mid += (U(0,1) ? 1 : -1);   // slow random-walk of the mid
    }

    auto wall1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(wall1 - wall0).count();
    const long timed = (long)OPS - WARMUP;

    std::printf("Workload: %ld timed ops (after %d warmup), %.3f s, %.2f M ops/s\n",
                timed, WARMUP, secs, timed / secs / 1e6);
    std::printf("Resting orders at end: %zu | cancel misses (already filled): %ld\n\n", book.restingCount(), miss);
    std::printf("Per-operation latency:\n");
    passive.report("submit (passive rest)", tsc_per_ns);
    matching.report("submit (crossed/traded)", tsc_per_ns);
    cancels.report("cancel (O(1))", tsc_per_ns);
    return 0;
}
