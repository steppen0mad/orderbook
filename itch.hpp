#pragma once
//
// itch.hpp -- a decoder for NASDAQ TotalView-ITCH 5.0.
//
// ITCH is the real protocol NASDAQ publishes its full order-by-order book on.
// Messages are big-endian binary; in the BinaryFILE packaging each message is
// preceded by a 2-byte big-endian length. We decode the message types needed
// to reconstruct the book and hand them to a callback sink. Unhandled types
// are skipped using the length prefix, so the parser is robust to the full
// stream, not just the subset we model.
//
// Price fields are uint32 in units of 1/10000 of a dollar -- already integers,
// so they map straight onto our tick prices with zero floating point.
//
#include "types.hpp"
#include <cstdint>
#include <cstddef>

namespace ob::itch {

// ---- big-endian field readers (ITCH is network byte order) ----
inline std::uint16_t be16(const std::uint8_t* p) {
    return (std::uint16_t(p[0]) << 8) | p[1];
}
inline std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8)  |  std::uint32_t(p[3]);
}
inline std::uint64_t be48(const std::uint8_t* p) {           // 6-byte timestamp
    std::uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v = (v << 8) | p[i];
    return v;
}
inline std::uint64_t be64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

// Decoded events we care about for book building.
struct AddOrder      { std::uint64_t ref; Side side; std::uint32_t shares; std::uint32_t price; };
struct OrderExecuted { std::uint64_t ref; std::uint32_t shares; };           // trade vs a resting order
struct OrderExecutedPx { std::uint64_t ref; std::uint32_t shares; std::uint32_t price; };
struct OrderCancel   { std::uint64_t ref; std::uint32_t shares; };           // partial reduce
struct OrderDelete   { std::uint64_t ref; };
struct OrderReplace  { std::uint64_t old_ref, new_ref; std::uint32_t shares; std::uint32_t price; };

// Parse a length-prefixed ITCH 5.0 buffer, dispatching to sink.on*(...).
// Returns the number of messages consumed. `sink` is any type exposing the
// onAdd/onExec/onExecPx/onCancel/onDelete/onReplace methods (duck-typed, so
// the parser stays allocation-free and fully inlinable).
template <class Sink>
std::size_t parse(const std::uint8_t* buf, std::size_t len, Sink& sink) {
    std::size_t off = 0, count = 0;
    while (off + 2 <= len) {
        std::uint16_t msglen = be16(buf + off);
        if (msglen == 0 || off + 2 + msglen > len) break;
        const std::uint8_t* m = buf + off + 2;     // message body (m[0] = type)
        switch (m[0]) {
            case 'A': case 'F': {                  // Add Order (F has trailing MPID)
                sink.onAdd(AddOrder{ be64(m + 11),
                                     m[19] == 'B' ? Side::Buy : Side::Sell,
                                     be32(m + 20), be32(m + 32) });
                break;
            }
            case 'E':                              // Order Executed
                sink.onExec(OrderExecuted{ be64(m + 11), be32(m + 19) });
                break;
            case 'C':                              // Order Executed With Price
                sink.onExecPx(OrderExecutedPx{ be64(m + 11), be32(m + 19), be32(m + 32) });
                break;
            case 'X':                              // Order Cancel (partial)
                sink.onCancel(OrderCancel{ be64(m + 11), be32(m + 19) });
                break;
            case 'D':                              // Order Delete (full)
                sink.onDelete(OrderDelete{ be64(m + 11) });
                break;
            case 'U':                              // Order Replace
                sink.onReplace(OrderReplace{ be64(m + 11), be64(m + 19), be32(m + 27), be32(m + 31) });
                break;
            default: break;                        // system/trade/etc: skip via length
        }
        off += 2 + msglen;
        ++count;
    }
    return count;
}

} // namespace ob::itch
