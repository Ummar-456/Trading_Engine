# hft-engine

A low-latency order book matching engine written in modern C++17. Built around the principles used in production trading systems: lock-free data structures, zero heap allocation on the hot path, CRTP policy dispatch, and cache-line-aware memory layout.

---

## Results

Running against 5,284 real market close prices from `market_data.csv`:

| Metric | Value |
|---|---|
| Orders processed | 5,284 |
| Orders rejected | 0 |
| Trades executed | 2,514 |
| Volume traded | 25,140 units |
| Throughput | ~166,000 orders/sec |
| Match latency p50 | ~256 cycles (~90 ns @ 3 GHz) |
| Match latency p99 | ~4,096 cycles (~1.4 µs @ 3 GHz) |
| Pool leak check | `unmatched_orders == pool_utilization` ✓ |
| Test suite | **47 / 47 pass** |

---

## Architecture

```
market_data.csv
      │
      ▼
  main.cpp  ──submit()──►  SPSCQueue<Order, 65536>
                                    │
                                    ▼ (match thread)
                         SlabAllocator<Order, 200k>
                                    │
                                    ▼
                      MatchingEngine<Policy>
                       ├── bids: map<Price, deque<Order*>>
                       ├── asks: map<Price, deque<Order*>>
                       └── Policy::exec_price()  [CRTP, zero-cost]
                                    │
                              ┌─────┴─────┐
                              ▼           ▼
                           Trade[]   LatencyHistogram
```

The hot path is a single thread reading from the SPSC queue, acquiring order slots from the slab, placing them into the price-level maps, and running the match loop. No locks are taken on the matching thread. The submitting thread only touches the SPSC write head.

---

## Design Decisions

### Lock-free SPSC queue
`SPSCQueue<T, N>` is a power-of-2 ring buffer with producer and consumer heads on separate cache lines. Uses `memory_order_release` on writes and `memory_order_acquire` on reads — which on x86 TSO compiles to ordinary MOV instructions at zero cost. No mutex, no futex, no syscall.

### Slab allocator
`SlabAllocator<T, N>` pre-allocates all order storage at startup using a heap-backed array and an intrusive free list. `acquire()` and `release()` are pointer swaps — no `malloc`, no `free`, no heap lock on the hot path. Pool exhaustion returns `nullptr` rather than throwing, which the engine handles gracefully.

### CRTP matching policies
Matching strategy is a compile-time template parameter, not a runtime virtual dispatch. `MatchingEngine<PriceTimePolicy>`, `MatchingEngine<MidpointPolicy>`, and `MatchingEngine<AggressorPolicy>` are separate instantiations. The policy methods inline completely — zero branches, zero vtable lookups.

### Integer prices
All prices are stored as `int64_t` ticks (`price * 1000`). Floating-point is only used at the boundary (CSV parse and display output). Integer comparison is cheaper than floating-point, and there are no rounding errors in the matching logic.

### Cache-line-sized Order
`Order` is exactly 64 bytes and `alignas(64)`. A single order fits in one cache line and never straddles two. `static_assert` enforces the layout at compile time — a size change is a build error, not a silent regression.

### `rdtsc`-based latency
Latency is measured with `rdtsc`/`rdtscp` directly. The histogram uses log2 bucketing so it occupies 64 atomics and records in O(1) with a single `__builtin_clzll` call.

### Async logger
`Logger` runs a dedicated drain thread consuming from its own SPSC queue. The hot path never touches a file descriptor. `stop()` correctly joins the thread regardless of whether `set_enabled()` was called after `start()` — a race condition present in many naive logger implementations.

---

## Building

**Requirements:** GCC 9+ or Clang 10+, C++17, pthreads, Linux x86-64.

```bash
# Build and run all 47 tests
make test

# Build release binary and run against market_data.csv
make run

# Run with full trade list and order book depth
make run_verbose

# Build with AddressSanitizer + UBSan
make test_asan

# Clean everything
make clean
```

**CLI flags for the engine binary:**

```
--csv <path>          Path to market data CSV  (default: market_data.csv)
--trades              Print recent trades
--depth               Print top-5 bid/ask levels
--log                 Enable async file logger  (writes trading_engine.log)
--max-trades=N        Number of trades to print (default: 50)
--levels=N            Depth levels to print     (default: 5)
```

---

## Test Suite

| Suite | Tests | What is covered |
|---|---|---|
| `SPSCQueue` | 9 | Empty pop, FIFO order, capacity limits, refill after drain, concurrent 1M-element round-trip |
| `SlabAllocator` | 6 | Acquire/release, allocation counter, pool exhaustion, slot reuse, full cycle leak check |
| `Types & Layout` | 5 | `sizeof(Order)==64`, `sizeof(Trade)==40`, tick round-trips, `is_valid()` guards, fill tracking |
| `MatchingPolicy` | 3 | PriceTime exec at ask, Midpoint exec at mid, Aggressor exec at bid |
| `MatchingEngine` | 11 | No match below spread, full fill, partial fills, multi-fill, price priority both sides, rejection counting, N×N all-match |
| `Integration` | 13 | Pool drain invariant, volume consistency, exec price correctness per policy, latency histogram, throughput floor, full CSV pipeline, stop idempotency, FIFO within price level |

---

## How Success Is Measured

**Memory correctness:** After `stop()`, `pool_utilization()` must equal `stats().unmatched_orders`. Every live `Order*` in the slab corresponds to exactly one order still sitting in the book. Any divergence is a use-after-free or a leak.

**Trade correctness:** Every executed trade's `exec_price` is the ask price under `PriceTimePolicy`, the midpoint under `MidpointPolicy`, and the bid price under `AggressorPolicy`. The test suite verifies each with exact `Price` tick comparisons.

**Volume consistency:** `Σ trade.quantity == stats().volume_traded`. Verified as an integration test invariant on both synthetic workloads and the full CSV run.

**Throughput floor:** The integration test `throughput_exceeds_10k_per_sec` hard-fails if throughput drops below 10,000 orders/second. The observed rate on this machine is ~166,000/sec, leaving substantial headroom.

**Zero warnings:** The build uses `-Wall -Wextra -Wpedantic -Wshadow -Wnull-dereference`. Any new warning is a build failure.

---

## What This Does Not Cover (Yet)

- **Kernel bypass networking** — in production, the feed thread would read from a NIC directly via DPDK or Solarflare OpenOnload, not from a CSV file
- **CPU affinity and SCHED_FIFO** — the `pin_thread_to_core()` scaffolding is designed for it; Linux requires root or `CAP_SYS_NICE` to set `SCHED_FIFO`
- **Market orders and stop orders** — the type enum and validation path are wired; the routing logic in `run_match_loop()` treats all orders as limit orders currently
- **Cancel and replace** — `cancelOrder()` was intentionally omitted here (it was a stub in the original codebase); the slab design supports it cleanly via `release()`
- **Persistence and recovery** — trades are stored in a `std::vector` capped at 500k; a production system would drain to a ring-buffered journal
