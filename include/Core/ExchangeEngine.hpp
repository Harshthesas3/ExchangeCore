#pragma once

#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include "Core/OrderBook.hpp"
#include "Core/MatchingEngine.hpp"
#include "Core/Portfolio.hpp"
#include "Core/Statistics.hpp"
#include "API/Events.hpp"

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <deque>
#include <vector>

namespace Exchange {

class ExchangeEngine {
public:
    ExchangeEngine();
    ~ExchangeEngine() = default;

    // Symbol management
    void addSymbol(const std::string& symbol);
    bool hasSymbol(const std::string& symbol) const;
    std::vector<std::string> getSymbols() const;

    // Order operations (Thread-safe)
    void submitOrder(const std::shared_ptr<Order>& order);
    bool cancelOrder(OrderID id, const std::string& symbol);
    bool modifyOrder(OrderID id, const std::string& symbol, Price new_price, Quantity new_qty, OrderID new_order_id);

    // Snapshot queries (Thread-safe)
    std::vector<std::pair<Price, Quantity>> getBidDepthSnapshot(const std::string& symbol, size_t depth) const;
    std::vector<std::pair<Price, Quantity>> getAskDepthSnapshot(const std::string& symbol, size_t depth) const;
    Price getSpread(const std::string& symbol) const;
    Price getMidPrice(const std::string& symbol) const;
    double getVWAP(const std::string& symbol) const;

    StatisticsSnapshot getStatistics() const;
    Portfolio getPortfolio() const;
    std::vector<Trade> getRecentTrades(size_t limit = 50) const;

    // Event Registration
    void registerListener(std::shared_ptr<ExchangeEventListener> listener);
    void unregisterListener(const std::shared_ptr<ExchangeEventListener>& listener);

    // Reset simulator
    void reset();

private:
    void dispatchEvent(const Event& event);
    void handleTradeExecution(const Trade& trade);

    mutable std::mutex engine_mutex_;

    std::unordered_map<std::string, OrderBook> order_books_;
    std::unique_ptr<MatchingEngine> matching_engine_;
    Portfolio portfolio_;
    Statistics stats_;
    std::deque<Trade> recent_trades_;
    const size_t max_recent_trades_{100};

    std::vector<std::shared_ptr<ExchangeEventListener>> listeners_;
};

} // namespace Exchange
