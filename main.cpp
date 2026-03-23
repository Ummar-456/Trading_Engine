#include "OrderBook.h"
#include "Logger.h"
#include "Types.h"
#include "Order.h"
#include "Metrics.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <stdexcept>
#include <string_view>

using namespace hft;

static std::vector<Order> load_csv(const char* path) {
    std::vector<Order> orders;
    orders.reserve(6'000);

    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error(std::string("Cannot open: ") + path);

    std::string line;
    std::getline(f, line); // header

    OrderId  id  = 1;
    constexpr Qty QTY = 10;

    while (std::getline(f, line)) {
        const char* p     = line.c_str();
        const char* comma = static_cast<const char*>(
            std::memchr(p, ',', line.size()));
        if (!comma) continue;

        char* end;
        const double price_d = std::strtod(comma + 1, &end);
        if (end == comma + 1 || price_d <= 0.0) continue;

        const Side  side = (id % 2 == 0) ? Side::Buy : Side::Sell;
        const Price px   = to_ticks(price_d);
        orders.emplace_back(id++, side, OType::Limit, px, QTY);
    }
    return orders;
}

static void print_banner(std::string_view label) {
    std::cout << "\n=== " << label << " ===\n";
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    bool        log_enabled  = false;
    bool        print_trades = false;
    bool        print_depth  = false;
    const char* csv_path     = "market_data.csv";
    std::size_t max_trades   = 50;
    std::size_t depth_levels = 5;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};
        if      (arg == "--log")        log_enabled  = true;
        else if (arg == "--trades")     print_trades = true;
        else if (arg == "--depth")      print_depth  = true;
        else if (arg == "--csv" && i + 1 < argc)  csv_path = argv[++i];
        else if (arg.substr(0, 13) == "--max-trades=")
            max_trades = std::strtoul(argv[i] + 13, nullptr, 10);
        else if (arg.substr(0, 9) == "--levels=")
            depth_levels = std::strtoul(argv[i] + 9, nullptr, 10);
    }

    Logger logger("trading_engine.log");
    logger.set_enabled(log_enabled);
    logger.start();

    MatchingEngine<PriceTimePolicy> engine;
    engine.start();
    logger.log("Engine started (PriceTimePolicy)");

    std::vector<Order> orders;
    try {
        orders = load_csv(csv_path);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << '\n';
        engine.stop();
        logger.stop();
        return 1;
    }

    logger.log("Loaded " + std::to_string(orders.size()) + " orders from CSV");

    for (auto& o : orders) {
        o.arrival_tsc = rdtsc();
        (void)engine.submit(o);
    }

    engine.stop();
    logger.log("Engine stopped");
    logger.stop();

    const BookStats stats = engine.stats();

    print_banner("Engine Statistics");
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Orders received     : " << stats.orders_received  << '\n';
    std::cout << "Orders rejected     : " << stats.orders_rejected  << '\n';
    std::cout << "Trades executed     : " << stats.trades_executed  << '\n';
    std::cout << "Volume traded       : " << stats.volume_traded    << '\n';
    std::cout << "Unmatched in book   : " << stats.unmatched_orders << '\n';
    std::cout << "Pool utilization    : " << engine.pool_utilization() << '\n';
    std::cout << "Elapsed (s)         : " << stats.elapsed_sec      << '\n';
    if (stats.elapsed_sec > 0.0)
        std::cout << "Throughput (ord/s)  : "
                  << static_cast<double>(stats.orders_received) / stats.elapsed_sec
                  << '\n';

    const auto& lat = engine.latency();
    print_banner("Match Latency (cycles)");
    std::cout << "Samples             : " << lat.count()        << '\n';
    std::cout << "Mean                : " << lat.mean_cycles()  << '\n';
    std::cout << "p50                 : " << lat.percentile(50) << '\n';
    std::cout << "p99                 : " << lat.percentile(99) << '\n';

    if (print_depth)
        engine.print_depth(depth_levels);

    if (print_trades) {
        print_banner("Recent Trades");
        const auto& trades = engine.trades();
        const std::size_t start = trades.size() > max_trades
            ? trades.size() - max_trades : 0;
        for (std::size_t i = start; i < trades.size(); ++i) {
            const auto& t = trades[i];
            std::cout << "Buy#" << std::setw(6) << t.buy_id
                      << " x Sell#" << std::setw(6) << t.sell_id
                      << "  @ " << from_ticks(t.exec_price)
                      << "  qty=" << t.quantity << '\n';
        }
    }

    return 0;
}
