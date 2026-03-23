#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

namespace hft {

template <typename T, std::size_t N>
class SPSCQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static_assert(N >= 2,             "N must be at least 2");

    static constexpr std::size_t MASK = N - 1;
    static constexpr std::size_t CL   = 64;

    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    alignas(CL) std::atomic<std::size_t> write_{0};
    alignas(CL) std::atomic<std::size_t> read_{0};
    alignas(CL) std::array<Storage, N>   buf_;

public:
    SPSCQueue()  = default;
    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&)                 = delete;
    SPSCQueue& operator=(SPSCQueue&&)      = delete;

    [[nodiscard]] bool try_push(T val) noexcept {
        const std::size_t w    = write_.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & MASK;
        if (next == read_.load(std::memory_order_acquire)) return false;
        new (&buf_[w]) T(std::move(val));
        write_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        T* slot = std::launder(reinterpret_cast<T*>(&buf_[r]));
        out = std::move(*slot);
        slot->~T();
        read_.store((r + 1) & MASK, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool        empty()    const noexcept {
        return read_.load(std::memory_order_acquire) ==
               write_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return N - 1; }
    [[nodiscard]] std::size_t size()     const noexcept {
        const auto w = write_.load(std::memory_order_acquire);
        const auto r = read_.load(std::memory_order_acquire);
        return (w - r) & MASK;
    }
};

} // namespace hft
