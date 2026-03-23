#include "TestRunner.h"
#include "OrderBook.h"
#include "Types.h"
#include "Order.h"
#include "Trade.h"
#include "MatchingPolicy.h"

#include <thread>
#include <chrono>

using namespace hft;

void run_matching_tests() {
    std::cout << "\n[Types & Layout]\n";

    test::run("order_size_is_64", [] {
        ASSERT_EQ(sizeof(Order), std::size_t{64});
        ASSERT_EQ(alignof(Order), std::size_t{64});
    });

    test::run("trade_size_is_40", [] {
        ASSERT_EQ(sizeof(Trade), std::size_t{40});
    });

    test::run("tick_round_trip", [] {
        ASSERT_EQ(to_ticks(1.000), Price{1000});
        ASSERT_EQ(to_ticks(0.999), Price{999});
        ASSERT_EQ(to_ticks(1.500), Price{1500});
        const Price p = to_ticks(1.234);
        ASSERT_EQ(p, Price{1234});
    });

    test::run("order_is_valid_guards", [] {
        Order good{1, Side::Buy, OType::Limit, Price{1000}, Qty{10}};
        ASSERT_TRUE(good.is_valid());

        Order zero_qty{2, Side::Buy, OType::Limit, Price{1000}, Qty{0}};
        ASSERT_TRUE(!zero_qty.is_valid());

        Order zero_px{3, Side::Buy, OType::Limit, Price{0}, Qty{10}};
        ASSERT_TRUE(!zero_px.is_valid());

        Order market_no_px{4, Side::Buy, OType::Market, Price{0}, Qty{10}};
        ASSERT_TRUE(market_no_px.is_valid()); // market orders need no price
    });

    test::run("order_fill_tracking", [] {
        Order o{1, Side::Buy, OType::Limit, Price{1000}, Qty{30}};
        ASSERT_EQ(o.remaining(), Qty{30});
        ASSERT_TRUE(!o.is_filled());

        o.filled = 10;
        ASSERT_EQ(o.remaining(), Qty{20});
        ASSERT_TRUE(!o.is_filled());

        o.filled = 30;
        ASSERT_TRUE(o.is_filled());
        ASSERT_EQ(o.remaining(), Qty{0});
    });

    std::cout << "\n[MatchingPolicy]\n";

    test::run("price_time_policy_exec_at_ask", [] {
        PriceTimePolicy p;
        ASSERT_TRUE(p.can_match(Price{1050}, Price{1000}));
        ASSERT_TRUE(p.can_match(Price{1000}, Price{1000}));
        ASSERT_TRUE(!p.can_match(Price{900}, Price{1000}));
        ASSERT_EQ(p.exec_price(Price{1050}, Price{1000}), Price{1000});
    });

    test::run("midpoint_policy_exec_at_mid", [] {
        MidpointPolicy p;
        ASSERT_TRUE(p.can_match(Price{1050}, Price{1000}));
        ASSERT_EQ(p.exec_price(Price{1100}, Price{900}), Price{1000});
        ASSERT_EQ(p.exec_price(Price{1001}, Price{999}), Price{1000});
    });

    test::run("aggressor_policy_exec_at_bid", [] {
        AggressorPolicy p;
        ASSERT_TRUE(p.can_match(Price{1050}, Price{1000}));
        ASSERT_EQ(p.exec_price(Price{1050}, Price{1000}), Price{1050});
    });

    std::cout << "\n[MatchingEngine<PriceTimePolicy>]\n";

    test::run("no_match_when_bid_below_ask", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{900},  Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{0});
        ASSERT_EQ(s.orders_received, uint64_t{2});
        ASSERT_EQ(s.unmatched_orders, std::size_t{2});
    });

    test::run("simple_full_match", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{1});
        ASSERT_EQ(s.volume_traded,   uint64_t{10});
        ASSERT_EQ(s.unmatched_orders, std::size_t{0});
        ASSERT_EQ(eng.pool_utilization(), std::size_t{0});

        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].buy_id,    OrderId{1});
        ASSERT_EQ(trades[0].sell_id,   OrderId{2});
        ASSERT_EQ(trades[0].exec_price, Price{1000}); // PTP: exec at ask
        ASSERT_EQ(trades[0].quantity,  Qty{10});
    });

    test::run("partial_fill_buy_larger", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1000}, Qty{30}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{1});
        ASSERT_EQ(s.volume_traded,   uint64_t{10});
        // Buy order is partially filled — remains in book
        ASSERT_EQ(s.unmatched_orders, std::size_t{1});
        ASSERT_EQ(eng.pool_utilization(), std::size_t{1}); // bid still allocated
    });

    test::run("partial_fill_sell_larger", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{30}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{1});
        ASSERT_EQ(s.volume_traded,   uint64_t{10});
        ASSERT_EQ(s.unmatched_orders, std::size_t{1}); // ask remains
        ASSERT_EQ(eng.pool_utilization(), std::size_t{1});
    });

    test::run("multiple_fills_one_bid", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1000}, Qty{30}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({3, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        (void)eng.submit({4, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{3});
        ASSERT_EQ(s.volume_traded,   uint64_t{30});
        ASSERT_EQ(s.unmatched_orders, std::size_t{0});
        ASSERT_EQ(eng.pool_utilization(), std::size_t{0});
    });

    test::run("price_priority_best_bid_matches_first", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        // Two bids at different prices — higher bid should match first
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{900},  Qty{10}});
        (void)eng.submit({2, Side::Buy,  OType::Limit, Price{1100}, Qty{10}});
        (void)eng.submit({3, Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].buy_id, OrderId{2}); // higher bid matched, not id=1
        ASSERT_EQ(eng.stats().unmatched_orders, std::size_t{1}); // lower bid remains
    });

    test::run("price_priority_best_ask_matches_first", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        // Two asks at different prices — lower ask matches first
        (void)eng.submit({1, Side::Sell, OType::Limit, Price{1100}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{900},  Qty{10}});
        (void)eng.submit({3, Side::Buy,  OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].sell_id, OrderId{2}); // lower ask matched
        ASSERT_EQ(eng.stats().unmatched_orders, std::size_t{1}); // higher ask remains
    });

    test::run("invalid_orders_rejected", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{0},    Qty{10}}); // bad price
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{1000}, Qty{0}});  // bad qty
        (void)eng.submit({3, Side::Buy,  OType::Limit, Price{1000}, Qty{10}}); // good
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.orders_received, uint64_t{1}); // only good one counted
        ASSERT_EQ(s.orders_rejected, uint64_t{2});
    });

    test::run("midpoint_policy_exec_price", [] {
        MatchingEngine<MidpointPolicy> eng;
        eng.start();
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1100}, Qty{10}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{900},  Qty{10}});
        eng.stop();
        const auto& trades = eng.trades();
        ASSERT_EQ(trades.size(), std::size_t{1});
        ASSERT_EQ(trades[0].exec_price, Price{1000}); // midpoint of 1100 and 900
    });

    test::run("n_buys_n_sells_all_match", [] {
        constexpr int N = 100;
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        for (int i = 1; i <= N; ++i)
            (void)eng.submit({static_cast<OrderId>(i),
                        Side::Buy, OType::Limit, Price{1000}, Qty{10}});
        for (int i = N + 1; i <= 2 * N; ++i)
            (void)eng.submit({static_cast<OrderId>(i),
                        Side::Sell, OType::Limit, Price{1000}, Qty{10}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed,  uint64_t{N});
        ASSERT_EQ(s.volume_traded,    uint64_t{N * 10});
        ASSERT_EQ(s.unmatched_orders, std::size_t{0});
        ASSERT_EQ(eng.pool_utilization(), std::size_t{0});
    });

    test::run("crossing_spread_all_fill", [] {
        MatchingEngine<PriceTimePolicy> eng;
        eng.start();
        // Bid at 1100, ask at 900 — should match immediately
        (void)eng.submit({1, Side::Buy,  OType::Limit, Price{1100}, Qty{15}});
        (void)eng.submit({2, Side::Sell, OType::Limit, Price{900},  Qty{15}});
        eng.stop();
        const auto s = eng.stats();
        ASSERT_EQ(s.trades_executed, uint64_t{1});
        ASSERT_EQ(s.volume_traded,   uint64_t{15});
        ASSERT_EQ(eng.pool_utilization(), std::size_t{0});
    });
}
