#pragma once

#include <array>
#include <memory>
#include <cstddef>
#include <new>
#include <type_traits>
#include <cassert>

namespace hft {

template <typename T, std::size_t N>
class SlabAllocator {
    static_assert(N > 0,              "Pool size must be positive");
    static_assert(sizeof(T) >= sizeof(void*),
                  "T too small for intrusive free list");

    using RawStorage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    // Heap-allocated storage — avoids blowing the stack for large pools
    // Unique ownership, allocated once at construction, never reallocated
    std::unique_ptr<RawStorage[]> storage_;
    T*          free_head_{nullptr};
    std::size_t allocated_{0};

    [[nodiscard]] T* slot(std::size_t i) noexcept {
        return std::launder(reinterpret_cast<T*>(&storage_[i]));
    }

public:
    SlabAllocator() : storage_(std::make_unique<RawStorage[]>(N)) {
        for (std::size_t i = 0; i < N - 1; ++i)
            *reinterpret_cast<T**>(&storage_[i]) = slot(i + 1);
        *reinterpret_cast<T**>(&storage_[N - 1]) = nullptr;
        free_head_ = slot(0);
    }

    ~SlabAllocator() = default;

    SlabAllocator(const SlabAllocator&)            = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;
    SlabAllocator(SlabAllocator&&)                 = delete;
    SlabAllocator& operator=(SlabAllocator&&)      = delete;

    template <typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) noexcept {
        if (__builtin_expect(free_head_ == nullptr, 0)) return nullptr;
        T* p       = free_head_;
        free_head_ = *reinterpret_cast<T**>(p);
        ++allocated_;
        return new (p) T(std::forward<Args>(args)...);
    }

    void release(T* p) noexcept {
        assert(p != nullptr);
        p->~T();
        *reinterpret_cast<T**>(p) = free_head_;
        free_head_ = p;
        --allocated_;
    }

    [[nodiscard]] std::size_t allocated() const noexcept { return allocated_; }
    [[nodiscard]] std::size_t capacity()  const noexcept { return N; }
    [[nodiscard]] bool        full()      const noexcept { return free_head_ == nullptr; }
    [[nodiscard]] bool        empty()     const noexcept { return allocated_ == 0; }
};

} // namespace hft
