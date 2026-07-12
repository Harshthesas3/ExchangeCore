#pragma once

#include "Core/ExchangeEngine.hpp"
#include <string>
#include <chrono>
#include <functional>

namespace Exchange {

struct BenchmarkResult {
    uint64_t total_orders;
    double execution_time_ms;
    double orders_per_sec;
    double avg_latency_us;
    double peak_latency_us;
    uint64_t total_trades;
    size_t memory_used_bytes;
};

class BenchmarkRunner {
public:
    // Runs an isolated benchmark with a specified count of orders.
    // If progress_callback is provided, it will be invoked periodically (e.g. every 1% of orders).
    static BenchmarkResult run(uint64_t num_orders, std::function<void(double)> progress_callback = nullptr);

    // Returns the current Process working set size in bytes
    static size_t getMemoryUsage();
};

} // namespace Exchange
