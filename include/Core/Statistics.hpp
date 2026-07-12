#pragma once

#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>

namespace Exchange {

struct StatisticsSnapshot {
    uint64_t orders_received;
    uint64_t trades_count;
    uint64_t total_volume;
    double avg_latency_us;
    int64_t peak_latency_us;
    double orders_per_sec;
    double trades_per_sec;
};

class Statistics {
public:
    Statistics();

    // Reset statistics
    void reset();

    // Performance-critical path: lock-free updates
    void recordOrderReceived();
    void recordLatency(int64_t latency_us);

    // Updates for trade events
    void recordTrade(const Trade& trade);

    // Updates for market data changes
    void recordSpread(const std::string& symbol, Price spread);

    // Snapshot query (called by UI, does thread-safe aggregation)
    StatisticsSnapshot getSnapshot() const;

    // Symbol-specific metrics
    double getVWAP(const std::string& symbol) const;
    Price getSpread(const std::string& symbol) const;

private:
    // Atomic counters (lock-free)
    std::atomic<uint64_t> orders_received_{0};
    std::atomic<uint64_t> trades_count_{0};
    std::atomic<uint64_t> total_volume_{0};
    std::atomic<int64_t> peak_latency_us_{0};
    std::atomic<uint64_t> total_latency_us_{0};
    std::atomic<uint64_t> latency_count_{0};

    // Symbol metrics (protected by a mutex as they are updated on trades/market changes)
    mutable std::mutex metrics_mutex_;
    std::unordered_map<std::string, double> sum_price_qty_;
    std::unordered_map<std::string, uint64_t> sum_qty_;
    std::unordered_map<std::string, Price> spreads_;

    // Throughput tracking (updated dynamically on snapshot queries)
    mutable std::mutex throughput_mutex_;
    mutable std::chrono::steady_clock::time_point last_rate_time_;
    mutable uint64_t last_orders_count_{0};
    mutable uint64_t last_trades_count_{0};
    mutable double current_orders_per_sec_{0.0};
    mutable double current_trades_per_sec_{0.0};
};

} // namespace Exchange
