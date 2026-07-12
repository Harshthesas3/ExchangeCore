#pragma once

#include "Core/ExchangeEngine.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <random>
#include <deque>
#include <unordered_map>

namespace Exchange {

class MarketSimulator {
public:
    explicit MarketSimulator(ExchangeEngine& engine);
    ~MarketSimulator();

    void start();
    void stop();
    bool isRunning() const { return running_; }

private:
    void run();

    ExchangeEngine& engine_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    std::unordered_map<std::string, double> ref_prices_;
    std::unordered_map<std::string, std::deque<OrderID>> bot_orders_;
    const size_t max_bot_orders_per_symbol_{30};
    
    OrderID next_bot_order_id_{1000000}; // Starts high to avoid overlap with TUI user order IDs
    std::mt19937 rng_{std::random_device{}()};
};

} // namespace Exchange
