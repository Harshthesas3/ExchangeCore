#include "Benchmarks/BenchmarkRunner.hpp"
#include <random>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace Exchange {

size_t BenchmarkRunner::getMemoryUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss * 1024; // convert KB to bytes
    }
#endif
    return 0;
}

BenchmarkResult BenchmarkRunner::run(uint64_t num_orders, std::function<void(double)> progress_callback) {
    // Create an isolated engine for benchmarking
    auto engine = std::make_shared<ExchangeEngine>();
    engine->addSymbol("TEST");

    size_t start_mem = getMemoryUsage();
    auto start_time = std::chrono::high_resolution_clock::now();

    // Use deterministic seed for repeatable results
    std::mt19937 rng(42); 
    double ref_price = 100.0;

    uint64_t report_interval = num_orders / 100;
    if (report_interval == 0) report_interval = 1;

    for (uint64_t i = 0; i < num_orders; ++i) {
        if (progress_callback && (i % report_interval == 0)) {
            progress_callback(static_cast<double>(i) * 100.0 / num_orders);
        }

        // Random walk of reference price (-0.02% to +0.02%)
        double pct_change = std::uniform_real_distribution<double>(-0.0002, 0.0002)(rng);
        ref_price *= (1.0 + pct_change);

        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        OrderType type = (rng() % 10 < 2) ? OrderType::Market : OrderType::Limit; // 20% Market, 80% Limit
        Quantity qty = (rng() % 50) + 1; // 1 to 50 shares
        Price price = 0;

        if (type == OrderType::Limit) {
            // 15% chance to place a crossing (matching) limit order
            bool cross = (rng() % 100 < 15);
            if (side == Side::Buy) {
                if (cross) {
                    price = doubleToPrice(ref_price * 1.001); // Buy above mid
                } else {
                    price = doubleToPrice(ref_price * 0.999); // Buy below mid
                }
            } else {
                if (cross) {
                    price = doubleToPrice(ref_price * 0.999); // Sell below mid
                } else {
                    price = doubleToPrice(ref_price * 1.001); // Sell above mid
                }
            }
        }

        if (price <= 0 && type == OrderType::Limit) {
            price = 1000000; // $100.00 floor
        }

        auto order = std::make_shared<Order>();
        order->id = i + 1;
        order->symbol = "TEST";
        order->side = side;
        order->type = type;
        order->price = price;
        order->qty = qty;
        order->timestamp = std::chrono::system_clock::now();
        order->client_id = "BENCH_CLIENT";

        engine->submitOrder(order);
    }

    if (progress_callback) {
        progress_callback(100.0);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    size_t end_mem = getMemoryUsage();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    auto stats = engine->getStatistics();
    size_t mem_used = (end_mem > start_mem) ? (end_mem - start_mem) : 0;
    double ops = (num_orders / duration_ms) * 1000.0;

    return BenchmarkResult{
        num_orders,
        duration_ms,
        ops,
        stats.avg_latency_us,
        static_cast<double>(stats.peak_latency_us),
        stats.trades_count,
        mem_used
    };
}

} // namespace Exchange
