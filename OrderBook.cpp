#include "OrderBook.h"

#include <iostream>
#include <iomanip>
#include <algorithm>

namespace hft {

template <typename Policy>
MatchingEngine<Policy>::MatchingEngine() {
    trades_.reserve(MAX_TRADES);
}

template <typename Policy>
MatchingEngine<Policy>::~MatchingEngine() {
    stop();
}

template <typename Policy>
void MatchingEngine<Policy>::start() {
    running_.store(true, std::memory_order_release);
    start_tp_ = std::chrono::high_resolution_clock::now();
    match_thread_ = std::thread([this] { run_match_loop(); });
}

template <typename Policy>
void MatchingEngine<Policy>::stop() {
    running_.store(false, std::memory_order_release);
    if (match_thread_.joinable()) match_thread_.join();
    stop_tp_ = std::chrono::high_resolution_clock::now();
}

template <typename Policy>
bool MatchingEngine<Policy>::submit(Order order) noexcept {
    if (!order.is_valid()) {
        reject_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    recv_count_.fetch_add(1, std::memory_order_relaxed);
    while (!inbound_.try_push(order)) {
        if (!running_.load(std::memory_order_acquire)) return false;
        std::this_thread::yield();
    }
    return true;
}

template <typename Policy>
void MatchingEngine<Policy>::run_match_loop() noexcept {
    Order incoming;
    while (true) {
        if (!inbound_.try_pop(incoming)) {
            if (!running_.load(std::memory_order_acquire) && inbound_.empty()) break;
            std::this_thread::yield();
            continue;
        }

        const uint64_t t0 = rdtsc();

        Order* o = pool_.acquire(incoming);
        if (__builtin_expect(o == nullptr, 0)) {
            reject_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (o->is_buy()) bids_[o->price].push_back(o);
        else             asks_[o->price].push_back(o);

        try_match();

        match_latency_.record(rdtscp() - t0);
    }
}

template <typename Policy>
void MatchingEngine<Policy>::try_match() noexcept {
    while (!bids_.empty() && !asks_.empty()) {
        auto bid_rit  = bids_.rbegin();
        auto ask_it   = asks_.begin();
        const Price best_bid = bid_rit->first;
        const Price best_ask = ask_it->first;

        if (!policy_.can_match(best_bid, best_ask)) break;

        Order* bid = bid_rit->second.front();
        Order* ask = ask_it->second.front();

        settle(bid, ask);

        if (bid->is_filled()) {
            bid_rit->second.pop_front();
            pool_.release(bid);
            if (bid_rit->second.empty()) bids_.erase(best_bid);
        }

        if (ask->is_filled()) {
            ask_it->second.pop_front();
            pool_.release(ask);
            if (ask_it->second.empty()) asks_.erase(ask_it);
        }
    }
}

template <typename Policy>
void MatchingEngine<Policy>::settle(Order* bid, Order* ask) noexcept {
    const Qty matched = std::min(bid->remaining(), ask->remaining());
    bid->filled += matched;
    ask->filled += matched;

    static constexpr OStatus lut[2] = { OStatus::PartiallyFilled, OStatus::Filled };
    bid->status = lut[bid->is_filled() ? 1 : 0];
    ask->status = lut[ask->is_filled() ? 1 : 0];

    const Price    exec = policy_.exec_price(bid->price, ask->price);
    const uint64_t tsc  = rdtscp();

    if (trades_.size() < MAX_TRADES)
        trades_.push_back({bid->id, ask->id, exec, matched, 0u, tsc});

    trade_count_.fetch_add(1,                              std::memory_order_relaxed);
    volume_.fetch_add(static_cast<uint64_t>(matched),     std::memory_order_relaxed);
}

template <typename Policy>
BookStats MatchingEngine<Policy>::stats() const noexcept {
    const double dur = std::chrono::duration<double>(stop_tp_ - start_tp_).count();
    std::size_t unmatched = 0;
    for (const auto& [px, q] : bids_) unmatched += q.size();
    for (const auto& [px, q] : asks_) unmatched += q.size();
    return {
        recv_count_.load(std::memory_order_relaxed),
        reject_count_.load(std::memory_order_relaxed),
        trade_count_.load(std::memory_order_relaxed),
        volume_.load(std::memory_order_relaxed),
        unmatched,
        dur
    };
}

template <typename Policy>
void MatchingEngine<Policy>::print_depth(std::size_t levels) const {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\nTop " << levels << " bid levels:\n";
    std::size_t n = 0;
    for (auto it = bids_.rbegin(); it != bids_.rend() && n < levels; ++it, ++n) {
        Qty total = 0;
        for (const auto* o : it->second) total += o->remaining();
        std::cout << "  " << from_ticks(it->first) << "  x  " << total << '\n';
    }
    std::cout << "Top " << levels << " ask levels:\n";
    n = 0;
    for (auto it = asks_.begin(); it != asks_.end() && n < levels; ++it, ++n) {
        Qty total = 0;
        for (const auto* o : it->second) total += o->remaining();
        std::cout << "  " << from_ticks(it->first) << "  x  " << total << '\n';
    }
}

template class MatchingEngine<PriceTimePolicy>;
template class MatchingEngine<MidpointPolicy>;
template class MatchingEngine<AggressorPolicy>;

} // namespace hft
