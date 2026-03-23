#include "TestRunner.h"
#include "SPSCQueue.h"

#include <thread>
#include <atomic>
#include <vector>
#include <numeric>

using namespace hft;

void run_spsc_tests() {
    std::cout << "\n[SPSCQueue]\n";

    test::run("empty_queue_pop_fails", [] {
        SPSCQueue<int, 8> q;
        int out = -1;
        ASSERT_TRUE(!q.try_pop(out));
        ASSERT_EQ(out, -1);
        ASSERT_TRUE(q.empty());
    });

    test::run("push_pop_single", [] {
        SPSCQueue<int, 8> q;
        ASSERT_TRUE(q.try_push(42));
        int out = 0;
        ASSERT_TRUE(q.try_pop(out));
        ASSERT_EQ(out, 42);
        ASSERT_TRUE(q.empty());
    });

    test::run("fifo_order", [] {
        SPSCQueue<int, 16> q;
        for (int i = 0; i < 8; ++i) ASSERT_TRUE(q.try_push(i));
        for (int i = 0; i < 8; ++i) {
            int out;
            ASSERT_TRUE(q.try_pop(out));
            ASSERT_EQ(out, i);
        }
    });

    test::run("capacity_is_n_minus_1", [] {
        SPSCQueue<int, 8> q;
        ASSERT_EQ(q.capacity(), std::size_t{7});
        for (int i = 0; i < 7; ++i) ASSERT_TRUE(q.try_push(i));
        ASSERT_TRUE(!q.try_push(99)); // full
    });

    test::run("full_queue_push_fails", [] {
        SPSCQueue<int, 4> q;
        ASSERT_TRUE(q.try_push(1));
        ASSERT_TRUE(q.try_push(2));
        ASSERT_TRUE(q.try_push(3));
        ASSERT_TRUE(!q.try_push(4)); // 4th push fills the 4-1=3 capacity
    });

    test::run("size_tracks_correctly", [] {
        SPSCQueue<int, 16> q;
        ASSERT_EQ(q.size(), std::size_t{0});
        (void)q.try_push(1);
        ASSERT_EQ(q.size(), std::size_t{1});
        (void)q.try_push(2);
        ASSERT_EQ(q.size(), std::size_t{2});
        int x;
        (void)q.try_pop(x);
        ASSERT_EQ(q.size(), std::size_t{1});
    });

    test::run("refill_after_drain", [] {
        SPSCQueue<int, 8> q;
        for (int r = 0; r < 3; ++r) {
            for (int i = 0; i < 7; ++i) ASSERT_TRUE(q.try_push(i));
            for (int i = 0; i < 7; ++i) {
                int out;
                ASSERT_TRUE(q.try_pop(out));
                ASSERT_EQ(out, i);
            }
            ASSERT_TRUE(q.empty());
        }
    });

    test::run("non_trivial_type", [] {
        SPSCQueue<std::string, 8> q;
        ASSERT_TRUE(q.try_push("hello"));
        ASSERT_TRUE(q.try_push("world"));
        std::string a, b;
        ASSERT_TRUE(q.try_pop(a));
        ASSERT_TRUE(q.try_pop(b));
        ASSERT_TRUE(a == "hello");
        ASSERT_TRUE(b == "world");
    });

    test::run("concurrent_producer_consumer_1M", [] {
        static constexpr int N = 1'000'000;
        SPSCQueue<int, 65536> q;
        std::atomic<bool> done{false};
        std::atomic<long> sum_produced{0}, sum_consumed{0};

        std::thread producer([&] {
            for (int i = 1; i <= N; ++i) {
                while (!q.try_push(i)) std::this_thread::yield();
                sum_produced.fetch_add(i, std::memory_order_relaxed);
            }
            done.store(true, std::memory_order_release);
        });

        std::thread consumer([&] {
            int val;
            long count = 0;
            while (count < N) {
                if (q.try_pop(val)) {
                    sum_consumed.fetch_add(val, std::memory_order_relaxed);
                    ++count;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        producer.join();
        consumer.join();

        const long expected = static_cast<long>(N) * (N + 1) / 2;
        ASSERT_EQ(sum_produced.load(), expected);
        ASSERT_EQ(sum_consumed.load(), expected);
        ASSERT_TRUE(q.empty());
    });
}
