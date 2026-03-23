#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <chrono>

namespace hft {

[[nodiscard]] inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
    return static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}

[[nodiscard]] inline uint64_t rdtscp() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi, aux;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
    return rdtsc();
#endif
}

class LatencyHistogram {
    static constexpr std::size_t BUCKETS = 64;
    std::array<std::atomic<uint64_t>, BUCKETS> counts_{};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> sum_{0};

public:
    LatencyHistogram() = default;

    LatencyHistogram(const LatencyHistogram&)            = delete;
    LatencyHistogram& operator=(const LatencyHistogram&) = delete;

    void record(uint64_t cycles) noexcept {
        const unsigned clz   = (cycles == 0) ? 64u
            : static_cast<unsigned>(__builtin_clzll(cycles));
        const std::size_t b  = std::min<std::size_t>(BUCKETS - 1, 63u - clz);
        counts_[b].fetch_add(1, std::memory_order_relaxed);
        total_.fetch_add(1,      std::memory_order_relaxed);
        sum_.fetch_add(cycles,   std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t count() const noexcept {
        return total_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] double mean_cycles() const noexcept {
        const uint64_t n = total_.load(std::memory_order_relaxed);
        if (n == 0) return 0.0;
        return static_cast<double>(sum_.load(std::memory_order_relaxed))
             / static_cast<double>(n);
    }

    [[nodiscard]] uint64_t percentile(int pct) const noexcept {
        const uint64_t n = total_.load(std::memory_order_relaxed);
        if (n == 0) return 0;
        const uint64_t target = (n * static_cast<uint64_t>(pct)) / 100;
        uint64_t cumul = 0;
        for (std::size_t i = 0; i < BUCKETS; ++i) {
            cumul += counts_[i].load(std::memory_order_relaxed);
            if (cumul > target)
                return (i == 0) ? 1u : (uint64_t{1} << i);
        }
        return uint64_t{1} << (BUCKETS - 1);
    }
};

} // namespace hft
