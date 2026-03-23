#pragma once

#include "Types.h"
#include "Order.h"
#include "Trade.h"
#include "SPSCQueue.h"
#include "SlabAllocator.h"
#include "MatchingPolicy.h"
#include "Metrics.h"

#include <map>
#include <deque>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>

namespace hft {

struct BookStats {
    uint64_t    orders_received{0};
    uint64_t    orders_rejected{0};
    uint64_t    trades_executed{0};
    uint64_t    volume_traded{0};
    std::size_t unmatched_orders{0};
    double      elapsed_sec{0.0};
};

template <typename Policy = PriceTimePolicy>
class MatchingEngine {
    static constexpr std::size_t POOL_SIZE  = 200'000;
    static constexpr std::size_t QUEUE_CAP  = 65'536;
    static constexpr std::size_t MAX_TRADES = 500'000;

    using LevelMap = std::map<Price, std::deque<Order*>>;

    LevelMap bids_;  // highest price = best bid  → iterate rbegin
    LevelMap asks_;  // lowest price  = best ask  → iterate begin

    SlabAllocator<Order, POOL_SIZE> pool_;
    SPSCQueue<Order, QUEUE_CAP>     inbound_;

    [[no_unique_address]] Policy policy_;

    std::vector<Trade>    trades_;

    std::atomic<bool>     running_{false};
    std::thread           match_thread_;

    std::atomic<uint64_t> recv_count_{0};
    std::atomic<uint64_t> reject_count_{0};
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> volume_{0};

    LatencyHistogram match_latency_;

    std::chrono::high_resolution_clock::time_point start_tp_;
    std::chrono::high_resolution_clock::time_point stop_tp_;

    void run_match_loop() noexcept;
    void try_match()      noexcept;
    void settle(Order* bid, Order* ask) noexcept;

public:
    MatchingEngine();
    ~MatchingEngine();

    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&)                 = delete;
    MatchingEngine& operator=(MatchingEngine&&)      = delete;

    void start();
    void stop();

    [[nodiscard]] bool submit(Order order) noexcept;

    // Safe only after stop()
    [[nodiscard]] BookStats              stats()           const noexcept;
    [[nodiscard]] const std::vector<Trade>& trades()       const noexcept { return trades_; }
    [[nodiscard]] const LatencyHistogram& latency()        const noexcept { return match_latency_; }
    [[nodiscard]] std::size_t            pool_utilization()const noexcept { return pool_.allocated(); }

    void print_depth(std::size_t levels = 5) const;
};

extern template class MatchingEngine<PriceTimePolicy>;
extern template class MatchingEngine<MidpointPolicy>;
extern template class MatchingEngine<AggressorPolicy>;

} // namespace hft
