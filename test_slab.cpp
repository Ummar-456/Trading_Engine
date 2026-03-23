#include "TestRunner.h"
#include "SlabAllocator.h"
#include "Order.h"

using namespace hft;

void run_slab_tests() {
    std::cout << "\n[SlabAllocator]\n";

    test::run("acquire_returns_valid_pointer", [] {
        SlabAllocator<Order, 16> slab;
        Order* o = slab.acquire(1u, Side::Buy, OType::Limit, Price{1000}, Qty{10});
        ASSERT_TRUE(o != nullptr);
        ASSERT_EQ(o->id, OrderId{1});
        ASSERT_EQ(o->price, Price{1000});
        ASSERT_EQ(o->quantity, Qty{10});
        ASSERT_EQ(o->filled, Qty{0});
        slab.release(o);
    });

    test::run("allocation_count_tracks_correctly", [] {
        SlabAllocator<Order, 8> slab;
        ASSERT_EQ(slab.allocated(), std::size_t{0});
        auto* a = slab.acquire(1u, Side::Buy,  OType::Limit, Price{100}, Qty{10});
        auto* b = slab.acquire(2u, Side::Sell, OType::Limit, Price{200}, Qty{20});
        ASSERT_EQ(slab.allocated(), std::size_t{2});
        slab.release(a);
        ASSERT_EQ(slab.allocated(), std::size_t{1});
        slab.release(b);
        ASSERT_EQ(slab.allocated(), std::size_t{0});
        ASSERT_TRUE(slab.empty());
    });

    test::run("pool_exhaustion_returns_nullptr", [] {
        SlabAllocator<Order, 4> slab;
        std::vector<Order*> ptrs;
        for (int i = 0; i < 4; ++i)
            ptrs.push_back(slab.acquire(static_cast<OrderId>(i + 1),
                           Side::Buy, OType::Limit, Price{100}, Qty{1}));
        for (auto* p : ptrs) ASSERT_TRUE(p != nullptr);
        ASSERT_TRUE(slab.full());

        Order* overflow = slab.acquire(99u, Side::Buy, OType::Limit, Price{100}, Qty{1});
        ASSERT_TRUE(overflow == nullptr);

        for (auto* p : ptrs) slab.release(p);
        ASSERT_EQ(slab.allocated(), std::size_t{0});
    });

    test::run("release_and_reacquire", [] {
        SlabAllocator<Order, 2> slab;
        auto* a = slab.acquire(1u, Side::Buy, OType::Limit, Price{500}, Qty{5});
        ASSERT_TRUE(a != nullptr);
        slab.release(a);

        // Slab should hand back the same memory slot
        auto* b = slab.acquire(2u, Side::Sell, OType::Limit, Price{600}, Qty{6});
        ASSERT_TRUE(b != nullptr);
        ASSERT_EQ(b->id, OrderId{2}); // freshly constructed
        ASSERT_EQ(b->price, Price{600});
        slab.release(b);
    });

    test::run("objects_in_contiguous_storage", [] {
        SlabAllocator<Order, 4> slab;
        auto* a = slab.acquire(1u, Side::Buy,  OType::Limit, Price{100}, Qty{1});
        auto* b = slab.acquire(2u, Side::Sell, OType::Limit, Price{200}, Qty{2});
        auto* c = slab.acquire(3u, Side::Buy,  OType::Limit, Price{300}, Qty{3});

        // Pointers should differ (each at a distinct slot)
        ASSERT_NE(reinterpret_cast<uintptr_t>(a),
                  reinterpret_cast<uintptr_t>(b));
        ASSERT_NE(reinterpret_cast<uintptr_t>(b),
                  reinterpret_cast<uintptr_t>(c));

        slab.release(a);
        slab.release(b);
        slab.release(c);
        ASSERT_TRUE(slab.empty());
    });

    test::run("no_leak_after_full_cycle", [] {
        SlabAllocator<Order, 64> slab;
        std::vector<Order*> ptrs;
        ptrs.reserve(64);
        for (int i = 0; i < 64; ++i)
            ptrs.push_back(slab.acquire(static_cast<OrderId>(i + 1),
                           Side::Buy, OType::Limit, Price{100 + i}, Qty{10}));
        ASSERT_EQ(slab.allocated(), std::size_t{64});
        ASSERT_TRUE(slab.full());
        for (auto* p : ptrs) slab.release(p);
        ASSERT_EQ(slab.allocated(), std::size_t{0});
        ASSERT_TRUE(slab.empty());
        ASSERT_TRUE(!slab.full());
    });
}
