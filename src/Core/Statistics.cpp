#include "Core/Statistics.hpp"

namespace Exchange {

Statistics::Statistics() {
    last_rate_time_ = std::chrono::steady_clock::now();
}

void Statistics::reset() {
    orders_received_ = 0;
    trades_count_ = 0;
    total_volume_ = 0;
    peak_latency_us_ = 0;
    total_latency_us_ = 0;
    latency_count_ = 0;

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    sum_price_qty_.clear();
    sum_qty_.clear();
    spreads_.clear();

    std::lock_guard<std::mutex> tp_lock(throughput_mutex_);
    last_rate_time_ = std::chrono::steady_clock::now();
    last_orders_count_ = 0;
    last_trades_count_ = 0;
    current_orders_per_sec_ = 0.0;
    current_trades_per_sec_ = 0.0;
}

void Statistics::recordOrderReceived() {
    orders_received_.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::recordLatency(int64_t latency_us) {
    if (latency_us < 0) return;
    total_latency_us_.fetch_add(latency_us, std::memory_order_relaxed);
    latency_count_.fetch_add(1, std::memory_order_relaxed);

    int64_t current_peak = peak_latency_us_.load(std::memory_order_relaxed);
    while (latency_us > current_peak &&
           !peak_latency_us_.compare_exchange_weak(current_peak, latency_us,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
        // Loop terminates when CAS succeeds or another thread updates to a higher peak
    }
}

void Statistics::recordTrade(const Trade& trade) {
    trades_count_.fetch_add(1, std::memory_order_relaxed);
    total_volume_.fetch_add(trade.qty, std::memory_order_relaxed);

    double trade_price = priceToDouble(trade.price);
    double value = trade_price * trade.qty;

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    sum_price_qty_[trade.symbol] += value;
    sum_qty_[trade.symbol] += trade.qty;
}

void Statistics::recordSpread(const std::string& symbol, Price spread) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    spreads_[symbol] = spread;
}

double Statistics::getVWAP(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    auto qty_it = sum_qty_.find(symbol);
    if (qty_it == sum_qty_.end() || qty_it->second == 0) {
        return 0.0;
    }
    auto val_it = sum_price_qty_.find(symbol);
    return val_it->second / qty_it->second;
}

Price Statistics::getSpread(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    auto it = spreads_.find(symbol);
    return (it != spreads_.end()) ? it->second : 0;
}

StatisticsSnapshot Statistics::getSnapshot() const {
    auto now = std::chrono::steady_clock::now();

    uint64_t ords = orders_received_.load(std::memory_order_relaxed);
    uint64_t trds = trades_count_.load(std::memory_order_relaxed);
    uint64_t vol = total_volume_.load(std::memory_order_relaxed);
    int64_t peak = peak_latency_us_.load(std::memory_order_relaxed);
    uint64_t tot_lat = total_latency_us_.load(std::memory_order_relaxed);
    uint64_t lat_cnt = latency_count_.load(std::memory_order_relaxed);

    double avg_lat = lat_cnt > 0 ? static_cast<double>(tot_lat) / lat_cnt : 0.0;

    std::lock_guard<std::mutex> lock(throughput_mutex_);
    double elapsed = std::chrono::duration<double>(now - last_rate_time_).count();
    if (elapsed >= 0.5) { // Calculate throughput rates at most twice a second
        current_orders_per_sec_ = (ords - last_orders_count_) / elapsed;
        current_trades_per_sec_ = (trds - last_trades_count_) / elapsed;
        last_orders_count_ = ords;
        last_trades_count_ = trds;
        last_rate_time_ = now;
    }

    return StatisticsSnapshot{
        ords,
        trds,
        vol,
        avg_lat,
        peak,
        current_orders_per_sec_,
        current_trades_per_sec_
    };
}

} // namespace Exchange
