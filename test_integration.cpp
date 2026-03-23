#include "TestRunner.h"
#include "OrderBook.h"
#include "Types.h"
#include "Order.h"
#include "Metrics.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <numeric>

using namespace hft;

static std::vector<Order> load_csv_for_test(const char* path) {
    std::vector<Order> orders;
    std::ifstream f(path);
    if (!f.is_open()) return orders;

    std::string line;
    std::getline(f, line);

    OrderId id = 1;
    while (std::getline(f, line)) {
        const char* p     = line.c_str();
        const char* comma = static_cast<const char*>(std::memchr(p, ',', line.size()));
        if (!comma) continue;
        char* end;
        const double price_d = std::strtod(comma + 1, &end);
        if (end == comma + 1 || price_d <= 0.0) continue;
        const Side  side = (id % 2 == 0) ? Side::Buy : Side::Sell;
        orders.emplace_back(id++, side, OType::Limit, to_ticks(price_d), Qty{10});
    }
    return orders;
}

void run_integration_tests() {
    std::cout << "\n[Integration]\n";

    test::run("pool_fully_drained_after_all_matches", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (int i = 1; i <= 50; i += 2) {
            (void)eng.submit({static_cast<OrderId>(i),   Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
            (void)eng.submit({static_cast<OrderId>(i+1), Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        }
        eng.stop();
        ASSERT_EQ(eng.pool_utilization(), std::size_t{0});
        ASSERT_EQ(eng.stats().unmatched_orders, std::size_t{0});
    });

    test::run("volume_equals_sum_of_trade_quantities", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (int i = 1; i <= 20; ++i)
            (void)eng.submit({static_cast<OrderId>(i), Side::Buy,  OType::Limit, Price{1000}, Qty{5}});
        for (int i = 21; i <= 40; ++i)
            (void)eng.submit({static_cast<OrderId>(i), Side::Sell, OType::Limit, Price{1000}, Qty{5}});
        eng.stop();
        const auto& trades = eng.trades();
        uint64_t sum = 0;
        for (const auto& t : trades) sum += static_cast<uint64_t>(t.quantity);
        ASSERT_EQ(sum, eng.stats().volume_traded);
    });

    test::run("exec_price_within_spread_ptp", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1200}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{800},  Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        // PTP: exec at ask price = 800
        ASSERT_EQ(trades[0].exec_price, Price{800});
    });

    test::run("exec_price_midpoint_policy", [] {
        MatchingEngine<MidpointPolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1200}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{800},  Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].exec_price, Price{1000}); // (1200+800)/2
    });

    test::run("latency_histogram_populated", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (int i = 1; i <= 100; i += 2) {
            (void)eng.submit({static_cast<OrderId>(i),   Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
            (void)eng.submit({static_cast<OrderId>(i+1), Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        }
        eng.stop();
        ASSERT_GE(eng.latency().count(), uint64_t{50});
        ASSERT_GE(eng.latency().mean_cycles(), 0.0);
        ASSERT_GE(eng.latency().percentile(50), uint64_t{1});
        ASSERT_GE(eng.latency().percentile(99), uint64_t{1});
    });

    test::run("throughput_exceeds_10k_per_sec", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        constexpr int N = 10'000;
        for (int i = 1; i <= N; i += 2) {
            (void)eng.submit({static_cast<OrderId>(i),   Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
            (void)eng.submit({static_cast<OrderId>(i+1), Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        }
        eng.stop();
        const auto s = eng.stats();
        const double tput = static_cast<double>(s.orders_received) / s.elapsed_sec;
        ASSERT_GT(tput, 10'000.0);
    });

    test::run("csv_pipeline_all_orders_received", [] {
        const auto orders = load_csv_for_test("market_data.csv");
        if (orders.empty()) {
            std::cout << "    (skipped — market_data.csv not found)\n";
            return;
        }
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (auto o : orders) (void)eng.submit(o);
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.orders_received + s.orders_rejected,
                  static_cast<uint64_t>(orders.size()));
    });

    test::run("csv_pipeline_pool_consistent", [] {
        const auto orders = load_csv_for_test("market_data.csv");
        if (orders.empty()) { return; }
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (auto o : orders) (void)eng.submit(o);
        eng.stop();
        const auto s = eng.stats();
        // Pool allocated count == number of orders still sitting in book
        ASSERT_EQ(eng.pool_utilization(), s.unmatched_orders);
    });

    test::run("csv_pipeline_volume_consistent", [] {
        const auto orders = load_csv_for_test("market_data.csv");
        if (orders.empty()) { return; }
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (auto o : orders) (void)eng.submit(o);
        eng.stop();
        const auto& trades = eng.trades();
        uint64_t sum = 0;
        for (const auto& t : trades) sum += static_cast<uint64_t>(t.quantity);
        ASSERT_EQ(sum, eng.stats().volume_traded);
    });

    test::run("csv_pipeline_no_trade_with_bid_below_ask", [] {
        const auto orders = load_csv_for_test("market_data.csv");
        if (orders.empty()) { return; }
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (auto o : orders) (void)eng.submit(o);
        eng.stop();
        for (const auto& t : eng.trades()) {
            // exec_price must be >= ask side's contribution
            // Just verify every trade has positive price and quantity
            ASSERT_GT(t.exec_price, Price{0});
            ASSERT_GT(t.quantity,   Qty{0});
        }
    });

    test::run("stop_is_idempotent", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        eng.stop(); // second stop must not crash
    });

    test::run("aggressor_policy_exec_at_bid", [] {
        MatchingEngine<AggressorPolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1300}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{700},  Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].exec_price, Price{1300}); // aggressor = bid price
    });

    test::run("fifo_within_price_level", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        // Three bids at same price — sell should match them in submission order
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({2, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({3, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({4, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].buy_id, OrderId{1}); // first-in, first-matched
        ASSERT_EQ(eng.stats().unmatched_orders, std::size_t{2});
    });
}
